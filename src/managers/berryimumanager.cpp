/*
 * BerryIMU v3 Manager for OCTAVE (C++ port)
 * Reads accelerometer, gyroscope, magnetometer, and barometer data
 * via I2C and emits signals to QML for the CarMenu 3D model and gauges.
 *
 * Uses Madgwick AHRS for sensor fusion — outputs a quaternion directly
 * to the 3D model, completely avoiding gimbal lock. Fusion is deliberately
 * accel+gyro only (6DOF); the magnetometer feeds the heading readout only.
 *
 * I2C sensors (Linux only):
 *   LSM6DSL  (accel/gyro)  at 0x6A
 *   LIS3MDL  (magnetometer) at 0x1C
 *   BMP388   (barometer)    at 0x77
 */

#include "berryimumanager.h"

#ifndef Q_OS_MOBILE

#include "settingsmanager.h"

#include <QVariant>

#include <QLoggingCategory>
#include <QtMath>
#include <QThread>
#include <cstring>
#include <cmath>
#include <algorithm>

#ifdef Q_OS_LINUX
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif

Q_LOGGING_CATEGORY(lcImu, "octave.berryimu")

// ==================== Constants ====================

static constexpr int I2C_BUS          = 2;
static constexpr int LSM6DSL_ADDR     = 0x6A;
static constexpr int LIS3MDL_ADDR     = 0x1C;
static constexpr int BMP388_ADDR      = 0x77;
static constexpr double SEA_LEVEL_PRESSURE = 1013.25;
static constexpr double MADGWICK_BETA = 0.03;
static constexpr double GYRO_DEADBAND = 0.4;
static constexpr double EMIT_INTERVAL = 0.016; // ~60Hz
static constexpr double IDLE_EMIT_INTERVAL = 0.2; // 5 Hz while no page shows the values

// Sensor sample interval — ~200Hz target. The read loop sleeps for whatever is
// left of this period after the I2C reads, so it never busy-spins. Measured on
// the Orange Pi 5 Plus: the three I2C block reads cost ~4.3ms per iteration,
// so the loop is bus-bound and settles at ~172Hz with ~8% CPU on the worker
// thread — that CPU is the cost of reading the sensor, not a bug. Do NOT set
// this below the read cost (~0.005 here): `remaining` goes negative, the sleep
// is skipped, and the loop spins. Raising it to 0.01 (100Hz) roughly halves
// bus traffic and CPU while still giving the Madgwick filter ~1.7 samples per
// 60Hz emit; below ~100Hz the filter starts to starve.
static constexpr double SAMPLE_INTERVAL = 0.005;

// Barometer read interval — ~5Hz, time-based so it doesn't drift with loop rate
static constexpr double BARO_INTERVAL = 0.2;

// Consecutive failed accel/gyro/mag block reads before the loop declares the
// bus dead, emits "Error" and hands off to the manager's retry/back-off path
// (the Python backend raises on the first OSError; a small threshold here
// rides out a single NACK without dropping the connection).
static constexpr int MAX_CONSECUTIVE_READ_FAILURES = 5;

// ==================== Madgwick AHRS ====================

MadgwickAHRS::MadgwickAHRS(double b)
    : q{1.0, 0.0, 0.0, 0.0}
    , beta(b)
{
}

