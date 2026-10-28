#include "webbluetooth.h"

#include "../../managers/obdmanager.h"

#include <QIODevice>
#include <QLoggingCategory>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstring>
#include <string>
#include <vector>

Q_LOGGING_CATEGORY(lcWebBt, "octave.obd.webbt")

namespace {

constexpr const char *kScheme = "webbt://";
// What the main thread (notifications, link events) shares with the port
// device on the OBD worker thread
struct Shared {
    QMutex mutex;
    QWaitCondition changed;
    QByteArray rx;
    bool up = false;
    QString error;
    QString describe;   // the characteristics in use, for the connection log
};

Shared &shared()
{
    static Shared s;
    return s;
}

WebBluetoothLink *g_link = nullptr;

// clang-format off
// The JS half. Promise chains keep one GATT operation in flight at a time,
// which Chrome requires ("GATT operation already in progress").
EM_JS(void, octave_webbt_install, (), {
    if (Module.octaveWebBt)
        return;
    // Serial-over-GATT services of ELM327 BLE adapters, with the notify / write
    // characteristics to prefer when a service has more than one of each
    const canon = function (u) { return typeof u === "number" ? BluetoothUUID.canonicalUUID(u) : u; };
    const PROFILES = [
        { service: "49535343-fe7d-4ae5-8fa9-9fafd205e455",   // ISSC / Microchip transparent UART (OBDLink, NEXAS)
          notify: "49535343-1e4d-4bd9-ba61-23c647249616", write: "49535343-8841-43f4-a8d4-ecbe34729bb3" },
        { service: 0xfff0, notify: 0xfff1, write: 0xfff2 },  // Vgate, Konnwei, many clones
        { service: 0xffe0, notify: 0xffe1, write: 0xffe1 },  // HM-10 style, one characteristic both ways
        { service: 0x18f0, notify: 0x2af0, write: 0x2af1 },  // some v2.x clones
        { service: "e7810a71-73ae-499d-8c15-faa9aef0c3f2",   // vLinker / IOS-Vlink
          notify: "bef8d6c9-9c21-4c9e-b632-bd58c1009f9f", write: "bef8d6c9-9c21-4c9e-b632-bd58c1009f9f" },
    ];
    const SERVICES = PROFILES.map(function (p) { return p.service; });
    // Name prefixes are case sensitive
    const NAMES = ["OBD", "Obd", "obd", "ELM", "elm", "V-LINK", "VLINK", "vLinker", "IOS-Vlink", "Android-Vlink",
                   "Vgate", "vgate", "iCar", "Veepeak", "VEEPEAK", "OBDLink", "NEXAS", "NexLink", "KONNWEI",
                   "Konnwei", "LELink", "Viecar", "Carista", "BAFX", "Kiwi"];
    const CHUNK = 20;   // the smallest BLE payload; every adapter takes it
    const event = function (kind, a, b, c) { Module.octaveWebBtEvent(kind, a || "", b || "", c || ""); };
    const errText = function (e) { return (e && e.message) ? e.message : String(e); };
    const listening = new WeakSet();

    const bt = {
        known: new Map(),   // id -> BluetoothDevice the user picked this session
        device: null, rxChar: null, txChar: null,
        chain: Promise.resolve(),
        gen: 0,

        available: function () {
            return !!(window.isSecureContext && navigator.bluetooth && navigator.bluetooth.requestDevice);
        },

        // Why available() is false, for the message: "insecure", "linux-chromium"
        // (Web Bluetooth is behind a flag there), "firefox", "apple" or "other"
        browserKind: function () {
            const ua = navigator.userAgent || "";
            const has = function (t) { return ua.indexOf(t) >= 0; };
            const chromium = has("Chrome/") || has("Chromium/");
            if (!window.isSecureContext)
                return "insecure";
            if (has("Firefox/"))
                return "firefox";
            if (has("iPhone") || has("iPad") || (has("Safari/") && !chromium))
                return "apple";
            if (has("Linux") && !has("Android") && chromium)
                return "linux-chromium";
            return "other";
        },

        choose: async function (every) {
            try {
                const options = every
                    ? { acceptAllDevices: true, optionalServices: SERVICES }
                    : { filters: SERVICES.map(function (s) { return { services: [s] }; })
                                 .concat(NAMES.map(function (n) { return { namePrefix: n }; })),
                        optionalServices: SERVICES };
                const d = await navigator.bluetooth.requestDevice(options);
                bt.known.set(d.id, d);
                event("chosen", d.id, d.name || "Bluetooth LE adapter");
            } catch (e) {
                event("chooseFailed", e.name || "", errText(e));
            }
        },

        drop: function () {
            const d = bt.device;
            bt.device = null; bt.rxChar = null; bt.txChar = null;
            if (d && d.gatt.connected)
                d.gatt.disconnect();
        },

        disconnect: function () { ++bt.gen; bt.drop(); },

        // Every known serial service the adapter has, as {service, notify,
        // write}. Some adapters carry several (the NEXAS NexLINK has ISSC and
        // FFF0, and its ISSC side never answered), so with more than one the
        // first that answers "ATI" wins.
        pick: async function (server) {
            const canNotify = function (c) { return c && (c.properties.notify || c.properties.indicate); };
            const canWrite = function (c) { return c && (c.properties.write || c.properties.writeWithoutResponse); };
            const found = [];
            for (const p of PROFILES) {
                let service, chars;
                try {
                    service = await server.getPrimaryService(p.service);
                    chars = await service.getCharacteristics();
                } catch (e) {
                    if (e && e.name === "NotFoundError")
                        continue;
                    throw e;   // a link that went away, not a missing service
                }
                const byUuid = function (u) {
                    return chars.find(function (c) { return c.uuid === canon(u); });
                };
                // The profile's own pair, then any notify + write pair of the
                // service (a vendor that moved its characteristics)
                const n = byUuid(p.notify), w = byUuid(p.write);
                if (canNotify(n) && canWrite(w))
                    found.push({ service: service, notify: n, write: w });
                const gn = chars.find(canNotify), gw = chars.find(canWrite);
                if (gn && gw && (gn !== n || gw !== w))
                    found.push({ service: service, notify: gn, write: gw });
            }
            if (found.length < 2)
                return found[0] || null;
            // The pair that answered last time on this adapter first: no
            // probe of a service known to stay silent
            const remembered = bt.recall(server.device.id);
            found.sort(function (x, y) {
                return (bt.describe(y) === remembered) - (bt.describe(x) === remembered);
            });
            for (const f of found) {
                if (await bt.probe(f)) {
                    bt.remember(server.device.id, bt.describe(f));
                    return f;
                }
            }
            return found[0];
        },

        // Per-adapter memory of the serial service that answered. Browser
        // storage may be off (private window): then every connect probes.
        recall: function (id) {
            try { return localStorage.getItem("octave.webbt." + id) || ""; } catch (e) { return ""; }
        },
        remember: function (id, what) {
            try { localStorage.setItem("octave.webbt." + id, what); } catch (e) {}
        },

        describe: function (f) {
            // 16-bit ids as "fff1", 128-bit ones by their first two groups
            const id = function (u) {
                return u.endsWith("-0000-1000-8000-00805f9b34fb") ? u.slice(4, 8) : u.slice(0, 13);
            };
            return "service " + id(f.service.uuid) + ", notify " + id(f.notify.uuid) + ", write " + id(f.write.uuid)
                + (f.write.properties.write ? " (with response)" : " (no response)");
        },

        // Does this service answer an ELM327? Leaves notifications on if so.
        // Waits for the whole answer (the ">" prompt): a tail arriving after
        // the link is up was read as the answer to ATZ, and the commands
        // after it went to an adapter still resetting.
        probe: async function (f) {
            let got = 0, prompt = false;
            const count = function (e) {
                const v = new Uint8Array(e.target.value.buffer, e.target.value.byteOffset, e.target.value.byteLength);
                got += v.length;
                if (v.indexOf(62) >= 0)
                    prompt = true;
            };
            f.notify.addEventListener("characteristicvaluechanged", count);
            try {
                await f.notify.startNotifications();
                const ati = new TextEncoder().encode("ATI" + String.fromCharCode(13));
                await (f.write.properties.write ? f.write.writeValueWithResponse(ati)
                                                : f.write.writeValueWithoutResponse(ati));
                for (let i = 0; i < 15 && !prompt; ++i)
                    await new Promise(function (r) { setTimeout(r, 100); });
            } catch (e) {
                event("log", "probe " + bt.describe(f) + ": " + errText(e));
                // A busy stack, not a silent service: let connect() retry
                if (errText(e).indexOf("unknown reason") >= 0 || errText(e).indexOf("in progress") >= 0) {
                    f.notify.removeEventListener("characteristicvaluechanged", count);
                    throw e;
                }
            }
            f.notify.removeEventListener("characteristicvaluechanged", count);
            if (!got) {
                try { await f.notify.stopNotifications(); } catch (e) {}
            }
            event("log", "probe " + bt.describe(f) + ": " + (got ? got + " bytes back" : "silent"));
            return got > 0;
        },

        connect: async function (id) {
            const gen = ++bt.gen;
            bt.drop();
            const d = bt.known.get(id);
            if (!d) {
                event("error", "The browser no longer knows that adapter. Pick it again from the Bluetooth list.");
                return;
            }
            if (!listening.has(d)) {
                listening.add(d);
                d.addEventListener("gattserverdisconnected", function () {
                    if (bt.device !== d)
                        return;   // dropped on purpose
                    bt.device = null; bt.rxChar = null; bt.txChar = null;
                    event("down", "The Bluetooth adapter disconnected.");
                });
            }
            // Android's stack is often still busy with the last link (a page
            // reload, a reconnect): "GATT operation failed for unknown reason",
            // "already in progress". Drop the link, give it a moment, again.
            const transient = function (e) {
                const m = errText(e);
                return m.indexOf("unknown reason") >= 0 || m.indexOf("in progress") >= 0
                    || m.indexOf("disconnected") >= 0 || (e && e.name === "NetworkError");
            };
            for (let attempt = 1; ; ++attempt) {
                try {
                    await bt.connectOnce(d, gen);
                    return;
                } catch (e) {
                    if (gen !== bt.gen)
                        return;
                    if (d.gatt.connected)
                        d.gatt.disconnect();
                    if (attempt >= 4 || !transient(e)) {
                        event("error", "Couldn't connect to " + (d.name || "the adapter") + ": " + errText(e));
                        return;
                    }
                    event("log", "connect attempt " + attempt + ": " + errText(e) + "; retrying");
                    await new Promise(function (r) { setTimeout(r, 700 * attempt); });
                    if (gen !== bt.gen)
                        return;
                }
            }
        },

        // One try: GATT connect, pick the serial service, notifications on.
        // Throws on a failure worth retrying; reports the rest itself.
        connectOnce: async function (d, gen) {
            const server = await d.gatt.connect();
            const found = await bt.pick(server);
            if (gen !== bt.gen)
                return;   // superseded
            if (!found) {
                d.gatt.disconnect();
                event("error", (d.name || "That device") + " has no OBD serial service. It may not be an ELM327 adapter.");
                return;
            }
            found.notify.addEventListener("characteristicvaluechanged", bt.onValue);
            await found.notify.startNotifications();
            if (gen !== bt.gen)
                return;
            bt.device = d; bt.rxChar = found.notify; bt.txChar = found.write;
            bt.chain = Promise.resolve();
            event("up", found.service.uuid, d.name || "", bt.describe(found));
        },

        onValue: function (e) {
            if (e.target !== bt.rxChar)
                return;
            const v = e.target.value;
            Module.octaveWebBtRx(new Uint8Array(v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength)));
        },

        // `bytes` may view the wasm heap (shared memory); slice() copies,
        // which writeValue needs anyway
        write: function (bytes) {
            const c = bt.txChar;
            if (!c)
                return;
            // With response where the adapter allows it: the promise then
            // settles when the adapter has the bytes, so the chain really
            // keeps one write in flight. Without response it settles at
            // once, and Android Chrome refused the next write ("GATT
            // operation already in progress"), dropping whole commands.
            const noResponse = !c.properties.write;
            const send = async function (part) {
                for (let attempt = 0; ; ++attempt) {
                    try {
                        return await (noResponse ? c.writeValueWithoutResponse(part) : c.writeValueWithResponse(part));
                    } catch (e) {
                        if (attempt >= 5 || bt.txChar !== c || errText(e).indexOf("in progress") < 0)
                            throw e;
                        await new Promise(function (r) { setTimeout(r, 20 * (attempt + 1)); });
                    }
                }
            };
            for (let i = 0; i < bytes.length; i += CHUNK) {
                const part = bytes.slice(i, i + CHUNK);
                bt.chain = bt.chain
                    .then(function () { return send(part); })
                    .catch(function (e) {
                        if (bt.txChar === c)
                            event("log", "write failed: " + errText(e));
                    });
            }
        },
    };
    Module.octaveWebBt = bt;
});
// clang-format on

