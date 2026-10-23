#ifndef OCTAVE_DEV_DEVRUNTIME_H
#define OCTAVE_DEV_DEVRUNTIME_H

// Developer tooling for the C++ binary, compiled in only when the
// OCTAVE_DEV_TOOLS CMake option is on (default for Debug desktop builds, off
// for Release, Android and iOS) and inactive unless asked for:
//
//   octave --dev               simulated car (OBD over a fake ELM327, keyboard
//                              driving), dev overlay + tools panel
//   octave --profile           perf monitor + the UI / perf actions on the
//                              command server, which --profile starts in
//                              these builds (127.0.0.1:9876; main.cpp owns
//                              it, src/service/serviceoptions.h)
//   octave --dev --profile     both, plus the sim_* actions (what the
//                              screenshot stories and MCP tools launch)
//   octave --headless --dev    the simulated car (and IMU motion) with no QML
//                              engine; the sim_* actions on the service, for
//                              developing another frontend (startHeadless)
//   --command-port N           listen somewhere other than 9876
//
// See wiki/development.html.

#include <QDir>
#include <QObject>
#include <QPointer>
#include <QStringList>

class BerryIMUManager;
class ServicePairing;
class OBDManager;
class QQmlApplicationEngine;
class QQuickItem;
class QTimer;

namespace OctaveDev {

class CarSim;
class CollageBridge;
}
namespace OctaveService { class CommandServer; }
namespace OctaveDev {
using OctaveService::CommandServer;
class DevImuFeed;
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
    BerryIMUManager *imu = nullptr;
    QDir projectRoot;   // checkout root: frontend/, src/dev/qml/, dev/perf/logs/
    ServicePairing *pairing = nullptr;   // sim_pairing_code: a code without the Settings page
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
    // --headless (no engine, `m_app.engine` unused): with --dev the
    // simulated car, the IMU feed on, and the sim_* actions on `server`,
    // before main.cpp starts it. --profile needs the UI and is ignored.
    void startHeadless(CommandServer *server);
    // From aboutToQuit, before the managers clean up
    void shutdown();

private:
    void installSim();
    void registerSimActions();
    // sim_video: tests/data/stream-test.h264 looped into the
    // phoneMirrorManager.video stream at 30 fps, as the phone would send it
    void setVideoSim(bool on);
    void videoSimTick();

    AppObjects m_app;
    bool m_dev = false;
    bool m_profile = false;

    CarSim *m_sim = nullptr;
    DevKeyboardController *m_keyboard = nullptr;
    DevToolsManager *m_tools = nullptr;
    DevImuFeed *m_imuFeed = nullptr;
    PerfMonitor *m_perf = nullptr;
    UiDriver *m_ui = nullptr;
    CollageBridge *m_collage = nullptr;
    CommandServer *m_server = nullptr;   // main.cpp's
    QPointer<QQuickItem> m_overlay;
    QTimer *m_videoTimer = nullptr;
    QList<QByteArray> m_videoUnits;
    qint64 m_videoFrame = 0;
};

} // namespace OctaveDev

#endif // OCTAVE_DEV_DEVRUNTIME_H