void MadgwickAHRS::update(double gx, double gy, double gz,
                           double ax, double ay, double az,
                           double mx, double my, double mz,
                           double dt)
{
    double q0 = q[0], q1 = q[1], q2 = q[2], q3 = q[3];

    // Gyro quaternion rate of change
    double qd0 = 0.5 * (-q1 * gx - q2 * gy - q3 * gz);
    double qd1 = 0.5 * ( q0 * gx + q2 * gz - q3 * gy);
    double qd2 = 0.5 * ( q0 * gy - q1 * gz + q3 * gx);
    double qd3 = 0.5 * ( q0 * gz + q1 * gy - q2 * gx);

    // Normalize accelerometer
    double aNorm = std::sqrt(ax * ax + ay * ay + az * az);
    if (aNorm < 0.001) {
        q0 += qd0 * dt; q1 += qd1 * dt; q2 += qd2 * dt; q3 += qd3 * dt;
        double n = std::sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
        q[0] = q0/n; q[1] = q1/n; q[2] = q2/n; q[3] = q3/n;
        return;
    }
    ax /= aNorm; ay /= aNorm; az /= aNorm;

    // Normalize magnetometer
    double mNorm = std::sqrt(mx * mx + my * my + mz * mz);
    bool useMag = mNorm > 0.001;
    if (useMag) {
        mx /= mNorm; my /= mNorm; mz /= mNorm;
    }

    // Precompute products
    double q0q0 = q0*q0, q0q1 = q0*q1, q0q2 = q0*q2, q0q3 = q0*q3;
    double q1q1 = q1*q1, q1q2 = q1*q2, q1q3 = q1*q3;
    double q2q2 = q2*q2, q2q3 = q2*q3, q3q3 = q3*q3;

    double _2q0 = 2.0*q0, _2q1 = 2.0*q1, _2q2 = 2.0*q2, _2q3 = 2.0*q3;
    double _2q0q2 = 2.0*q0q2, _2q2q3 = 2.0*q2q3;

    double s0, s1, s2, s3;

    if (useMag) {
        double _2q0mx = _2q0*mx, _2q0my = _2q0*my, _2q0mz = _2q0*mz;
        double _2q1mx = _2q1*mx;

        double hx = (mx*q0q0 - _2q0my*q3 + _2q0mz*q2 + mx*q1q1
                     + _2q1*my*q2 + _2q1*mz*q3 - mx*q2q2 - mx*q3q3);
        double hy = (_2q0mx*q3 + my*q0q0 - _2q0mz*q1 + _2q1mx*q2
                     - my*q1q1 + my*q2q2 + _2q2*mz*q3 - my*q3q3);
        double _2bx = std::sqrt(hx*hx + hy*hy);
        double _2bz = (-_2q0mx*q2 + _2q0my*q1 + mz*q0q0 + _2q1mx*q3
                       - mz*q1q1 + _2q2*my*q3 - mz*q2q2 + mz*q3q3);
        double _4bx = 2.0*_2bx;
        double _4bz = 2.0*_2bz;

        s0 = (-_2q2*(2.0*q1q3 - _2q0q2 - ax)
              + _2q1*(2.0*q0q1 + _2q2q3 - ay)
              - _2bz*q2*(_2bx*(0.5 - q2q2 - q3q3) + _2bz*(q1q3 - q0q2) - mx)
              + (-_2bx*q3 + _2bz*q1)*(_2bx*(q1q2 - q0q3) + _2bz*(q0q1 + q2q3) - my)
              + _2bx*q2*(_2bx*(q0q2 + q1q3) + _2bz*(0.5 - q1q1 - q2q2) - mz));

        s1 = (_2q3*(2.0*q1q3 - _2q0q2 - ax)
              + _2q0*(2.0*q0q1 + _2q2q3 - ay)
              - 4.0*q1*(1.0 - 2.0*q1q1 - 2.0*q2q2 - az)
              + _2bz*q3*(_2bx*(0.5 - q2q2 - q3q3) + _2bz*(q1q3 - q0q2) - mx)
              + (_2bx*q2 + _2bz*q0)*(_2bx*(q1q2 - q0q3) + _2bz*(q0q1 + q2q3) - my)
              + (_2bx*q3 - _4bz*q1)*(_2bx*(q0q2 + q1q3) + _2bz*(0.5 - q1q1 - q2q2) - mz));

        s2 = (-_2q0*(2.0*q1q3 - _2q0q2 - ax)
              + _2q3*(2.0*q0q1 + _2q2q3 - ay)
              - 4.0*q2*(1.0 - 2.0*q1q1 - 2.0*q2q2 - az)
              + (-_4bx*q2 - _2bz*q0)*(_2bx*(0.5 - q2q2 - q3q3) + _2bz*(q1q3 - q0q2) - mx)
              + (_2bx*q1 + _2bz*q3)*(_2bx*(q1q2 - q0q3) + _2bz*(q0q1 + q2q3) - my)
              + (_2bx*q0 - _4bz*q2)*(_2bx*(q0q2 + q1q3) + _2bz*(0.5 - q1q1 - q2q2) - mz));

        s3 = (_2q1*(2.0*q1q3 - _2q0q2 - ax)
              + _2q2*(2.0*q0q1 + _2q2q3 - ay)
              + (-_4bx*q3 + _2bz*q1)*(_2bx*(0.5 - q2q2 - q3q3) + _2bz*(q1q3 - q0q2) - mx)
              + (-_2bx*q0 + _2bz*q2)*(_2bx*(q1q2 - q0q3) + _2bz*(q0q1 + q2q3) - my)
              + _2bx*q1*(_2bx*(q0q2 + q1q3) + _2bz*(0.5 - q1q1 - q2q2) - mz));

        // Suppress unused variable warnings
        (void)_4bx;
        (void)_4bz;
    } else {
        // 6DOF fallback — accel only correction (no heading)
        s0 = -_2q2*(2.0*q1q3 - _2q0q2 - ax) + _2q1*(2.0*q0q1 + _2q2q3 - ay);
        s1 = (_2q3*(2.0*q1q3 - _2q0q2 - ax) + _2q0*(2.0*q0q1 + _2q2q3 - ay)
              - 4.0*q1*(1.0 - 2.0*q1q1 - 2.0*q2q2 - az));
        s2 = (-_2q0*(2.0*q1q3 - _2q0q2 - ax) + _2q3*(2.0*q0q1 + _2q2q3 - ay)
              - 4.0*q2*(1.0 - 2.0*q1q1 - 2.0*q2q2 - az));
        s3 = _2q1*(2.0*q1q3 - _2q0q2 - ax) + _2q2*(2.0*q0q1 + _2q2q3 - ay);
    }

    // Normalize gradient step
    double sNorm = std::sqrt(s0*s0 + s1*s1 + s2*s2 + s3*s3);
    if (sNorm > 0.0) {
        s0 /= sNorm; s1 /= sNorm; s2 /= sNorm; s3 /= sNorm;
    }

    // Apply correction to gyro rate
    qd0 -= beta * s0;
    qd1 -= beta * s1;
    qd2 -= beta * s2;
    qd3 -= beta * s3;

    // Integrate
    q0 += qd0 * dt;
    q1 += qd1 * dt;
    q2 += qd2 * dt;
    q3 += qd3 * dt;

    // Normalize quaternion
    double n = std::sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
    q[0] = q0/n; q[1] = q1/n; q[2] = q2/n; q[3] = q3/n;
}

void MadgwickAHRS::getEuler(double &pitch, double &roll, double &heading) const
{
    double w = q[0], x = q[1], y = q[2], z = q[3];

    // Pitch (nose up/down)
    double sinp = 2.0 * (w * y - z * x);
    if (std::abs(sinp) >= 1.0)
        pitch = std::copysign(90.0, sinp);
    else
        pitch = qRadiansToDegrees(std::asin(sinp));

    // Roll (lean left/right)
    double sinr = 2.0 * (w * x + y * z);
    double cosr = 1.0 - 2.0 * (x * x + y * y);
    roll = qRadiansToDegrees(std::atan2(sinr, cosr));

    // Heading / yaw
    double siny = 2.0 * (w * z + x * y);
    double cosy = 1.0 - 2.0 * (y * y + z * z);
    heading = qRadiansToDegrees(std::atan2(siny, cosy));
    if (heading < 0.0)
        heading += 360.0;
}

// ==================== BerryIMUWorker ====================

