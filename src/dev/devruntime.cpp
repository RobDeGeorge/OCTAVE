#include "devruntime.h"

#include "collagebridge.h"
#include "../service/commandserver.h"
#include "../sim/carsim.h"
#include "../sim/simulator.h"
#include "coreactions.h"
#include "devkeyboard.h"
#include "devtoolsmanager.h"
#include "perfmonitor.h"
#include "uidriver.h"

#include "../managers/obdmanager.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>

#include <cstdio>

Q_LOGGING_CATEGORY(lcDev, "octave.dev")

namespace OctaveDev {

Runtime::Runtime(const QStringList &arguments, const AppObjects &app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    for (int i = 1; i < arguments.size(); ++i) {
        const QString &a = arguments.at(i);
        if (a == QLatin1String("--dev"))
            m_dev = true;
        else if (a == QLatin1String("--profile"))
            m_profile = true;
    }
}

Runtime::~Runtime()
{
    shutdown();
}

void Runtime::beforeLoad()
{
    QQmlContext *ctx = m_app.engine->rootContext();

    if (m_dev) {
        std::fprintf(stdout,
                     "[DEV] Installing dev runtime (C++)...\n"
                     "============================================================\n"
                     "KEYBOARD CONTROLS:\n"
                     "  W/Up   = Accelerate    S/Down = Brake\n"
                     "  Space  = Rev engine    E      = Toggle engine\n"
                     "  Q/R    = Shift down/up 1-6    = Set gear\n"
                     "  F1-F4  = Test scenarios (cold/hot/low fuel/reset)\n"
                     "  Tab    = Toggle dev overlay\n"
                     "============================================================\n");
        std::fflush(stdout);

        m_keyboard = new DevKeyboardController(m_app.sim ? m_app.sim->car() : nullptr, this);
        m_tools = new DevToolsManager(m_app.projectRoot, this);
        ctx->setContextProperty(QStringLiteral("devKeyboardController"), m_keyboard);
        ctx->setContextProperty(QStringLiteral("devToolsManager"), m_tools);
        ctx->setContextProperty(QStringLiteral("devMode"), true);
    }

    if (m_profile) {
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
        QDir logs(m_app.projectRoot.filePath(QStringLiteral("dev/perf/logs")));
        logs.mkpath(QStringLiteral("."));
        m_perf = new PerfMonitor(logs.filePath(QStringLiteral("perf_%1.csv").arg(stamp)),
                                 logs.filePath(QStringLiteral("perf_%1.log").arg(stamp)), this);
        ctx->setContextProperty(QStringLiteral("perfMonitor"), m_perf);
    }
}

void Runtime::afterLoad(CommandServer *server)
{
    const auto roots = m_app.engine->rootObjects();
    auto *window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());

    if (m_dev && window) {
        // Main.qml knows nothing about the overlay; it is reparented into the
        // root window here
        const QString path = m_app.projectRoot.filePath(QStringLiteral("src/dev/qml/DevOverlay.qml"));
        QQmlComponent component(m_app.engine, QUrl::fromLocalFile(path));
        QObject *obj = component.isError() ? nullptr : component.create(m_app.engine->rootContext());
        if (auto *overlay = qobject_cast<QQuickItem *>(obj)) {
            // C++ ownership plus a QObject parent, or the JS GC reclaims it:
            // setParentItem() only sets the visual parent
            QQmlEngine::setObjectOwnership(overlay, QQmlEngine::CppOwnership);
            overlay->setParent(window);
            overlay->setParentItem(window->contentItem());
            m_overlay = overlay;
            qCInfo(lcDev) << "DevOverlay attached to root window";
        } else {
            delete obj;
            for (const QQmlError &e : component.errors())
                qCWarning(lcDev).noquote() << "DevOverlay error:" << e.toString();
        }
        m_tools->setWindow(window);
    }

    if (m_profile)
        m_perf->start();
    // --profile turns the server on in these builds (serviceoptions.h); the
    // UI / perf / sim actions ride on it only then, never on a plain --service
    if (m_profile && server) {
        m_server = server;
        m_ui = new UiDriver(m_app.engine, this);
        registerCoreActions(*m_server, {m_ui, m_perf, m_app.settings, m_app.media, m_app.volume, m_app.nowPlaying, m_app.obd});
        m_collage = new CollageBridge(m_app.engine, m_app.projectRoot, this);
        m_collage->registerActions(*m_server);
        if (m_dev)
            registerSimActions();
    }
}

