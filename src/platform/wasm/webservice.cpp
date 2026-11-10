#include "webservice.h"

#include "../../service/commandserver.h"

#include <QCoreApplication>
#include <QHash>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

#include <emscripten.h>
#include <emscripten/bind.h>

#include <memory>
#include <string>

Q_LOGGING_CATEGORY(lcWebService, "octave.webservice")

namespace {

// Every page connection stops answering past this many (a frontend that
// leaks connections in a loop): the playground runs one or two
constexpr int kMaxConnections = 32;
// Each delivery of queued events is at most this many per connection, so a
// flood of events can't hold the page's main thread for long
constexpr int kMaxEventsPerTick = 400;

struct State {
    QPointer<OctaveService::CommandServer> server;
    QHash<int, std::shared_ptr<OctaveService::Connection>> connections;
    int nextId = 0;
    QTimer *pump = nullptr;
};
State *g_state = nullptr;

// clang-format off
EM_JS(void, octave_websvc_install, (), {
    if (Module.octaveService)
        return;
    const handlers = new Map();
    Module.octaveServiceDeliver = function (id, text) {
        const h = handlers.get(id);
        if (h) {
            try { h(text); } catch (e) { console.error("octaveService handler:", e); }
        }
    };
    Module.octaveService = {
        open: function (onMessage) {
            const id = Module.octaveServiceOpen();
            if (id > 0)
                handlers.set(id, onMessage);
            return id;
        },
        send: function (id, text) { Module.octaveServiceSend(id, String(text)); },
        close: function (id) {
            handlers.delete(id);
            Module.octaveServiceClose(id);
        },
    };
    // A page outside the Qt loader (the playground's own script) finds it here
    globalThis.octaveService = Module.octaveService;
    if (typeof globalThis.onOctaveServiceReady === "function") {
        try { globalThis.onOctaveServiceReady(Module.octaveService); } catch (e) { console.error(e); }
    }
});

EM_JS(void, octave_websvc_deliver, (int id, const char *text), {
    Module.octaveServiceDeliver(id, UTF8ToString(text));
});
// clang-format on

void deliver(int id, const QByteArray &text)
{
    QByteArray line = text.trimmed();
    if (line.isEmpty())
        return;
    line.append('\0');
    octave_websvc_deliver(id, line.constData());
}

void pumpEvents()
{
    if (!g_state)
        return;
    // Copy: a handler may close (or open) a connection while we deliver
    const auto connections = g_state->connections;
    for (auto it = connections.cbegin(); it != connections.cend(); ++it) {
        const QList<OctaveService::Connection::Item> items = it.value()->takeAll();
        int sent = 0;
        for (const OctaveService::Connection::Item &item : items) {
            if (item.binary)
                continue;   // stream records: no video in the browser build
            if (++sent > kMaxEventsPerTick)
                break;      // the connection's own drop rule covers a reader that falls behind
            deliver(it.key(), item.bytes);
        }
    }
}

int onOpen()
{
    if (!g_state || !g_state->server)
        return 0;
    if (g_state->connections.size() >= kMaxConnections) {
        qCWarning(lcWebService) << "refused a page connection:" << kMaxConnections << "are open";
        return 0;
    }
    auto c = std::make_shared<OctaveService::Connection>();
    c->setPeer(true, QStringLiteral("page"));   // the visitor's own page: a program on the device
    g_state->server->trackConnection(c);
    const int id = ++g_state->nextId;
    g_state->connections.insert(id, c);
    if (!g_state->pump->isActive())
        g_state->pump->start();
    qCInfo(lcWebService) << "page connection" << id << "open";
    return id;
}

void onSend(int id, const std::string &text)
{
    if (!g_state)
        return;
    const QByteArray line = QByteArray::fromStdString(text);
    // Queued: JS may call in from inside a Qt call that delivered an event
    QMetaObject::invokeMethod(QCoreApplication::instance(), [id, line] {
        if (!g_state || !g_state->server)
            return;
        const auto c = g_state->connections.value(id);
        if (!c || c->isClosed())
            return;
        QByteArray reply;
        try {
            reply = g_state->server->handleLineOn(c, line);
        } catch (const std::exception &e) {
            qCWarning(lcWebService) << "page request failed:" << e.what();
            return;
        }
        // Events the request queued (a subscribe's first values) go after its reply
        deliver(id, reply);
        pumpEvents();
    }, Qt::QueuedConnection);
}

void onClose(int id)
{
    if (!g_state)
        return;
    const auto c = g_state->connections.take(id);
    if (c)
        c->close();   // its subscriptions and claims end (cleanups on the GUI thread)
    if (g_state->connections.isEmpty())
        g_state->pump->stop();
    qCInfo(lcWebService) << "page connection" << id << "closed";
}

} // namespace

EMSCRIPTEN_BINDINGS(octave_websvc)
{
    emscripten::function("octaveServiceOpen", &onOpen);
    emscripten::function("octaveServiceSend", &onSend);
    emscripten::function("octaveServiceClose", &onClose);
}

void WebService::install(OctaveService::CommandServer *server)
{
    if (g_state || !server)
        return;
    g_state = new State;
    g_state->server = server;
    g_state->pump = new QTimer(QCoreApplication::instance());
    g_state->pump->setInterval(16);
    QObject::connect(g_state->pump, &QTimer::timeout, g_state->pump, &pumpEvents);
    octave_websvc_install();
    qCInfo(lcWebService) << "the service is open to this page (Module.octaveService)";
}