emscripten::val helper()
{
    return emscripten::val::module_property("octaveWebBt");
}

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

void onEvent(const std::string &kind, const std::string &a, const std::string &b, const std::string &c)
{
    if (g_link)
        g_link->handleEvent(qs(kind), qs(a), qs(b), qs(c));
}

void onBytes(emscripten::val bytes)
{
    const std::vector<uint8_t> v = emscripten::convertJSArrayToNumberVector<uint8_t>(bytes);
    if (g_link && !v.empty())
        g_link->handleBytes(QByteArray(reinterpret_cast<const char *>(v.data()), qsizetype(v.size())));
}

// The ELM327 as the OBD worker sees it. Lives on the worker thread.
class PortDevice : public QIODevice
{
public:
    using QIODevice::QIODevice;

    bool isSequential() const override { return true; }

    bool open(OpenMode mode) override
    {
        Shared &s = shared();
        QMutexLocker lock(&s.mutex);
        if (!s.up) {
            // Ask the main thread to bring the last adapter back and fail
            // now: OBDManager retries the port on its own schedule. Waiting
            // here for the main thread let a main thread that waits on this
            // worker (a reconnect tearing it down) freeze the page for the
            // whole connect timeout.
            lock.unlock();
            QMetaObject::invokeMethod(g_link, [] { g_link->reconnect(); }, Qt::QueuedConnection);
            setErrorString(QStringLiteral("Reconnecting the Bluetooth adapter"));
            return false;
        }
        s.rx.clear();
        setObjectName(s.describe);
        return QIODevice::open(mode);
    }

