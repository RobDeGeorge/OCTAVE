#ifndef OCTAVE_DEV_DEVRUNTIME_H
#define OCTAVE_DEV_DEVRUNTIME_H

// Developer tooling for the C++ binary, compiled in only when the
// OCTAVE_DEV_TOOLS CMake option is on (default for Debug desktop builds, off
// for Release, Android and iOS) and inactive unless asked for:
//
//   octave --dev               the simulator (--sim, src/sim/, which main.cpp
//                              starts) driven from the keyboard, dev overlay
//                              + tools panel
//   octave --profile           perf monitor + the UI / perf actions on the
//                              command server, which --profile starts in
//                              these builds (127.0.0.1:9876; main.cpp owns
//                              it, src/service/serviceoptions.h)
//   octave --dev --profile     both, plus the sim_* actions (what the
//                              screenshot stories and MCP tools launch)
//   octave --headless --dev    the sim_* actions on the service with no QML
//                              engine (startHeadless)
// The sim_* actions are aliases for the `sim` object's methods
// (src/sim/simulator.h), which every build serves under --sim.
//   --command-port N           listen somewhere other than 9876
//
// See wiki/development.html.

#include <QDir>
#include <QObject>
#include <QPointer>
#include <QStringList>

class OBDManager;
class QQmlApplicationEngine;
class QQuickItem;

class Simulator;
namespace OctaveDev {
class CollageBridge;
}
namespace OctaveService { class CommandServer; }
namespace OctaveDev {
using OctaveService::CommandServer;
class DevKeyboardController;
class DevToolsManager;
class PerfMonitor;
class UiDriver;

struct AppObjects {
    QQmlApplicationEngine *engine = nullptr;
    QObject *settings = nullptr;
    QObject *media = nullptr;
    QObject *volume = nullptr;   // volumeController
    QObject *nowPlaying = nullptr;
    OBDManager *obd = nullptr;
    QDir projectRoot;   // checkout root: frontend/, src/dev/qml/, dev/perf/logs/
    Simulator *sim = nullptr;   // started by main.cpp before beforeLoad() under --dev
};

class Runtime : public QObject
{
    Q_OBJECT

public:
    Runtime(const QStringList &arguments, const AppObjects &app, QObject *parent = nullptr);
    ~Runtime() override;

    bool devMode() const { return m_dev; }
    bool profiling() const { return m_profile; }

    // Before engine.load(): context properties, the simulated adapter
    void beforeLoad();
    // After Main.qml loaded: the overlay, the perf monitor, and (with
    // --profile) its actions on `server`, before main.cpp starts it
    void afterLoad(CommandServer *server);
    // --headless (no engine, `m_app.engine` unused): with --dev the sim_*
    // actions on `server`, before main.cpp starts it. --profile needs the
    // UI and is ignored.
    void startHeadless(CommandServer *server);
    // From aboutToQuit, before the managers clean up
    void shutdown();

private:
    void registerSimActions();

    AppObjects m_app;
    bool m_dev = false;
    bool m_profile = false;

    DevKeyboardController *m_keyboard = nullptr;
    DevToolsManager *m_tools = nullptr;
    PerfMonitor *m_perf = nullptr;
    UiDriver *m_ui = nullptr;
    CollageBridge *m_collage = nullptr;
    CommandServer *m_server = nullptr;   // main.cpp's
    QPointer<QQuickItem> m_overlay;
};

} // namespace OctaveDev

#endif // OCTAVE_DEV_DEVRUNTIME_H
