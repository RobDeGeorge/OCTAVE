#include "devruntime.h"

#include "carsim.h"
#include "collagebridge.h"
#include "commandserver.h"
#include "coreactions.h"
#include "devimufeed.h"
#include "devtoolsmanager.h"
#include "perfmonitor.h"
#include "simelm327.h"
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
        else if (a == QLatin1String("--command-port") && i + 1 < arguments.size())
            m_port = static_cast<quint16>(arguments.at(++i).toUInt());
        else if (a.startsWith(QLatin1String("--command-port=")))
            m_port = static_cast<quint16>(a.section(QLatin1Char('='), 1).toUInt());
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

        // The real OBDManager talks to a simulated ELM327: the protocol
        // parsing, demand-driven polling, vehicle scan and VIN decode all run
        // as they would in the car. The saved port setting is left alone.
        m_sim = new CarSim(this);
        const std::shared_ptr<CarState> state = m_sim->state();
        OBDConnectionWorker::setCustomTransport(
            [](const QString &port) { return port == QLatin1String(kSimPort); },
            [state](const QString &, QObject *parent) -> QIODevice * { return new SimElm327Device(state, parent); });
        if (m_app.obd)
            m_app.obd->setPortOverride(QString::fromLatin1(kSimPort));

        m_keyboard = new DevKeyboardController(m_sim, this);
        m_tools = new DevToolsManager(m_app.projectRoot, this);
        ctx->setContextProperty(QStringLiteral("devKeyboardController"), m_keyboard);
        ctx->setContextProperty(QStringLiteral("devToolsManager"), m_tools);
        ctx->setContextProperty(QStringLiteral("devMode"), true);
        if (m_app.imu)
            m_imuFeed = new DevImuFeed(m_app.imu, m_sim, this);
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

void Runtime::afterLoad()
{
    const auto roots = m_app.engine->rootObjects();
    auto *window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());

    if (m_dev && window) {
        // Main.qml knows nothing about the overlay; it is reparented into the
        // root window here, as dev/dev_runtime.py does for Python
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

    if (m_profile) {
        m_perf->start();
        m_ui = new UiDriver(m_app.engine, this);
        m_server = new CommandServer(m_port, this);
        registerCoreActions(*m_server, {m_ui, m_perf, m_app.settings, m_app.media, m_app.obd});
        m_collage = new CollageBridge(m_app.engine, m_app.projectRoot, this);
        m_collage->registerActions(*m_server);
        if (m_dev)
            registerSimActions();
        m_server->start();
    }
}

void Runtime::shutdown()
{
    if (m_server)
        m_server->stop();
    if (m_perf)
        m_perf->stop();
    if (m_ui)
        m_ui->recordStop();
}

void Runtime::registerSimActions()
{
    QPointer<CarSim> sim = m_sim;
    QPointer<DevImuFeed> imu = m_imuFeed;

    // Fire-and-forget on the GUI thread, as Python's SimBridge does
    auto queued = [this, sim](const QString &name, std::function<void(CarSim *, const QVariantMap &)> apply) {
        m_server->registerAction(name, [sim, name, apply](const QVariantMap &args) -> QVariant {
            CommandServer::postGui([sim, args, apply] { if (sim) apply(sim, args); });
            return QVariantMap{{QStringLiteral("queued"), name}};
        });
    };
    // Python reads one of these keys, in this order, as the value
    auto value = [](const QVariantMap &a, double def = 0) {
        for (const char *k : {"value", "mph", "rpm", "gear", "intensity"}) {
            if (a.contains(QLatin1String(k)))
                return a.value(QLatin1String(k)).toDouble();
        }
        return def;
    };

    queued("sim_throttle", [value](CarSim *s, const QVariantMap &a) { s->setThrottle(value(a)); });
    queued("sim_brake", [value](CarSim *s, const QVariantMap &a) { s->setBrake(value(a)); });
    queued("sim_rev", [value](CarSim *s, const QVariantMap &a) {
        const double v = value(a);
        s->revEngine(v ? v : 50);
    });
    queued("sim_engine", [](CarSim *s, const QVariantMap &a) {
        if (!a.contains(QStringLiteral("on")))
            s->toggleEngine();
        else
            s->setEngineOn(a.value(QStringLiteral("on")).toBool());
    });
    queued("sim_gear", [value](CarSim *s, const QVariantMap &a) { s->setGear(int(value(a))); });
    queued("sim_upshift", [](CarSim *s, const QVariantMap &) { s->upshift(); });
    queued("sim_downshift", [](CarSim *s, const QVariantMap &) { s->downshift(); });
    queued("sim_speed", [value](CarSim *s, const QVariantMap &a) { s->setSpeed(value(a)); });
    queued("sim_rpm", [value](CarSim *s, const QVariantMap &a) { s->setRpm(value(a)); });
    queued("sim_realistic", [](CarSim *s, const QVariantMap &a) {
        s->setRealisticMode(a.value(QStringLiteral("enabled"), a.value(QStringLiteral("on"), true)).toBool());
    });
    queued("sim_scenario", [](CarSim *s, const QVariantMap &a) {
        const QString name = a.value(QStringLiteral("name"), QStringLiteral("normal")).toString();
        if (name == QLatin1String("cold"))
            s->simulateColdStart();
        else if (name == QLatin1String("hot"))
            s->simulateOverheating();
        else if (name == QLatin1String("low_fuel"))
            s->simulateLowFuel();
        else
            s->resetToNormal();
    });
    // C++ additions: the link-fault knobs the Python mock only had as slots
    queued("sim_dropouts", [](CarSim *s, const QVariantMap &a) {
        s->setSimulateDropouts(a.value(QStringLiteral("on"), true).toBool());
    });
    queued("sim_dropout_now", [](CarSim *s, const QVariantMap &) { s->triggerDropoutNow(); });
    queued("sim_failure_rate", [value](CarSim *s, const QVariantMap &a) { s->setFailureRate(value(a, 0.02)); });
    queued("sim_link", [](CarSim *s, const QVariantMap &a) { s->setLinkUp(a.value(QStringLiteral("up"), true).toBool()); });

    m_server->registerAction("sim_imu", [imu](const QVariantMap &a) -> QVariant {
        if (!imu)
            throw CommandError(QStringLiteral("no berryIMU manager to drive"));
        const bool on = a.value(QStringLiteral("on"), true).toBool();
        CommandServer::postGui([imu, on] { if (imu) imu->setEnabled(on); });
        return QVariantMap{{QStringLiteral("queued"), QStringLiteral("sim_imu")}};
    });
    m_server->registerAction("sim_status", [sim, imu](const QVariantMap &) {
        return CommandServer::onGui([sim, imu] {
            QVariantMap st = sim ? sim->status() : QVariantMap();
            if (imu)
                st.insert(QStringLiteral("imu"), imu->status());
            return QVariant(st);
        });
    });
    std::fprintf(stdout, "[DEV] Simulator actions on the command server: sim_throttle, sim_brake, sim_rev, sim_engine, "
                         "sim_gear, sim_upshift, sim_downshift, sim_speed, sim_rpm, sim_scenario, sim_realistic, "
                         "sim_dropouts, sim_dropout_now, sim_failure_rate, sim_link, sim_status, sim_imu\n");
    std::fflush(stdout);
}

} // namespace OctaveDev