BerryIMUWorker::BerryIMUWorker(QObject *parent)
    : QObject(parent)
    , m_running(false)
    , m_fd(-1)
    , m_bmpCalib{}
    , m_gyroBiasX(0.0)
    , m_gyroBiasY(0.0)
    , m_gyroBiasZ(0.0)
    , m_tareQ{1.0, 0.0, 0.0, 0.0}
    , m_accelBiasX(0.0)
    , m_accelBiasY(0.0)
    , m_tareRequested(false)
    , m_tareResetRequested(false)
    , m_lastAx(0.0)
    , m_lastAy(0.0)
    , m_emitInterval(EMIT_INTERVAL)
    , m_ahrs(MADGWICK_BETA)
{
}

BerryIMUWorker::~BerryIMUWorker()
{
    stop();
    closeBus();
}

void BerryIMUWorker::stop()
{
    m_running = false;
}

void BerryIMUWorker::calibrateTare()
{
    m_tareRequested = true;
}

void BerryIMUWorker::resetTare()
{
    m_tareResetRequested = true;
}

void BerryIMUWorker::setEmitInterval(double interval)
{
    m_emitInterval = interval;
}

void BerryIMUWorker::setIdle(bool idle)
{
    m_idle = idle;
}

// ==================== I2C Helpers ====================

bool BerryIMUWorker::openBus(bool &permissionDenied)
{
    permissionDenied = false;
#ifdef Q_OS_LINUX
    QString busPath = QStringLiteral("/dev/i2c-%1").arg(I2C_BUS);
    m_fd = ::open(busPath.toLocal8Bit().constData(), O_RDWR);
    if (m_fd < 0) {
        if (errno == EACCES || errno == EPERM) {
            // Same as the Python backend: a permission problem will not fix
            // itself on retry, so report it once and stop.
            permissionDenied = true;
            qCWarning(lcImu) << "permission denied on" << busPath
                             << "- add the user to the i2c group; not retrying";
        } else {
            qCWarning(lcImu) << "failed to open" << busPath;
        }
        return false;
    }
    return true;
#else
    qCInfo(lcImu) << "I2C not available on this platform";
    return false;
#endif
}

