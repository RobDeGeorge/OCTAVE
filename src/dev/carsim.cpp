#include "carsim.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <limits>

Q_LOGGING_CATEGORY(lcCarSim, "octave.dev.carsim")

namespace OctaveDev {

namespace {
double uniform(double lo, double hi)
{
    return lo + (hi - lo) * QRandomGenerator::global()->generateDouble();
}
} // namespace

bool CarState::linkDown() const
{
    QMutexLocker l(&m_lock);
    return m_link.downUntilMs > QDateTime::currentMSecsSinceEpoch();
}

CarSim::CarSim(QObject *parent)
    : QObject(parent)
    , m_state(std::make_shared<CarState>())
{
    // 60 Hz physics
    m_physics.setInterval(16);
    connect(&m_physics, &QTimer::timeout, this, &CarSim::step);
    m_physics.start();

    m_revRelease.setSingleShot(true);
    m_revRelease.setInterval(400);
    connect(&m_revRelease, &QTimer::timeout, this, [this] { setThrottle(0); });

    m_dropout.setSingleShot(true);
    connect(&m_dropout, &QTimer::timeout, this, &CarSim::triggerDropoutNow);
}

void CarSim::step()
{
    constexpr double dt = 0.016;
    m_state->updateValues([this](CarValues &v) {
        if (!v.engineOn) {
            v.rpm = std::max(0.0, v.rpm - 100);
            v.speedMph = std::max(0.0, v.speedMph - 1);
            v.runTimeS = 0;
            v.voltage = 12.4 + uniform(-0.05, 0.05);
            return;
        }
        v.runTimeS += dt;

        if (v.throttle > 0) {
            const double targetRpm = 800 + (v.throttle / 100) * 5500;
            v.rpm += (targetRpm - v.rpm) * 0.1;
            v.speedMph = std::min(140.0, v.speedMph + (v.throttle / 100) * 15 * dt);
        } else {
            v.rpm += (800 - v.rpm) * 0.05;
            v.speedMph = std::max(0.0, v.speedMph - 3 * dt);
        }
        if (v.brake > 0) {
            v.speedMph = std::max(0.0, v.speedMph - (v.brake / 100) * 40 * dt);
            if (v.speedMph < 5)
                v.rpm += (800 - v.rpm) * 0.1;
        }

        if (v.speedMph < 15) v.gear = 1;
        else if (v.speedMph < 30) v.gear = 2;
        else if (v.speedMph < 45) v.gear = 3;
        else if (v.speedMph < 65) v.gear = 4;
        else if (v.speedMph < 90) v.gear = 5;
        else v.gear = 6;

        if (v.speedMph > 5 && v.throttle < 10) {
            const double speedRpm = 800 + (v.speedMph / 140) * 4000;
            v.rpm += (speedRpm - v.rpm) * 0.02;
        }
        v.rpm = std::clamp(v.rpm, 0.0, 7000.0);

        v.engineLoad = 15 + (v.throttle / 100) * 70;
        v.maf = 8 + (v.rpm / 1000) * 10 + (v.throttle / 100) * 30;
        v.map = 35 + (v.throttle / 100) * 60;
        v.timing = 12 + (v.rpm / 1000) * 2 - (v.engineLoad / 100) * 5;
        v.afr = v.throttle > 70 ? 12.5 + uniform(-0.2, 0.2) : 14.7 + uniform(-0.3, 0.3);
        v.o2 = 0.45 + std::sin(v.rpm / 400) * 0.3;
        v.coolant = m_coolantBase + uniform(-0.5, 0.5);
        v.oil = m_oilBase + uniform(-0.5, 0.5);
        v.voltage = 14.2 + uniform(-0.1, 0.1);
        v.odometerKm += v.speedMph * 1.609344 * dt / 3600.0;
    });
}

void CarSim::setThrottle(double value)
{
    m_state->updateValues([value](CarValues &v) { v.throttle = std::clamp(value, 0.0, 100.0); });
}

void CarSim::setBrake(double value)
{
    m_state->updateValues([value](CarValues &v) { v.brake = std::clamp(value, 0.0, 100.0); });
}

void CarSim::revEngine(double intensity)
{
    setThrottle(intensity);
    m_revRelease.start();
}

void CarSim::toggleEngine()
{
    setEngineOn(!m_state->values().engineOn);
}

void CarSim::setEngineOn(bool on)
{
    m_state->updateValues([on](CarValues &v) {
        if (on && !v.engineOn)
            v.rpm = 800;
        v.engineOn = on;
    });
    qCInfo(lcCarSim) << (on ? "engine on" : "engine off");
}

void CarSim::setGear(int gear)
{
    m_state->updateValues([gear](CarValues &v) { v.gear = std::clamp(gear, 1, 6); });
}

void CarSim::upshift()
{
    m_state->updateValues([](CarValues &v) { v.gear = std::min(6, v.gear + 1); });
}

void CarSim::downshift()
{
    m_state->updateValues([](CarValues &v) { v.gear = std::max(1, v.gear - 1); });
}

void CarSim::setSpeed(double mph)
{
    m_state->updateValues([mph](CarValues &v) { v.speedMph = std::clamp(mph, 0.0, 140.0); });
}

void CarSim::setRpm(double rpm)
{
    m_state->updateValues([rpm](CarValues &v) { v.rpm = std::clamp(rpm, 0.0, 7000.0); });
}

void CarSim::simulateColdStart()
{
    m_coolantBase = m_oilBase = 20;
    m_state->updateValues([](CarValues &v) { v.coolant = v.oil = 20; v.rpm = 1100; });
    qCInfo(lcCarSim) << "cold start";
}

void CarSim::simulateOverheating()
{
    m_coolantBase = 115;
    m_oilBase = 130;
    m_state->updateValues([](CarValues &v) { v.coolant = 115; v.oil = 130; });
    qCInfo(lcCarSim) << "overheating";
}

void CarSim::simulateLowFuel()
{
    m_state->updateValues([](CarValues &v) { v.fuelLevel = 5; });
    qCInfo(lcCarSim) << "low fuel";
}

void CarSim::resetToNormal()
{
    m_coolantBase = 90;
    m_oilBase = 95;
    m_state->updateValues([](CarValues &v) {
        v.coolant = 90;
        v.oil = 95;
        v.rpm = 800;
        v.speedMph = 0;
        v.fuelLevel = 72;
        v.engineOn = true;
        v.throttle = 0;
        v.brake = 0;
    });
    qCInfo(lcCarSim) << "reset";
}

void CarSim::setRealisticMode(bool enabled)
{
    m_state->updateLink([enabled](LinkSettings &l) { l.realistic = enabled; });
    qCInfo(lcCarSim) << "realistic link" << enabled;
}

void CarSim::setFailureRate(double rate)
{
    m_state->updateLink([rate](LinkSettings &l) { l.failureRate = std::clamp(rate, 0.0, 1.0); });
}

void CarSim::setSimulateDropouts(bool enabled)
{
    m_dropouts = enabled;
    if (enabled)
        scheduleDropout();
    else
        m_dropout.stop();
}

void CarSim::scheduleDropout()
{
    if (m_dropouts)
        m_dropout.start(static_cast<int>(uniform(90000, 180000)));
}

void CarSim::triggerDropoutNow()
{
    const qint64 until = QDateTime::currentMSecsSinceEpoch() + static_cast<qint64>(uniform(2000, 4000));
    m_state->updateLink([until](LinkSettings &l) { l.downUntilMs = until; });
    qCInfo(lcCarSim) << "simulated Bluetooth dropout";
    scheduleDropout();
}

void CarSim::setLinkUp(bool up)
{
    const qint64 until = up ? 0 : std::numeric_limits<qint64>::max();
    m_state->updateLink([until](LinkSettings &l) { l.downUntilMs = until; });
    qCInfo(lcCarSim) << (up ? "adapter plugged in" : "adapter unplugged");
}

QVariantMap CarSim::status() const
{
    const CarValues v = m_state->values();
    const LinkSettings l = m_state->link();
    return {
        {QStringLiteral("_speed"), v.speedMph},
        {QStringLiteral("_rpm"), v.rpm},
        {QStringLiteral("_gear"), v.gear},
        {QStringLiteral("_engine_on"), v.engineOn},
        {QStringLiteral("_throttle"), v.throttle},
        {QStringLiteral("_brake"), v.brake},
        {QStringLiteral("realistic"), l.realistic},
        {QStringLiteral("link_down"), m_state->linkDown()},
    };
}

// ---------------------------------------------------------------------------

void DevKeyboardController::handle_key(int key, bool pressed)
{
    if (pressed) {
        m_held.insert(key);
        onPressed(key);
    } else {
        m_held.remove(key);
    }
    // Only the held keys drive throttle/brake; re-applying them on every key
    // would cancel Space's rev straight away.
    static const QSet<int> continuous{Qt::Key_W, Qt::Key_Up, Qt::Key_S, Qt::Key_Down};
    if (continuous.contains(key))
        updateContinuous();
}

void DevKeyboardController::onPressed(int key)
{
    switch (key) {
    case Qt::Key_Space: m_sim->revEngine(70); break;
    case Qt::Key_E: m_sim->toggleEngine(); break;
    case Qt::Key_Q: m_sim->downshift(); break;
    case Qt::Key_R: m_sim->upshift(); break;
    case Qt::Key_F1: m_sim->simulateColdStart(); break;
    case Qt::Key_F2: m_sim->simulateOverheating(); break;
    case Qt::Key_F3: m_sim->simulateLowFuel(); break;
    case Qt::Key_F4: m_sim->resetToNormal(); break;
    default:
        if (key >= Qt::Key_1 && key <= Qt::Key_6)
            m_sim->setGear(key - Qt::Key_0);
        break;
    }
}

void DevKeyboardController::updateContinuous()
{
    m_sim->setThrottle(m_held.contains(Qt::Key_W) || m_held.contains(Qt::Key_Up) ? 100 : 0);
    m_sim->setBrake(m_held.contains(Qt::Key_S) || m_held.contains(Qt::Key_Down) ? 100 : 0);
}

} // namespace OctaveDev
