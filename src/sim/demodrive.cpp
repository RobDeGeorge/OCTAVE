#include "demodrive.h"

#include "../managers/obdmanager.h"
#include "carsim.h"
#include "simelm327.h"
#include "simulator.h"

#include <QRandomGenerator>

#include <algorithm>

using namespace OctaveSim;

namespace {
constexpr int kTickMs = 100;

int between(int lo, int hi) { return QRandomGenerator::global()->bounded(lo, hi + 1); }
}

DemoDrive::DemoDrive(Simulator *sim, OBDManager *obd, BerryIMUManager *imu,
                     ServicePairing *pairing, QObject *parent)
    : QObject(parent), m_simulator(sim), m_pairing(pairing), m_obd(obd), m_imu(imu)
{
    connect(&m_autopilot, &QTimer::timeout, this, &DemoDrive::drive);
    connect(m_obd, &OBDManager::simulatedAdapterRequested, this, &DemoDrive::start);
    connect(m_obd, &OBDManager::simulatedChanged, this, [this] {
        if (!m_obd->isSimulated()) stop();
    });
}

DemoDrive::~DemoDrive()
{
    stop();
}

void DemoDrive::start()
{
    m_simulator->start(m_obd, m_imu, m_pairing, true);
    m_sim = m_simulator->car();
    // A new selection is always a working car, even after a dev tool has
    // unplugged it, stopped the engine or introduced faults.
    m_sim->resetToNormal();
    m_sim->setLinkUp(true);
    m_sim->setSimulateDropouts(false);
    m_sim->setRealisticMode(false);
    m_sim->setFailureRate(0);
    m_sim->setEngineOn(true);
    m_sim->setSpeed(0);
    m_sim->setGear(1);
    m_obd->setPortOverride(QString::fromLatin1(kSimPort));
    m_simulator->setImu(true);
    m_autopilot.start(kTickMs);
    enter(Phase::Idle);
    m_obd->force_connect();
}

void DemoDrive::stop()
{
    m_autopilot.stop();
    m_simulator->setImu(false);
    if (m_sim) {
        m_sim->setThrottle(0);
        m_sim->setBrake(0);
    }
}

void DemoDrive::enter(Phase phase)
{
    m_phase = phase;
    m_inPhase.start();
    switch (phase) {
    case Phase::Idle:     m_phaseMs = between(3000, 6000);   m_target = 0;                break;
    case Phase::PullAway: m_phaseMs = 30000;                 m_target = between(40, 70);
                          m_cruiseMph = between(35, 70);                              break;
    case Phase::Cruise:   m_phaseMs = between(10000, 20000); m_target = 0;                break;
    case Phase::Coast:    m_phaseMs = between(3000, 6000);   m_target = 0;                break;
    case Phase::Brake:    m_phaseMs = 30000;                 m_target = between(30, 60);  break;
    }
    m_sim->setThrottle(phase == Phase::PullAway ? m_target : 0);
    m_sim->setBrake(phase == Phase::Brake ? m_target : 0);
}

void DemoDrive::drive()
{
    const bool done = m_inPhase.elapsed() >= m_phaseMs;
    switch (m_phase) {
    case Phase::Idle:
        // The odd blip, so the tach is not dead while parked
        if (QRandomGenerator::global()->bounded(40) == 0)
            m_sim->revEngine(between(25, 70));
        if (done) enter(Phase::PullAway);
        break;
    case Phase::PullAway:
        if (m_sim->state()->values().speedMph >= m_cruiseMph || done) enter(Phase::Cruise);
        break;
    case Phase::Cruise: {
        // Hold the road speed: a little throttle per mph short of it
        const double shortBy = m_cruiseMph - m_sim->state()->values().speedMph;
        m_sim->setThrottle(std::clamp(15.0 + 4.0 * shortBy, 0.0, 60.0));
        if (done) enter(Phase::Coast);
        break;
    }
    case Phase::Coast:
        if (done) enter(Phase::Brake);
        break;
    case Phase::Brake:
        if (m_sim->state()->values().speedMph < 1.0 || done) {
            m_sim->setSpeed(0);
            enter(Phase::Idle);
        }
        break;
    }
}