void BerryIMUWorker::closeBus()
{
#ifdef Q_OS_LINUX
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

bool BerryIMUWorker::writeByteData(int addr, int reg, int value)
{
#ifdef Q_OS_LINUX
    if (m_fd < 0) return false;
    if (ioctl(m_fd, I2C_SLAVE, addr) < 0) return false;
    uint8_t buf[2] = { static_cast<uint8_t>(reg), static_cast<uint8_t>(value) };
    return ::write(m_fd, buf, 2) == 2;
#else
    Q_UNUSED(addr); Q_UNUSED(reg); Q_UNUSED(value);
    return false;
#endif
}

int BerryIMUWorker::readByteData(int addr, int reg)
{
#ifdef Q_OS_LINUX
    if (m_fd < 0) return -1;
    if (ioctl(m_fd, I2C_SLAVE, addr) < 0) return -1;
    uint8_t regBuf = static_cast<uint8_t>(reg);
    if (::write(m_fd, &regBuf, 1) != 1) return -1;
    uint8_t val = 0;
    if (::read(m_fd, &val, 1) != 1) return -1;
    return val;
#else
    Q_UNUSED(addr); Q_UNUSED(reg);
    return -1;
#endif
}

bool BerryIMUWorker::readBlockData(int addr, int reg, uint8_t *buf, int len)
{
#ifdef Q_OS_LINUX
    if (m_fd < 0) return false;
    if (ioctl(m_fd, I2C_SLAVE, addr) < 0) return false;
    uint8_t regBuf = static_cast<uint8_t>(reg);
    if (::write(m_fd, &regBuf, 1) != 1) return false;
    return ::read(m_fd, buf, len) == len;
#else
    Q_UNUSED(addr); Q_UNUSED(reg); Q_UNUSED(buf); Q_UNUSED(len);
    return false;
#endif
}

// ==================== Sensor Init ====================

bool BerryIMUWorker::initLSM6DSL()
{
    // Accel: 416Hz, +/-4g
    if (!writeByteData(LSM6DSL_ADDR, 0x10, 0x68)) return false;
    // Gyro: 416Hz, 245dps
    if (!writeByteData(LSM6DSL_ADDR, 0x11, 0x60)) return false;
    // BDU + auto-inc
    if (!writeByteData(LSM6DSL_ADDR, 0x12, 0x44)) return false;
    return true;
}

bool BerryIMUWorker::initLIS3MDL()
{
    // Ultra-high perf, 155Hz
    if (!writeByteData(LIS3MDL_ADDR, 0x20, 0xFE)) return false;
    // +/-8 gauss
    if (!writeByteData(LIS3MDL_ADDR, 0x21, 0x20)) return false;
    // Continuous
    if (!writeByteData(LIS3MDL_ADDR, 0x22, 0x00)) return false;
    // Ultra-high Z
    if (!writeByteData(LIS3MDL_ADDR, 0x23, 0x0C)) return false;
    return true;
}

bool BerryIMUWorker::initBMP388()
{
    // Read calibration data (21 bytes starting at 0x31)
    uint8_t raw[21];
    if (!readBlockData(BMP388_ADDR, 0x31, raw, 21))
        return false;

    std::memcpy(&m_bmpCalib.T1,  &raw[0],  2);
    std::memcpy(&m_bmpCalib.T2,  &raw[2],  2);
    m_bmpCalib.T3  = static_cast<int8_t>(raw[4]);
    std::memcpy(&m_bmpCalib.P1,  &raw[5],  2);
    std::memcpy(&m_bmpCalib.P2,  &raw[7],  2);
    m_bmpCalib.P3  = static_cast<int8_t>(raw[9]);
    m_bmpCalib.P4  = static_cast<int8_t>(raw[10]);
    std::memcpy(&m_bmpCalib.P5,  &raw[11], 2);
    std::memcpy(&m_bmpCalib.P6,  &raw[13], 2);
    m_bmpCalib.P7  = static_cast<int8_t>(raw[15]);
    m_bmpCalib.P8  = static_cast<int8_t>(raw[16]);
    std::memcpy(&m_bmpCalib.P9,  &raw[17], 2);
    m_bmpCalib.P10 = static_cast<int8_t>(raw[19]);
    m_bmpCalib.P11 = static_cast<int8_t>(raw[20]);

    // Configure BMP388
    if (!writeByteData(BMP388_ADDR, 0x1C, 0x03)) return false;
    if (!writeByteData(BMP388_ADDR, 0x1D, 0x02)) return false;
    if (!writeByteData(BMP388_ADDR, 0x1B, 0x33)) return false;
    QThread::msleep(50);
    return true;
}

// ==================== Sensor Read ====================

bool BerryIMUWorker::readAccel(double &ax, double &ay, double &az)
{
    uint8_t data[6];
    if (!readBlockData(LSM6DSL_ADDR, 0x28, data, 6)) {
        ax = ay = az = 0.0;
        return false;
    }
    int16_t rawX, rawY, rawZ;
    std::memcpy(&rawX, &data[0], 2);
    std::memcpy(&rawY, &data[2], 2);
    std::memcpy(&rawZ, &data[4], 2);
    ax = rawX * 0.000122;
    ay = rawY * 0.000122;
    az = rawZ * 0.000122;
    return true;
}

bool BerryIMUWorker::readGyro(double &gx, double &gy, double &gz)
{
    uint8_t data[6];
    if (!readBlockData(LSM6DSL_ADDR, 0x22, data, 6)) {
        gx = gy = gz = 0.0;
        return false;
    }
    int16_t rawX, rawY, rawZ;
    std::memcpy(&rawX, &data[0], 2);
    std::memcpy(&rawY, &data[2], 2);
    std::memcpy(&rawZ, &data[4], 2);
    gx = rawX * 0.00875;
    gy = rawY * 0.00875;
    gz = rawZ * 0.00875;
    return true;
}

bool BerryIMUWorker::readMag(double &mx, double &my, double &mz)
{
    // LIS3MDL auto-increment requires MSB set on register address
    uint8_t data[6];
    if (!readBlockData(LIS3MDL_ADDR, 0x28 | 0x80, data, 6)) {
        mx = my = mz = 0.0;
        return false;
    }
    int16_t rawX, rawY, rawZ;
    std::memcpy(&rawX, &data[0], 2);
    std::memcpy(&rawY, &data[2], 2);
    std::memcpy(&rawZ, &data[4], 2);
    mx = rawX / 3421.0;
    my = rawY / 3421.0;
    mz = rawZ / 3421.0;
    return true;
}

void BerryIMUWorker::readBaro(double &pressure, double &temp, double &altitude)
{
    uint8_t data[6];
    if (!readBlockData(BMP388_ADDR, 0x04, data, 6)) {
        pressure = temp = altitude = 0.0;
        return;
    }

    uint32_t rawPress = data[0] | (data[1] << 8) | (data[2] << 16);
    uint32_t rawTemp  = data[3] | (data[4] << 8) | (data[5] << 16);

    const auto &c = m_bmpCalib;

    double partial1 = static_cast<double>(rawTemp) - (c.T1 * 256.0);
    double partial2 = c.T2 / 1073741824.0;
    double partial3 = partial1 * partial2;
    double partial4 = partial1 * partial1;
    double partial5 = partial4 * (c.T3 / 281474976710656.0);
    double compTemp = partial3 + partial5;

    double t = compTemp;
    double rp = static_cast<double>(rawPress);

    double pp1  = (c.P1  - 16384.0) / 1048576.0;
    double pp2  = (c.P2  - 16384.0) / 536870912.0;
    double pp3  = c.P3   / 4294967296.0;
    double pp4  = c.P4   / 137438953472.0;
    double pp5  = c.P5   / 0.125;
    double pp6  = c.P6   / 64.0;
    double pp7  = c.P7   / 256.0;
    double pp8  = c.P8   / 32768.0;
    double pp9  = c.P9   / 281474976710656.0;
    double pp10 = c.P10  / 281474976710656.0;
    double pp11 = c.P11  / 36893488147419103232.0;

    double out1 = pp5 + pp6*t + pp7*t*t + pp8*t*t*t;
    double out2 = rp * (pp1 + pp2*t + pp3*t*t + pp4*t*t*t);
    double out3 = rp*rp*(pp9 + pp10*t) + rp*rp*rp*pp11;
    double compPress = out1 + out2 + out3;

    double pressureHpa = compPress / 100.0;
    altitude = 44330.0 * (1.0 - std::pow(pressureHpa / SEA_LEVEL_PRESSURE, 1.0 / 5.255));
    pressure = pressureHpa;
    temp = compTemp;
}

// ==================== Gyro Calibration ====================

void BerryIMUWorker::calibrateGyro()
{
    const int N = 200;
    double sx = 0.0, sy = 0.0, sz = 0.0;
    double gx, gy, gz;

    for (int i = 0; i < N; i++) {
        readGyro(gx, gy, gz);
        sx += gx; sy += gy; sz += gz;
        QThread::msleep(5);
    }

    m_gyroBiasX = sx / N;
    m_gyroBiasY = sy / N;
    m_gyroBiasZ = sz / N;

    qCInfo(lcImu) << "gyro calibrated - bias x=" << m_gyroBiasX
                  << "y=" << m_gyroBiasY << "z=" << m_gyroBiasZ << "dps";
}

// ==================== Main Run Loop ====================

void BerryIMUWorker::run()
{
    // Open I2C bus
    bool permissionDenied = false;
    if (!openBus(permissionDenied)) {
        emit connectionStatusChanged(QStringLiteral("Disconnected"));
        emit stopped(!permissionDenied);
        return;
    }

    // Initialize sensors
    if (!initLSM6DSL()) {
        qCWarning(lcImu) << "LSM6DSL init failed";
        closeBus();
        emit connectionStatusChanged(QStringLiteral("Error"));
        emit stopped(true);
        return;
    }
    if (!initLIS3MDL()) {
        qCWarning(lcImu) << "LIS3MDL init failed";
        closeBus();
        emit connectionStatusChanged(QStringLiteral("Error"));
        emit stopped(true);
        return;
    }
    if (!initBMP388()) {
        qCWarning(lcImu) << "BMP388 init failed";
        closeBus();
        emit connectionStatusChanged(QStringLiteral("Error"));
        emit stopped(true);
        return;
    }

    qCInfo(lcImu) << "all sensors initialized successfully";
    emit connectionStatusChanged(QStringLiteral("Connected"));
    emit started();

    m_running = true;

    // Calibrate gyro (takes ~1 second)
    calibrateGyro();

    // Monotonic time tracking
    auto getTime = []() -> double {
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()) / 1e6;
    };

    double lastTime = getTime();
    double lastEmit = 0.0;
    double lastBaro = 0.0;      // 0 => first baro read happens on the first iteration
    bool baroLogged = false;
    int readCount = 0;
    int consecutiveFailures = 0;
    bool busDead = false;

    while (m_running) {
        double now = getTime();
        double dt = now - lastTime;
        lastTime = now;
        if (dt <= 0.0) dt = 0.001;

        // Handle tare requests
        if (m_tareRequested.exchange(false)) {
            // Strip yaw, then compute inverse
            double qw = m_ahrs.q[0], qx = m_ahrs.q[1], qy = m_ahrs.q[2], qz = m_ahrs.q[3];
            double yawNorm = std::sqrt(qw*qw + qz*qz);
            double yw, yz;
            if (yawNorm > 0.001) {
                yw = qw / yawNorm;
                yz = qz / yawNorm;
            } else {
                yw = 1.0; yz = 0.0;
            }
            double tw = yw*qw + yz*qz;
            double tx = yw*qx + yz*qy;
            double ty = yw*qy - yz*qx;
            double tz = yw*qz - yz*qw;

            QMutexLocker locker(&m_tareMutex);
            m_tareQ = {tw, -tx, -ty, -tz};
            m_accelBiasX = m_lastAx;
            m_accelBiasY = m_lastAy;
            qCInfo(lcImu) << "tare set - q=[" << tw << tx << ty << tz << "]"
                          << "accel_bias=[" << m_accelBiasX << m_accelBiasY << "]";
        }

        if (m_tareResetRequested.exchange(false)) {
            QMutexLocker locker(&m_tareMutex);
            m_tareQ = {1.0, 0.0, 0.0, 0.0};
            m_accelBiasX = 0.0;
            m_accelBiasY = 0.0;
            qCInfo(lcImu) << "tare reset to identity";
        }

        // Read all sensors
        double ax, ay, az, gx, gy, gz, mx, my, mz;
        const bool accelOk = readAccel(ax, ay, az);
        const bool gyroOk  = readGyro(gx, gy, gz);
        const bool magOk   = readMag(mx, my, mz);
        if (!accelOk || !gyroOk || !magOk) {
            // Dead-bus detection: zeros must not be fed to the filter, and a
            // sustained failure has to surface as "Error" + retry like the
            // Python backend does instead of silently freezing the model.
            if (++consecutiveFailures >= MAX_CONSECUTIVE_READ_FAILURES) {
                qCWarning(lcImu) << "read loop error:" << consecutiveFailures
                                 << "consecutive I2C read failures (accel=" << accelOk
                                 << "gyro=" << gyroOk << "mag=" << magOk << ")";
                busDead = true;
                break;
            }
            QThread::usleep(static_cast<unsigned long>(SAMPLE_INTERVAL * 1e6));
            continue;
        }
        consecutiveFailures = 0;

        // Remove gyro bias and apply deadband
        double gxCorr = gx - m_gyroBiasX;
        double gyCorr = gy - m_gyroBiasY;
        double gzCorr = gz - m_gyroBiasZ;
        if (std::abs(gxCorr) < GYRO_DEADBAND) gxCorr = 0.0;
        if (std::abs(gyCorr) < GYRO_DEADBAND) gyCorr = 0.0;
        if (std::abs(gzCorr) < GYRO_DEADBAND) gzCorr = 0.0;
        double gxRad = qDegreesToRadians(gxCorr);
        double gyRad = qDegreesToRadians(gyCorr);
        double gzRad = qDegreesToRadians(gzCorr);

        // Only update filter when there's actual motion.
        // Fusion is deliberately accel+gyro only (6DOF): the magnetometer is
        // passed as zeros so MadgwickAHRS takes its 6DOF branch. In-car
        // magnetic interference (alternator, speakers, steel body) would pull
        // the quaternion around if it were fused; the magnetometer is used
        // only for the raw heading readout below. Enabling 9DOF fusion is a
        // behaviour change that needs on-vehicle testing first.
        if (gxCorr != 0.0 || gyCorr != 0.0 || gzCorr != 0.0) {
            m_ahrs.update(gxRad, gyRad, gzRad, ax, ay, az, 0.0, 0.0, 0.0, dt);
        }

        // G-force — store latest for tare capture
        m_lastAx = ax;
        m_lastAy = ay;
        double accelMag = std::sqrt(ax*ax + ay*ay + az*az);

        // Emit at configured rate
        readCount++;
        // Sampling and fusion continue at full rate; only the cross-thread
        // emission (7 queued signals per tick into QML bindings) slows down
        // while nothing on screen consumes it.
        double emitInterval = m_emitInterval.load();
        if (m_idle.load())
            emitInterval = std::max(emitInterval, IDLE_EMIT_INTERVAL);
        if (now - lastEmit >= emitInterval) {
            lastEmit = now;

            // Strip yaw from quaternion — only keep pitch & roll
            double qw = m_ahrs.q[0], qx = m_ahrs.q[1], qy = m_ahrs.q[2], qz = m_ahrs.q[3];
            double yawNorm = std::sqrt(qw*qw + qz*qz);
            double yw, yz;
            if (yawNorm > 0.001) {
                yw = qw / yawNorm;
                yz = qz / yawNorm;
            } else {
                yw = 1.0; yz = 0.0;
            }
            double tw = yw*qw + yz*qz;
            double tx = yw*qx + yz*qy;
            double ty = yw*qy - yz*qx;
            double tz = yw*qz - yz*qw;

            // Apply tare offset: q_out = q_tare * q_current
            double tqw, tqx, tqy, tqz;
            double abx, aby;
            {
                QMutexLocker locker(&m_tareMutex);
                tqw = m_tareQ[0]; tqx = m_tareQ[1]; tqy = m_tareQ[2]; tqz = m_tareQ[3];
                abx = m_accelBiasX;
                aby = m_accelBiasY;
            }
            double ow = tqw*tw - tqx*tx - tqy*ty - tqz*tz;
            double ox = tqw*tx + tqx*tw + tqy*tz - tqz*ty;
            double oy = tqw*ty - tqx*tz + tqy*tw + tqz*tx;
            double oz = tqw*tz + tqx*ty - tqy*tx + tqz*tw;
            emit orientationChanged(static_cast<float>(ow), static_cast<float>(ox),
                                    static_cast<float>(oy), static_cast<float>(oz));

            // Euler angles for gauges — from tared quaternion
            double sinp = 2.0 * (ow * oy - oz * ox);
            double pitch;
            if (std::abs(sinp) >= 1.0)
                pitch = std::copysign(90.0, sinp);
            else
                pitch = qRadiansToDegrees(std::asin(sinp));
            double sinr = 2.0 * (ow * ox + oy * oz);
            double cosr = 1.0 - 2.0 * (ox * ox + oy * oy);
            double roll = qRadiansToDegrees(std::atan2(sinr, cosr));

            emit pitchChanged(static_cast<float>(pitch));
            emit rollChanged(static_cast<float>(roll));

            // Heading from raw magnetometer
            double heading = qRadiansToDegrees(std::atan2(my, mx));
            if (heading < 0.0) heading += 360.0;
            emit headingChanged(static_cast<float>(heading));
            emit accelMagnitudeChanged(static_cast<float>(accelMag));
            emit lateralGChanged(static_cast<float>(ay - aby));
            emit longitudinalGChanged(static_cast<float>(ax - abx));

            if (readCount == 1) {
                qCInfo(lcImu) << "first emit - pitch=" << pitch << "roll=" << roll
                              << "heading=" << heading << "g=" << accelMag
                              << "q=[" << tw << tx << ty << tz << "]";
            }
        }

        // Baro at ~5Hz
        if (now - lastBaro >= BARO_INTERVAL) {
            lastBaro = now;
            double bPressure, bTemp, bAlt;
            readBaro(bPressure, bTemp, bAlt);
            if (bPressure > 0.0) {
                emit altitudeChanged(static_cast<float>(bAlt));
                emit baroTempChanged(static_cast<float>(bTemp));
                if (!baroLogged) {
                    baroLogged = true;
                    qCInfo(lcImu) << "baro - alt=" << bAlt << "m temp=" << bTemp << "C";
                }
            }
        }

        // Sleep out the rest of the sample period (never spin)
        double remaining = SAMPLE_INTERVAL - (getTime() - now);
        if (remaining > 0.0)
            QThread::usleep(static_cast<unsigned long>(remaining * 1e6));
    }

    closeBus();
    qCInfo(lcImu) << "read loop exited after" << readCount << "reads";
    if (busDead)
        emit connectionStatusChanged(QStringLiteral("Error"));
    emit stopped(true);
}