    qint64 bytesAvailable() const override
    {
        Shared &s = shared();
        QMutexLocker lock(&s.mutex);
        return s.rx.size() + QIODevice::bytesAvailable();
    }

    bool waitForReadyRead(int msecs) override
    {
        Shared &s = shared();
        QMutexLocker lock(&s.mutex);
        if (s.rx.isEmpty() && s.up)
            s.changed.wait(&s.mutex, QDeadlineTimer(msecs));
        if (!s.rx.isEmpty())
            return true;
        if (!s.up) {
            // OBDConnectionWorker::sendCommand() reports a closed custom
            // transport as a lost connection, with this error
            const QString err = s.error.isEmpty() ? QStringLiteral("The Bluetooth adapter disconnected") : s.error;
            lock.unlock();
            setErrorString(err);
            close();
        }
        return false;
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        Shared &s = shared();
        QMutexLocker lock(&s.mutex);
        const qint64 n = qMin<qint64>(maxSize, s.rx.size());
        std::memcpy(data, s.rx.constData(), size_t(n));
        s.rx.remove(0, n);
        return n;
    }

    qint64 writeData(const char *data, qint64 size) override
    {
        const QByteArray bytes(data, size);
        QMetaObject::invokeMethod(g_link, [bytes] { g_link->write(bytes); }, Qt::QueuedConnection);
        return size;
    }
};

} // namespace