void Runtime::startHeadless(CommandServer *server)
{
    if (m_profile)
        qCWarning(lcDev) << "--profile needs the QML UI; ignored with --headless (--dev still runs the simulated car)";
    if (m_dev && server) {
        m_server = server;
        registerSimActions();
    }
}

void Runtime::shutdown()
{
    if (m_perf)
        m_perf->stop();
    if (m_ui)
        m_ui->recordStop();
}

void Runtime::registerSimActions()
{
    QPointer<Simulator> sim = m_app.sim;

    // Fire-and-forget on the GUI thread
    auto queued = [this, sim](const QString &name, std::function<void(Simulator *, const QVariantMap &)> apply) {
        m_server->registerAction(name, [sim, name, apply](const QVariantMap &args) -> QVariant {
            if (!sim || !sim->active())
                throw CommandError(QStringLiteral("no simulated car in this run"));
            CommandServer::postGui([sim, args, apply] { if (sim) apply(sim, args); });
            return QVariantMap{{QStringLiteral("queued"), name}};
        });
    };
    // The value is the first of these keys present, in this order
    auto value = [](const QVariantMap &a, double def = 0) {
        for (const char *k : {"value", "mph", "rpm", "gear", "intensity"}) {
            if (a.contains(QLatin1String(k)))
                return a.value(QLatin1String(k)).toDouble();
        }
        return def;
    };
    auto on = [](const QVariantMap &a, const char *key = "on") { return a.value(QLatin1String(key), true).toBool(); };
    using S = Simulator;

    queued("sim_throttle", [value](S *s, const QVariantMap &a) { s->setThrottle(value(a)); });
    queued("sim_brake", [value](S *s, const QVariantMap &a) { s->setBrake(value(a)); });
    queued("sim_rev", [value](S *s, const QVariantMap &a) { s->rev(value(a)); });
    queued("sim_engine", [](S *s, const QVariantMap &a) {
        if (!a.contains(QStringLiteral("on")))
            s->toggleEngine();
        else
            s->setEngineOn(a.value(QStringLiteral("on")).toBool());
    });
    queued("sim_gear", [value](S *s, const QVariantMap &a) { s->setGear(int(value(a))); });
    queued("sim_upshift", [](S *s, const QVariantMap &) { s->upshift(); });
    queued("sim_downshift", [](S *s, const QVariantMap &) { s->downshift(); });
    queued("sim_speed", [value](S *s, const QVariantMap &a) { s->setSpeed(value(a)); });
    queued("sim_rpm", [value](S *s, const QVariantMap &a) { s->setRpm(value(a)); });
    queued("sim_realistic", [](S *s, const QVariantMap &a) {
        s->setRealistic(a.value(QStringLiteral("enabled"), a.value(QStringLiteral("on"), true)).toBool());
    });
    queued("sim_scenario", [](S *s, const QVariantMap &a) {
        s->scenario(a.value(QStringLiteral("name"), QStringLiteral("normal")).toString());
    });
    // Link-fault knobs
    queued("sim_dropouts", [on](S *s, const QVariantMap &a) { s->setDropouts(on(a)); });
    queued("sim_dropout_now", [](S *s, const QVariantMap &) { s->dropoutNow(); });
    queued("sim_failure_rate", [value](S *s, const QVariantMap &a) { s->setFailureRate(value(a, 0.02)); });
    queued("sim_link", [on](S *s, const QVariantMap &a) { s->setLinkUp(on(a, "up")); });
    queued("sim_imu", [on](S *s, const QVariantMap &a) { s->setImu(on(a)); });
    queued("sim_video", [on](S *s, const QVariantMap &a) { s->setVideo(on(a)); });
    m_server->registerAction("sim_pairing_code", [sim](const QVariantMap &) {
        return CommandServer::onGui([sim]() -> QVariant {
            const QVariantMap code = sim ? sim->pairingCode() : QVariantMap();
            if (code.isEmpty())
                throw CommandError(QStringLiteral("no pairing in this run"));
            return code;
        });
    });
    m_server->registerAction("sim_status", [sim](const QVariantMap &) {
        return CommandServer::onGui([sim] { return QVariant(sim ? sim->status() : QVariantMap()); });
    });
    std::fprintf(stdout, "[DEV] Simulator actions on the command server (aliases for the sim object): sim_throttle, "
                         "sim_brake, sim_rev, sim_engine, sim_gear, sim_upshift, sim_downshift, sim_speed, sim_rpm, "
                         "sim_scenario, sim_realistic, sim_dropouts, sim_dropout_now, sim_failure_rate, sim_link, "
                         "sim_status, sim_imu, sim_video, sim_pairing_code\n");
    std::fflush(stdout);
}

} // namespace OctaveDev