// ==================== BerryIMUManager ====================

BerryIMUManager::BerryIMUManager(QObject *parent)
    : QObject(parent)
    , m_settingsManager(nullptr)
    , m_workerThread(nullptr)
    , m_worker(nullptr)
    , m_emitInterval(EMIT_INTERVAL)
    , m_running(false)
    , m_enabled(true)
    , m_shuttingDown(false)
    , m_retryCount(0)
    , m_maxRetries(5)
    , m_retryDelayMs(5000)
{
    // Start connection attempt after a short delay
    QTimer::singleShot(1000, this, &BerryIMUManager::startSensor);
}

BerryIMUManager::~BerryIMUManager()
{
    cleanup();
}

void BerryIMUManager::connect_settings_manager(SettingsManager *settingsManager)
{
    m_settingsManager = settingsManager;
    QVariant v = settingsManager->property("imuEnabled");
    if (v.isValid())
        m_enabled = v.toBool();
}

bool BerryIMUManager::getConnected() const
{
    return m_running;
}

void BerryIMUManager::scheduleRetry()
{
    if (m_shuttingDown) return;
    if (m_retryCount >= m_maxRetries) {
        qCWarning(lcImu) << "max retries (" << m_maxRetries << ") reached, giving up";
        return;
    }
    m_retryCount++;
    int delay = m_retryDelayMs * m_retryCount;
    qCInfo(lcImu) << "retry" << m_retryCount << "/" << m_maxRetries << "in" << delay << "ms";
    QTimer::singleShot(delay, this, &BerryIMUManager::startSensor);
}

