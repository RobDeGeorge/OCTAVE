#ifndef OCTAVE_WASM_WEBDEMO_H
#define OCTAVE_WASM_WEBDEMO_H

// The browser demo's car (WebAssembly build only). There is no adapter in a
// browser, so the real OBDManager is pointed at the simulated car of
// `--sim` (src/sim/simulator.h, AppCore's `sim`) and an autopilot drives it
// round a loop: pull away, run up through the gears to
// a road speed (35-70 mph), cruise there, coast, brake to a stop, idle with the odd blip of the throttle. The
// simulator's IMU feed gives the matching motion. A Bluetooth LE adapter
// picked over Web Bluetooth (webbluetooth.h) replaces the demo car. Built
// from main.cpp before Main.qml loads.

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class OBDManager;
class BerryIMUManager;
class Simulator;
namespace OctaveSim {
class CarSim;
}

class WebDemo : public QObject
{
    Q_OBJECT

public:
    // Starts `sim` on `obd` / `imu` and drives it
    WebDemo(Simulator *sim, OBDManager *obd, BerryIMUManager *imu, QObject *parent = nullptr);

private slots:
    void drive();

private:
    enum class Phase { Idle, PullAway, Cruise, Coast, Brake };
    void enter(Phase phase);

    OctaveSim::CarSim *m_sim = nullptr;
    QTimer m_autopilot;
    QElapsedTimer m_inPhase;
    Phase m_phase = Phase::Idle;
    int m_phaseMs = 0;
    double m_target = 0;   // throttle (PullAway) or brake (Brake)
    double m_cruiseMph = 0;  // this lap's road speed, held through Cruise
};

#endif
