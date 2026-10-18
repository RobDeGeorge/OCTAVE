#include "devimufeed.h"

#include "carsim.h"
#include "../managers/berryimumanager.h"

#include <algorithm>
#include <cmath>

#ifdef Q_OS_MOBILE

// The QtSensors-backed mobile manager has no I2C worker to stand in for
namespace OctaveDev {
DevImuFeed::DevImuFeed(BerryIMUManager *imu, CarSim *sim, QObject *parent)
    : QObject(parent), m_imu(imu), m_sim(sim) {}
void DevImuFeed::setEnabled(bool) {}
QVariantMap DevImuFeed::status() const { return {{QStringLiteral("on"), false}}; }
void DevImuFeed::tick() {}
} // namespace OctaveDev

#else

// Friend of BerryIMUManager (see berryimumanager.h); global namespace so the
// friend declaration there matches.
class DevImuAccess
{
public:
    static void take(BerryIMUManager *imu)
    {
        imu->stopWorker();          // nothing else may emit while we do
        imu->m_shuttingDown = true; // makes startSensor() / scheduleRetry() no-ops
        imu->m_running = true;
        emit imu->connectionStatusChanged(QStringLiteral("Connected"));
    }
    static void release(BerryIMUManager *imu)
    {
        imu->m_running = false;
        imu->m_shuttingDown = false;
        imu->m_retryCount = 0;
        emit imu->connectionStatusChanged(QStringLiteral("Disconnected"));
        imu->startSensor();         // back to the real I2C path
    }
};

namespace OctaveDev {

namespace {
constexpr double kMphToMs = 0.44704;
constexpr double kG = 9.81;
constexpr double kPi = 3.14159265358979323846;
double deg(double rad) { return rad * 180.0 / kPi; }
double rad(double deg) { return deg * kPi / 180.0; }
} // namespace

DevImuFeed::DevImuFeed(BerryIMUManager *imu, CarSim *sim, QObject *parent)
    : QObject(parent)
    , m_imu(imu)
    , m_sim(sim)
{
    m_timer.setInterval(1000 / kRateHz);
    connect(&m_timer, &QTimer::timeout, this, &DevImuFeed::tick);
}

void DevImuFeed::setEnabled(bool on)
{
    if (on && !running()) {
        ::DevImuAccess::take(m_imu);
        m_clock.start();
        m_last = 0;
        m_speedMs = m_sim->state()->values().speedMph * kMphToMs;
        m_timer.start();
    } else if (!on && running()) {
        m_timer.stop();
        ::DevImuAccess::release(m_imu);
    }
}

QVariantMap DevImuFeed::status() const
{
    auto r = [](double v, int places) { const double f = std::pow(10.0, places); return std::round(v * f) / f; };
    return {
        {QStringLiteral("on"), running()},
        {QStringLiteral("heading"), r(m_heading, 1)},
        {QStringLiteral("lateral_g"), r(m_latG, 3)},
        {QStringLiteral("longitudinal_g"), r(m_longG, 3)},
        {QStringLiteral("altitude"), r(m_altitude, 1)},
    };
}

void DevImuFeed::tick()
{
    const double t = m_clock.elapsed() / 1000.0;
    const double dt = std::max(1e-3, t - m_last);
    m_last = t;

    const double v = m_sim->state()->values().speedMph * kMphToMs;
    const double accel = (v - m_speedMs) / dt;
    m_speedMs = v;
    // Smooth the derivative: the simulator steps speed in chunks
    m_longG += (accel / kG - m_longG) * std::min(1.0, dt * 4.0);

    // Winding road: curvature swings left and right; parked means no turning
    const double curvature = 0.012 * std::sin(t * 0.45) + 0.006 * std::sin(t * 1.1 + 1.3);
    const double yawRate = curvature * v;
    m_heading = std::fmod(m_heading + deg(yawRate) * dt + 360.0, 360.0);
    m_latG += ((v * yawRate) / kG - m_latG) * std::min(1.0, dt * 6.0);

    const double grade = 0.05 * std::sin(t * 0.18);   // +-5 % hills
    m_altitude += v * grade * dt;

    const double pitch = deg(std::atan(grade)) - m_longG * 3.0;
    const double roll = -m_latG * 4.0 + 0.3 * std::sin(t * 7.0) * std::min(1.0, v / 10.0);   // body roll + road buzz

    // Tilt-only quaternion (yaw stripped, like the real manager emits)
    const double cp = std::cos(rad(pitch) / 2), sp = std::sin(rad(pitch) / 2);
    const double cr = std::cos(rad(roll) / 2), sr = std::sin(rad(roll) / 2);
    emit m_imu->orientationChanged(float(cr * cp), float(sr * cp), float(cr * sp), float(-sr * sp));
    emit m_imu->pitchChanged(float(pitch));
    emit m_imu->rollChanged(float(roll));
    emit m_imu->headingChanged(float(m_heading));
    emit m_imu->lateralGChanged(float(m_latG));
    emit m_imu->longitudinalGChanged(float(m_longG));
    emit m_imu->accelMagnitudeChanged(float(std::sqrt(1.0 + m_latG * m_latG + m_longG * m_longG)));
    if (static_cast<int>(t * kRateHz) % 6 == 0) {   // baro at ~5 Hz like hardware
        emit m_imu->altitudeChanged(float(m_altitude));
        emit m_imu->baroTempChanged(float(21.5 + 0.8 * std::sin(t * 0.05)));
    }
}

} // namespace OctaveDev

#endif // Q_OS_MOBILE