void BerryIMUManager::stopWorker()
{
    if (m_worker) {
        m_worker->stop();
    }
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(2000);
        delete m_workerThread;
        m_workerThread = nullptr;
    }
    if (m_worker) {
        delete m_worker;
        m_worker = nullptr;
    }
    m_running = false;
}

void BerryIMUManager::startSensor()
{
    if (m_shuttingDown) return;
    if (!m_enabled) {
        qCInfo(lcImu) << "disabled in settings, skipping start";
        emit connectionStatusChanged(QStringLiteral("Disabled"));
        return;
    }

    // Clean up previous worker
    stopWorker();

#ifndef Q_OS_LINUX
    qCInfo(lcImu) << "I2C sensors not available on this platform";
    emit connectionStatusChanged(QStringLiteral("Disconnected"));
    return;
#else
    m_worker = new BerryIMUWorker();
    m_worker->setIdle(!m_active);
    m_worker->setEmitInterval(m_emitInterval);
    m_workerThread = new QThread(this);
    m_worker->moveToThread(m_workerThread);

    // Forward all worker signals to manager signals
    connect(m_worker, &BerryIMUWorker::orientationChanged,
            this, &BerryIMUManager::orientationChanged);
    connect(m_worker, &BerryIMUWorker::pitchChanged,
            this, &BerryIMUManager::pitchChanged);
    connect(m_worker, &BerryIMUWorker::rollChanged,
            this, &BerryIMUManager::rollChanged);
    connect(m_worker, &BerryIMUWorker::headingChanged,
            this, &BerryIMUManager::headingChanged);
    connect(m_worker, &BerryIMUWorker::altitudeChanged,
            this, &BerryIMUManager::altitudeChanged);
    connect(m_worker, &BerryIMUWorker::accelMagnitudeChanged,
            this, &BerryIMUManager::accelMagnitudeChanged);
    connect(m_worker, &BerryIMUWorker::lateralGChanged,
            this, &BerryIMUManager::lateralGChanged);
    connect(m_worker, &BerryIMUWorker::longitudinalGChanged,
            this, &BerryIMUManager::longitudinalGChanged);
    connect(m_worker, &BerryIMUWorker::baroTempChanged,
            this, &BerryIMUManager::baroTempChanged);
    connect(m_worker, &BerryIMUWorker::connectionStatusChanged,
            this, &BerryIMUManager::connectionStatusChanged);

    connect(m_worker, &BerryIMUWorker::started, this, [this]() {
        m_running = true;
        m_retryCount = 0;
    });
    connect(m_worker, &BerryIMUWorker::stopped, this, [this](bool retryable) {
        m_running = false;
        if (retryable)
            scheduleRetry();
    });

    // Start worker thread
    connect(m_workerThread, &QThread::started, m_worker, &BerryIMUWorker::run);
    m_workerThread->start();
#endif
}

