#include "webdemo.h"

#include "../../sim/carsim.h"
#include "../../sim/simulator.h"

#include <QRandomGenerator>

#include <algorithm>

using namespace OctaveSim;

namespace {
constexpr int kTickMs = 100;

int between(int lo, int hi) { return QRandomGenerator::global()->bounded(lo, hi + 1); }
}

WebDemo::WebDemo(Simulator *sim, OBDManager *obd, BerryIMUManager *imu, QObject *parent)
    : QObject(parent)
{
    sim->start(obd, imu, nullptr, true);
    m_sim = sim->car();
    // A browser has nothing to wait for on the link, so no Bluetooth-style
    // latency or NO DATA: the gauges move from the first frame
    m_sim->setRealisticMode(false);

    connect(&m_autopilot, &QTimer::timeout, this, &WebDemo::drive);
    m_autopilot.start(kTickMs);
    enter(Phase::Idle);
}

void WebDemo::enter(Phase phase)
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

void WebDemo::drive()
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
