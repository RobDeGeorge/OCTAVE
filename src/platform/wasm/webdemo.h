#ifndef OCTAVE_WASM_WEBDEMO_H
#define OCTAVE_WASM_WEBDEMO_H

// The browser demo's car (WebAssembly build only). There is no adapter in a
// browser, so the real OBDManager is pointed at the simulated ELM327 from the
// dev tooling (src/dev/carsim + simelm327, the same pair `--dev` uses) and an
// autopilot drives it round a loop: pull away, run up through the gears to
// a road speed (35-70 mph), cruise there, coast, brake to a stop, idle with the odd blip of the throttle. The
// IMU gets the matching motion from DevImuFeed. Built from main.cpp before
// Main.qml loads; owns everything it makes.

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class OBDManager;
class BerryIMUManager;
namespace OctaveDev {
class CarSim;
class DevImuFeed;
}

class WebDemo : public QObject
{
    Q_OBJECT

public:
    WebDemo(OBDManager *obd, BerryIMUManager *imu, QObject *parent = nullptr);

private slots:
    void drive();

private:
    enum class Phase { Idle, PullAway, Cruise, Coast, Brake };
    void enter(Phase phase);

    OctaveDev::CarSim *m_sim = nullptr;
    OctaveDev::DevImuFeed *m_imuFeed = nullptr;
    QTimer m_autopilot;
    QElapsedTimer m_inPhase;
    Phase m_phase = Phase::Idle;
    int m_phaseMs = 0;
    double m_target = 0;   // throttle (PullAway) or brake (Brake)
    double m_cruiseMph = 0;  // this lap's road speed, held through Cruise
};

#endif