// ==================== Public Slots ====================

QString BerryIMUManager::getConnectionStatus()
{
    return m_running ? QStringLiteral("Connected") : QStringLiteral("Disconnected");
}

void BerryIMUManager::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    if (m_settingsManager) {
        QMetaObject::invokeMethod(m_settingsManager, "save_imu_enabled",
                                  Q_ARG(bool, enabled));
    }
    if (enabled && !m_running) {
        m_retryCount = 0;
        startSensor();
    } else if (!enabled && m_running) {
        stopWorker();
        emit connectionStatusChanged(QStringLiteral("Disabled"));
    }
}

bool BerryIMUManager::isEnabled()
{
    return m_enabled;
}

void BerryIMUManager::setEmitRate(int hz)
{
    hz = qBound(10, hz, 120);
    m_emitInterval = 1.0 / hz;
    if (m_worker)
        m_worker->setEmitInterval(m_emitInterval);
    qCInfo(lcImu) << "emit rate set to" << hz << "Hz (interval=" << m_emitInterval << "s)";
}

void BerryIMUManager::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    if (m_worker)
        m_worker->setIdle(!active);
    qCDebug(lcImu) << (active ? "sensor page shown: full emit rate" : "no sensor page: idle emit rate");
}

void BerryIMUManager::calibrateTare()
{
    if (m_worker)
        m_worker->calibrateTare();
}

void BerryIMUManager::resetTare()
{
    if (m_worker)
        m_worker->resetTare();
}

void BerryIMUManager::cleanup()
{
    qCInfo(lcImu) << "cleaning up...";
    m_shuttingDown = true;
    stopWorker();
}

#else // Q_OS_MOBILE — Android QtSensors implementation

#include "settingsmanager.h"

#include <QAccelerometer>
#include <QCompass>
#include <QPressureSensor>
#include <QAccelerometerReading>
#include <QCompassReading>
#include <QPressureReading>
#include <QLoggingCategory>
#include <QVariant>
#include <QtMath>
#include <cmath>

static const QLoggingCategory lcImuMobile("octave.imu.mobile");

BerryIMUManager::BerryIMUManager(QObject *parent)
    : QObject(parent),
      m_settingsManager(nullptr),
      m_accel(new QAccelerometer(this)),
      m_compass(new QCompass(this)),
      m_pressure(new QPressureSensor(this)),
      m_emitRate(60),
      m_tarePitch(0.0),
      m_tareRoll(0.0),
      m_enabled(false),
      m_connected(false),
      m_hasPressure(false),
      m_hasTemperature(false)
{
    m_accel->setDataRate(m_emitRate);
    m_compass->setDataRate(m_emitRate);
    m_pressure->setDataRate(10);

    connect(m_accel,    &QAccelerometer::readingChanged,
            this,       &BerryIMUManager::onAccelReading);
    connect(m_compass,  &QCompass::readingChanged,
            this,       &BerryIMUManager::onCompassReading);
    connect(m_pressure, &QPressureSensor::readingChanged,
            this,       &BerryIMUManager::onPressureReading);

    const bool accelOk = m_accel->start();
    m_compass->start();  // may return false; non-fatal, heading just stays 0
    m_hasPressure = m_pressure->start();

    m_connected = accelOk;
    m_enabled   = accelOk;

    if (m_connected) {
        qCInfo(lcImuMobile) << "phone sensors started"
                            << "(accel=" << accelOk
                            << "compass=" << m_compass->isActive()
                            << "pressure=" << m_hasPressure << ")";
        emit connectionStatusChanged(QStringLiteral("Connected"));
    } else {
        qCWarning(lcImuMobile) << "accelerometer unavailable";
        emit connectionStatusChanged(QStringLiteral("Unavailable"));
    }
}

BerryIMUManager::~BerryIMUManager()
{
    cleanup();
}