EMSCRIPTEN_BINDINGS(octave_webbt)
{
    emscripten::function("octaveWebBtEvent", &onEvent);
    emscripten::function("octaveWebBtRx", &onBytes);
}

WebBluetoothLink::WebBluetoothLink(QObject *parent)
    : QObject(parent)
{
    octave_webbt_install();
}

WebBluetoothLink *WebBluetoothLink::instance()
{
    if (!g_link)
        g_link = new WebBluetoothLink;
    return g_link;
}

void WebBluetoothLink::registerTransport()
{
    instance();
    OBDConnectionWorker::addCustomTransport(
        [](const QString &port) { return port.startsWith(QLatin1String(kScheme)); },
        [](const QString &, QObject *parent) -> QIODevice * { return new PortDevice(parent); });
}

QString WebBluetoothLink::portFor(const QString &deviceId)
{
    return QLatin1String(kScheme) + deviceId;
}

bool WebBluetoothLink::available() const
{
    return helper().call<bool>("available");
}

QString WebBluetoothLink::browserKind() const
{
    return QString::fromStdString(helper().call<std::string>("browserKind"));
}

void WebBluetoothLink::choose(bool everyDevice)
{
    helper().call<void>("choose", everyDevice);
}

void WebBluetoothLink::connectDevice(const QString &id)
{
    {
        Shared &s = shared();
        QMutexLocker lock(&s.mutex);
        s.up = false;
        s.error.clear();
        s.changed.wakeAll();
    }
    m_connectingId = id;
    m_connecting = true;
    qCInfo(lcWebBt) << "connecting" << id;
    helper().call<void>("connect", id.toStdString());
}

