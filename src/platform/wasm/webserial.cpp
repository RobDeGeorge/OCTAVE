#include "webserial.h"

#include "../../managers/obdmanager.h"
#include "webport.h"

#include <QLoggingCategory>

#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>
#include <vector>

Q_LOGGING_CATEGORY(lcWebSerial, "octave.obd.webserial")

namespace {

constexpr const char *kScheme = "webserial://";

WebPortChannel &channel()
{
    static WebPortChannel c;
    return c;
}

WebSerialLink *g_link = nullptr;

// clang-format off
// The JS half
EM_JS(void, octave_webserial_install, (), {
    if (Module.octaveWebSerial)
        return;
    // What OBDConnectionWorker::openSerialPort() uses: the rate ELM327 cable
    // adapters ship at. A Bluetooth serial port ignores it.
    const BAUD = 38400;
    const CHIPS = { 0x0403: "FTDI", 0x1a86: "CH340", 0x10c4: "CP210x", 0x067b: "Prolific", 0x04d8: "Microchip" };
    const event = function (kind, a, b, c) { Module.octaveWebSerialEvent(kind, a || "", b || "", c || ""); };
    const errText = function (e) { return (e && e.message) ? e.message : String(e); };
    const info = function (p) { try { return p.getInfo() || {}; } catch (e) { return {}; } };
    const hex4 = function (n) { return ("000" + (n || 0).toString(16)).slice(-4); };
    const nameOf = function (p) {
        const i = info(p);
        if (i.bluetoothServiceClassId)
            return "Bluetooth serial adapter";
        if (i.usbVendorId === undefined)
            return "Serial adapter";
        return "USB adapter (" + (CHIPS[i.usbVendorId] || hex4(i.usbVendorId) + ":" + hex4(i.usbProductId)) + ")";
    };
    const same = function (p, q) {
        const a = info(p), b = info(q);
        return a.usbVendorId === b.usbVendorId && a.usbProductId === b.usbProductId
            && a.bluetoothServiceClassId === b.bluetoothServiceClassId;
    };

    const ws = {
        known: new Map(),   // id -> SerialPort the user picked this session
        next: 1,
        port: null, reader: null, writer: null,
        chain: Promise.resolve(),     // one write in flight
        closing: Promise.resolve(),   // settles when the last port is closed
        gen: 0,

        available: function () {
            return !!(window.isSecureContext && navigator.serial && navigator.serial.requestPort);
        },

        choose: async function () {
            try {
                const p = await navigator.serial.requestPort();
                let id = "";
                ws.known.forEach(function (v, k) { if (v === p) id = k; });
                if (!id) {
                    id = "port" + ws.next++;
                    ws.known.set(id, p);
                }
                event("chosen", id, nameOf(p));
            } catch (e) {
                event("chooseFailed", e.name || "", errText(e));
            }
        },

        // Lets go of the port. A port only closes once its streams are
        // unlocked, and only opens again once it has closed: connect() waits
        // for the promise.
        drop: function () {
            const p = ws.port, r = ws.reader, w = ws.writer;
            ws.port = null; ws.reader = null; ws.writer = null;
            if (!p)
                return ws.closing;
            ws.closing = (async function () {
                try { await r.cancel(); } catch (e) {}
                try { r.releaseLock(); } catch (e) {}
                try { w.releaseLock(); } catch (e) {}
                try { await p.close(); } catch (e) {}
            })();
            return ws.closing;
        },

        disconnect: function () { ++ws.gen; ws.drop(); },

        connect: async function (id) {
            const gen = ++ws.gen;
            await ws.drop();
            if (gen !== ws.gen)
                return;   // superseded
            let p = ws.known.get(id);
            if (!p) {
                event("error", "The browser no longer knows that adapter. Pick it again with Pick USB Adapter.");
                return;
            }
            try {
                try {
                    await p.open({ baudRate: BAUD });
                } catch (e) {
                    // Unplugged and plugged back in: the browser has a new
                    // port object for the same adapter, already allowed
                    const ports = await navigator.serial.getPorts();
                    const again = ports.find(function (q) { return q !== p && same(q, p); });
                    if (!again)
                        throw e;
                    ws.known.set(id, again);
                    p = again;
                    await p.open({ baudRate: BAUD });
                }
            } catch (e) {
                if (gen === ws.gen)
                    event("error", "Couldn't open the " + nameOf(p) + ": " + errText(e));
                return;
            }
            if (gen !== ws.gen) {
                try { await p.close(); } catch (e) {}
                return;
            }
            ws.port = p;
            ws.writer = p.writable.getWriter();
            ws.chain = Promise.resolve();
            event("up", id, nameOf(p), BAUD + " baud");
            ws.pump(p, gen);
        },

        // Reads until the port is dropped or goes away. A framing or overrun
        // error ends one stream and the port offers a fresh one; a port that
        // is gone has none.
        pump: async function (p, gen) {
            let why = "";
            let errors = 0;
            while (gen === ws.gen && ws.port === p && p.readable && errors < 10) {
                const reader = p.readable.getReader();
                ws.reader = reader;
                let done = false;
                try {
                    for (;;) {
                        const r = await reader.read();
                        if (r.done) {
                            done = true;
                            break;
                        }
                        if (r.value && r.value.length) {
                            errors = 0;
                            Module.octaveWebSerialRx(r.value);
                        }
                    }
                } catch (e) {
                    why = errText(e);
                    ++errors;
                    try { reader.releaseLock(); } catch (e2) {}
                }
                if (done)
                    break;
            }
            if (gen !== ws.gen || ws.port !== p)
                return;   // dropped on purpose
            ws.drop();
            event("down", "The adapter disconnected" + (why ? " (" + why + ")." : "."));
        },

        // `bytes` may view the wasm heap (shared memory); slice() copies
        write: function (bytes) {
            const w = ws.writer;
            if (!w)
                return;
            const part = bytes.slice();
            ws.chain = ws.chain
                .then(function () { return w.write(part); })
                .catch(function (e) {
                    if (ws.writer === w)
                        event("log", "write failed: " + errText(e));
                });
        },
    };
    Module.octaveWebSerial = ws;
});
// clang-format on

emscripten::val helper()
{
    return emscripten::val::module_property("octaveWebSerial");
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

} // namespace

EMSCRIPTEN_BINDINGS(octave_webserial)
{
    emscripten::function("octaveWebSerialEvent", &onEvent);
    emscripten::function("octaveWebSerialRx", &onBytes);
}

WebSerialLink::WebSerialLink(QObject *parent)
    : QObject(parent)
{
    octave_webserial_install();
}

WebSerialLink *WebSerialLink::instance()
{
    if (!g_link)
        g_link = new WebSerialLink;
    return g_link;
}

void WebSerialLink::registerTransport()
{
    instance();
    OBDConnectionWorker::addCustomTransport(
        [](const QString &port) { return port.startsWith(QLatin1String(kScheme)); },
        [](const QString &, QObject *parent) -> QIODevice * {
            return channel().createDevice(parent, {g_link, [] { g_link->reconnect(); },
                                                   [](const QByteArray &bytes) { g_link->write(bytes); },
                                                   QStringLiteral("Reopening the adapter's port"),
                                                   QStringLiteral("The adapter disconnected")});
        });
}

QString WebSerialLink::portFor(const QString &portId)
{
    return QLatin1String(kScheme) + portId;
}

bool WebSerialLink::available() const
{
    return helper().call<bool>("available");
}

void WebSerialLink::choose()
{
    helper().call<void>("choose");
}

void WebSerialLink::connectPort(const QString &id)
{
    channel().connecting();
    m_connectingId = id;
    m_connecting = true;
    qCInfo(lcWebSerial) << "opening" << id;
    helper().call<void>("connect", id.toStdString());
}

void WebSerialLink::disconnectPort()
{
    const bool had = m_connecting || !m_lastId.isEmpty();
    helper().call<void>("disconnect");
    m_connectingId.clear();
    m_connecting = false;
    m_lastId.clear();
    if (had)
        handleEvent(QStringLiteral("down"), QStringLiteral("Disconnected"), QString());
}

void WebSerialLink::reconnect()
{
    // The OBD worker asks on every retry; let an open under way finish
    if (m_connecting || m_lastId.isEmpty())
        return;
    connectPort(m_lastId);
}

void WebSerialLink::write(const QByteArray &bytes)
{
    helper().call<void>("write", emscripten::val(emscripten::typed_memory_view(
                                     size_t(bytes.size()), reinterpret_cast<const uint8_t *>(bytes.constData()))));
}

void WebSerialLink::handleBytes(const QByteArray &bytes)
{
    channel().received(bytes);
}

void WebSerialLink::handleEvent(const QString &kind, const QString &a, const QString &b, const QString &c)
{
    if (kind == QLatin1String("chosen")) {
        qCInfo(lcWebSerial) << "picked" << b << a;
        emit chosen(a, b);
    } else if (kind == QLatin1String("chooseFailed")) {
        // a = DOMException name, b = message
        qCInfo(lcWebSerial) << "chooser:" << a << b;
        if (a == QLatin1String("NotFoundError"))
            emit chooseFailed(QString(), true);   // closed without a pick
        else if (a == QLatin1String("SecurityError"))
            emit chooseFailed(QStringLiteral("Tap Pick USB Adapter to open the browser's list."), false);
        else
            emit chooseFailed(QStringLiteral("Serial port: %1").arg(b), false);
    } else if (kind == QLatin1String("up")) {
        m_lastId = a;
        m_connecting = false;
        qCInfo(lcWebSerial) << "port open:" << b << c;
        channel().up(c);
        emit linkUp(a, b);
    } else if (kind == QLatin1String("down") || kind == QLatin1String("error")) {
        qCWarning(lcWebSerial) << kind << a;
        if (kind == QLatin1String("error"))
            m_connecting = false;
        channel().down(a);
        emit linkDown(a);
    } else {
        qCInfo(lcWebSerial) << a;
    }
}
