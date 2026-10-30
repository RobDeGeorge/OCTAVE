#ifndef OCTAVE_SIM_DEMODRIVE_H
#define OCTAVE_SIM_DEMODRIVE_H

// The adapter picker's demo drive, shared by every build. Selecting
// sim://elm327 starts the real OBD protocol stack and a driving loop.
// Switching to a real adapter or closing OBD stops the automatic motion.

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class OBDManager;
class BerryIMUManager;
class Simulator;
class ServicePairing;
namespace OctaveSim {
class CarSim;
}

class DemoDrive : public QObject
{
    Q_OBJECT

public:
    // Nothing runs until start()
    DemoDrive(Simulator *sim, OBDManager *obd, BerryIMUManager *imu, ServicePairing *pairing, QObject *parent = nullptr);
    ~DemoDrive() override;

    // Connects OBDManager to the simulated car and drives it (again, after
    // a real adapter took over)
    void start();
    void stop();
    bool running() const { return m_autopilot.isActive(); }

private slots:
    void drive();

private:
    enum class Phase { Idle, PullAway, Cruise, Coast, Brake };
    void enter(Phase phase);

    Simulator *m_simulator = nullptr;
    ServicePairing *m_pairing = nullptr;
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