void WebBluetoothLink::disconnectDevice()
{
    helper().call<void>("disconnect");
    m_connectingId.clear();
    m_connecting = false;
    handleEvent(QStringLiteral("down"), QStringLiteral("Disconnected"), QString());
}

void WebBluetoothLink::reconnect()
{
    // The OBD worker asks on every retry; let a connect under way finish
    if (m_connecting)
        return;
    if (m_lastId.isEmpty()) {
        handleEvent(QStringLiteral("error"), QStringLiteral("No Bluetooth adapter picked yet"), QString());
        return;
    }
    connectDevice(m_lastId);
}

void WebBluetoothLink::write(const QByteArray &bytes)
{
    helper().call<void>("write", emscripten::val(emscripten::typed_memory_view(
                                     size_t(bytes.size()), reinterpret_cast<const uint8_t *>(bytes.constData()))));
}

void WebBluetoothLink::handleBytes(const QByteArray &bytes)
{
    Shared &s = shared();
    QMutexLocker lock(&s.mutex);
    s.rx.append(bytes);
    s.changed.wakeAll();
}

void WebBluetoothLink::handleEvent(const QString &kind, const QString &a, const QString &b, const QString &c)
{
    if (kind == QLatin1String("chosen")) {
        qCInfo(lcWebBt) << "picked" << b << a;
        emit chosen(a, b);
    } else if (kind == QLatin1String("chooseFailed")) {
        // a = DOMException name, b = message
        qCInfo(lcWebBt) << "chooser:" << a << b;
        if (a == QLatin1String("NotFoundError") && b.contains(QLatin1String("cancel"), Qt::CaseInsensitive))
            emit chooseFailed(QString(), true);
        else if (a == QLatin1String("SecurityError"))
            emit chooseFailed(QStringLiteral("Tap Pick Bluetooth Adapter to open the browser's list."), false);
        else if (a == QLatin1String("NotFoundError"))
            emit chooseFailed(QStringLiteral("The browser can't reach Bluetooth. Check that Bluetooth is on. On "
                                             "Linux, Chrome may need Web Bluetooth turned on in chrome://flags."),
                              false);
        else
            emit chooseFailed(QStringLiteral("Bluetooth: %1").arg(b), false);
    } else if (kind == QLatin1String("up")) {
        const QString id = m_connectingId;
        m_lastId = id;
        m_connecting = false;
        qCInfo(lcWebBt) << "link up to" << b << c;
        {
            Shared &s = shared();
            QMutexLocker lock(&s.mutex);
            s.rx.clear();
            s.describe = c;
            s.up = true;
            s.error.clear();
            s.changed.wakeAll();
        }
        emit linkUp(id, b);
    } else if (kind == QLatin1String("down") || kind == QLatin1String("error")) {
        qCWarning(lcWebBt) << kind << a;
        if (kind == QLatin1String("error"))
            m_connecting = false;
        {
            Shared &s = shared();
            QMutexLocker lock(&s.mutex);
            s.up = false;
            s.error = a;
            s.changed.wakeAll();
        }
        emit linkDown(a);
    } else {
        qCInfo(lcWebBt) << a;
    }
}