void BerryIMUManager::connect_settings_manager(SettingsManager *sm)
{
    m_settingsManager = sm;
    // Desktop reads imuEnabled before its delayed start; the phone sensors
    // were already started in the constructor, so honour a persisted "off"
    // here by stopping them again.
    QVariant v = sm->property("imuEnabled");
    if (v.isValid() && !v.toBool() && m_enabled) {
        m_enabled = false;
        m_accel->stop();
        m_compass->stop();
        m_pressure->stop();
        m_connected = false;
        qCInfo(lcImuMobile) << "disabled in settings, sensors stopped";
        emit connectionStatusChanged(QStringLiteral("Disabled"));
    }
}

bool BerryIMUManager::getConnected() const
{
    return m_connected;
}

QString BerryIMUManager::getConnectionStatus()
{
    return m_connected ? QStringLiteral("Connected") : QStringLiteral("Unavailable");
}

void BerryIMUManager::onAccelReading()
{
    QAccelerometerReading *r = m_accel->reading();
    if (!r) return;

    const double ax = r->x();
    const double ay = r->y();
    const double az = r->z();

    const double gMag = std::sqrt(ax*ax + ay*ay + az*az) / 9.81;
    emit accelMagnitudeChanged(static_cast<float>(gMag));
    emit lateralGChanged(static_cast<float>(ax / 9.81));
    emit longitudinalGChanged(static_cast<float>(ay / 9.81));

    double pitch = std::atan2(ay, std::sqrt(ax*ax + az*az)) * 180.0 / M_PI;
    double roll  = std::atan2(-ax, az) * 180.0 / M_PI;
    pitch -= m_tarePitch;
    roll  -= m_tareRoll;
    emit pitchChanged(static_cast<float>(pitch));
    emit rollChanged(static_cast<float>(roll));

    // QtSensors gives no quaternion here (QAccelerometer only), so build the
    // same yaw-stripped, tared orientation desktop emits from the tared
    // pitch/roll: q = q_pitch(Y) * q_roll(X) with yaw = 0. Inverting it with
    // the desktop's pitch = asin(2(wy - zx)) / roll = atan2(...) formulas
    // returns exactly these angles, so the CarMenu model sees one convention.
    const double hp = qDegreesToRadians(pitch) * 0.5;
    const double hr = qDegreesToRadians(roll) * 0.5;
    const double cp = std::cos(hp), sp = std::sin(hp);
    const double cr = std::cos(hr), sr = std::sin(hr);
    emit orientationChanged(static_cast<float>(cp * cr),
                            static_cast<float>(cp * sr),
                            static_cast<float>(sp * cr),
                            static_cast<float>(-sp * sr));
}

void BerryIMUManager::onCompassReading()
{
    QCompassReading *r = m_compass->reading();
    if (!r) return;
    double az = r->azimuth();
    // Qt docs say 0–360 but some Android implementations emit -180..180.
    // Normalize so QML cardinal lookup (dirs[Math.round(h/45) % 8]) never
    // indexes with a negative number.
    if (az < 0.0) az += 360.0;
    emit headingChanged(static_cast<float>(az));
}

void BerryIMUManager::onPressureReading()
{
    QPressureReading *r = m_pressure->reading();
    if (!r) return;
    const double p = r->pressure();
    if (p > 0) {
        // Standard atmosphere: h = 44330 * (1 - (p/p0)^(1/5.255))
        const double altitude = 44330.0 * (1.0 - std::pow(p / 101325.0, 1.0 / 5.255));
        emit altitudeChanged(static_cast<float>(altitude));
    }
    // Most phones' barometer chips don't surface ambient temperature to
    // Android (Qt returns 0.0). Only emit / advertise hasTemperature once
    // we see a real non-zero reading.
    const double tempC = r->temperature();
    if (tempC != 0.0) {
        if (!m_hasTemperature) {
            m_hasTemperature = true;
            emit hasTemperatureChanged(true);
        }
        emit baroTempChanged(static_cast<float>(tempC));
    }
}

void BerryIMUManager::setEnabled(bool enabled)
{
    if (enabled == m_enabled) return;
    m_enabled = enabled;
    // Persist exactly like the desktop branch so the toggle survives restart.
    if (m_settingsManager) {
        QMetaObject::invokeMethod(m_settingsManager, "save_imu_enabled",
                                  Q_ARG(bool, enabled));
    }
    if (enabled) {
        m_connected = m_accel->start();
        m_compass->start();
        if (m_hasPressure) m_pressure->start();
        emit connectionStatusChanged(m_connected ? QStringLiteral("Connected")
                                                 : QStringLiteral("Unavailable"));
    } else {
        m_accel->stop();
        m_compass->stop();
        m_pressure->stop();
        m_connected = false;   // `connected` reads false while disabled, as on desktop
        emit connectionStatusChanged(QStringLiteral("Disabled"));
    }
}

bool BerryIMUManager::isEnabled() { return m_enabled; }

void BerryIMUManager::setEmitRate(int hz)
{
    hz = qBound(10, hz, 120);   // same clamp as desktop / Python
    m_emitRate = hz;
    const int rate = m_active ? m_emitRate : 5;
    m_accel->setDataRate(rate);
    m_compass->setDataRate(rate);
}

void BerryIMUManager::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    const int rate = m_active ? m_emitRate : 5;   // 5 Hz while no page shows the values
    if (m_accel) m_accel->setDataRate(rate);
    if (m_compass) m_compass->setDataRate(rate);
}

void BerryIMUManager::calibrateTare()
{
    QAccelerometerReading *r = m_accel->reading();
    if (!r) return;
    const double ax = r->x();
    const double ay = r->y();
    const double az = r->z();
    m_tarePitch = std::atan2(ay, std::sqrt(ax*ax + az*az)) * 180.0 / M_PI;
    m_tareRoll  = std::atan2(-ax, az) * 180.0 / M_PI;
}

void BerryIMUManager::resetTare()
{
    m_tarePitch = 0.0;
    m_tareRoll  = 0.0;
}

void BerryIMUManager::cleanup()
{
    if (m_accel)    m_accel->stop();
    if (m_compass)  m_compass->stop();
    if (m_pressure) m_pressure->stop();
    m_connected = false;
}

#endif // Q_OS_MOBILE
