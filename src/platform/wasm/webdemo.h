#ifndef OCTAVE_WASM_WEBDEMO_H
#define OCTAVE_WASM_WEBDEMO_H

// The browser demo's car (WebAssembly build only). The page opens with no
// car connected; the adapter sheet offers a Bluetooth LE adapter over Web
// Bluetooth (webbluetooth.h) and "Simulated car", which calls start(): the
// real OBDManager is pointed at the simulated car of `--sim`
// (src/sim/simulator.h, AppCore's `sim`) and an autopilot drives it round a
// loop: pull away, run up through the gears to a road speed (35-70 mph),
// cruise there, coast, brake to a stop, idle with the odd blip of the
// throttle. The simulator's IMU feed gives the matching motion. Picking a
// real adapter later stops both. Built from main.cpp before Main.qml loads.

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
    // Nothing runs until start()
    WebDemo(Simulator *sim, OBDManager *obd, BerryIMUManager *imu, QObject *parent = nullptr);
    ~WebDemo() override;

    static WebDemo *instance();
    // Connects OBDManager to the simulated car and drives it (again, after
    // a real adapter took over)
    void start();

private slots:
    void drive();

private:
    enum class Phase { Idle, PullAway, Cruise, Coast, Brake };
    void enter(Phase phase);

    Simulator *m_simulator = nullptr;
    OBDManager *m_obd = nullptr;
    BerryIMUManager *m_imu = nullptr;
    OctaveSim::CarSim *m_sim = nullptr;   // once started
    QTimer m_autopilot;
    QElapsedTimer m_inPhase;
    Phase m_phase = Phase::Idle;
    int m_phaseMs = 0;
    double m_target = 0;   // throttle (PullAway) or brake (Brake)
    double m_cruiseMph = 0;  // this lap's road speed, held through Cruise
};

#endif
