/*
 * Gesture Manager for OCTAVE (C++ port)
 * PAJ7620U2 I2C gesture sensor — detects hand gestures for touchless control.
 *
 * Gesture protocol (I2C at 0x73):
 *   Register 0x43: directional + rotation flags
 *   Register 0x44: wave flag
 *
 * Two-pass disambiguation for forward/backward compound gestures
 * (the sensor initially reports directional, then settles).
 *
 * Linux-only — stubs on other platforms.
 */

#include "gesturemanager.h"

#ifndef Q_OS_MOBILE

#include "settingsmanager.h"

#include <QLoggingCategory>
#include <QThread>
#include <QVariant>
#include <QVariantMap>
#include <cstring>

#ifdef Q_OS_LINUX
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif

Q_LOGGING_CATEGORY(lcGesture, "octave.gesture")

// ==================== Constants ====================

static constexpr int I2C_BUS          = 2;
static constexpr int PAJ7620_ADDR     = 0x73;

// Register addresses
static constexpr int REG_BANK_SEL     = 0xEF;
static constexpr int REG_CHIP_ID_L    = 0x00;
static constexpr int REG_CHIP_ID_H    = 0x01;
static constexpr int REG_GESTURE_0    = 0x43;
static constexpr int REG_GESTURE_1    = 0x44;

// Gesture flags (Register 0x43)
static constexpr uint8_t GES_RIGHT            = 0x01;
static constexpr uint8_t GES_LEFT             = 0x02;
static constexpr uint8_t GES_UP               = 0x04;
static constexpr uint8_t GES_DOWN             = 0x08;
static constexpr uint8_t GES_FORWARD          = 0x10;
static constexpr uint8_t GES_BACKWARD         = 0x20;
static constexpr uint8_t GES_CLOCKWISE        = 0x40;
static constexpr uint8_t GES_COUNTERCLOCKWISE = 0x80;

// Gesture flag (Register 0x44)
static constexpr uint8_t GES_WAVE = 0x01;

// Timing
static constexpr double GESTURE_COOLDOWN     = 0.3;
static constexpr double POLL_INTERVAL        = 0.02;   // ~50Hz
static constexpr double GES_FORWARD_WAIT     = 0.30;
static constexpr double GES_BACKWARD_WAIT    = 0.40;
static constexpr double GES_POST_FLUSH       = 0.10;

// Consecutive failed gesture-register reads before the loop declares the bus
// dead, emits "Error" and hands off to the manager's retry/back-off path
// (Python raises on the first OSError; a small threshold rides out one NACK).
static constexpr int MAX_CONSECUTIVE_READ_FAILURES = 5;

// PAJ7620U2 initialization register table (DFRobot reference, 219 register pairs)
static const uint8_t INIT_REGISTER_ARRAY[][2] = {
    // Bank 0
    {0xEF, 0x00},
    {0x32, 0x29}, {0x33, 0x01}, {0x34, 0x00}, {0x35, 0x01},
    {0x36, 0x00}, {0x37, 0x07}, {0x38, 0x17}, {0x39, 0x06},
    {0x3A, 0x12}, {0x3F, 0x00}, {0x40, 0x02}, {0x41, 0xFF},
    {0x42, 0x01}, {0x46, 0x2D}, {0x47, 0x0F}, {0x48, 0x3C},
    {0x49, 0x00}, {0x4A, 0x1E}, {0x4B, 0x00}, {0x4C, 0x20},
    {0x4D, 0x00}, {0x4E, 0x1A}, {0x4F, 0x14}, {0x50, 0x00},
    {0x51, 0x10}, {0x52, 0x00}, {0x5C, 0x02}, {0x5D, 0x00},
    {0x5E, 0x10}, {0x5F, 0x3F}, {0x60, 0x27}, {0x61, 0x28},
    {0x62, 0x00}, {0x63, 0x03}, {0x64, 0xF7}, {0x65, 0x03},
    {0x66, 0xD9}, {0x67, 0x03}, {0x68, 0x01}, {0x69, 0xC8},
    {0x6A, 0x40}, {0x6D, 0x04}, {0x6E, 0x00}, {0x6F, 0x00},
    {0x70, 0x80}, {0x71, 0x00}, {0x72, 0x00}, {0x73, 0x00},
    {0x74, 0xF0}, {0x75, 0x00}, {0x80, 0x42}, {0x81, 0x44},
    {0x82, 0x04}, {0x83, 0x20}, {0x84, 0x20}, {0x85, 0x00},
    {0x86, 0x10}, {0x87, 0x00}, {0x88, 0x05}, {0x89, 0x18},
    {0x8A, 0x10}, {0x8B, 0x01}, {0x8C, 0x37}, {0x8D, 0x00},
    {0x8E, 0xF0}, {0x8F, 0x81}, {0x90, 0x06}, {0x91, 0x06},
    {0x92, 0x1E}, {0x93, 0x0D}, {0x94, 0x0A}, {0x95, 0x0A},
    {0x96, 0x0C}, {0x97, 0x05}, {0x98, 0x0A}, {0x99, 0x41},
    {0x9A, 0x14}, {0x9B, 0x0A}, {0x9C, 0x3F}, {0x9D, 0x33},
    {0x9E, 0xAE}, {0x9F, 0xF9}, {0xA0, 0x48}, {0xA1, 0x13},
    {0xA2, 0x10}, {0xA3, 0x08}, {0xA4, 0x30}, {0xA5, 0x19},
    {0xA6, 0x10}, {0xA7, 0x08}, {0xA8, 0x24}, {0xA9, 0x04},
    {0xAA, 0x1E}, {0xAB, 0x1E}, {0xCC, 0x19}, {0xCD, 0x0B},
    {0xCE, 0x13}, {0xCF, 0x64}, {0xD0, 0x21}, {0xD1, 0x0F},
    {0xD2, 0x88}, {0xE0, 0x01}, {0xE1, 0x04}, {0xE2, 0x41},
    {0xE3, 0xD6}, {0xE4, 0x00}, {0xE5, 0x0C}, {0xE6, 0x0A},
    {0xE7, 0x00}, {0xE8, 0x00}, {0xE9, 0x00}, {0xEE, 0x07},
    // Bank 1
    {0xEF, 0x01},
    {0x00, 0x1E}, {0x01, 0x1E}, {0x02, 0x0F}, {0x03, 0x10},
    {0x04, 0x02}, {0x05, 0x00}, {0x06, 0xB0}, {0x07, 0x04},
    {0x08, 0x0D}, {0x09, 0x0E}, {0x0A, 0x9C}, {0x0B, 0x04},
    {0x0C, 0x05}, {0x0D, 0x0F}, {0x0E, 0x02}, {0x0F, 0x12},
    {0x10, 0x02}, {0x11, 0x02}, {0x12, 0x00}, {0x13, 0x01},
    {0x14, 0x05}, {0x15, 0x07}, {0x16, 0x05}, {0x17, 0x07},
    {0x18, 0x01}, {0x19, 0x04}, {0x1A, 0x05}, {0x1B, 0x0C},
    {0x1C, 0x2A}, {0x1D, 0x01}, {0x1E, 0x00}, {0x21, 0x00},
    {0x22, 0x00}, {0x23, 0x00}, {0x25, 0x01}, {0x26, 0x00},
    {0x27, 0x39}, {0x28, 0x7F}, {0x29, 0x08}, {0x30, 0x03},
    {0x31, 0x00}, {0x32, 0x1A}, {0x33, 0x1A}, {0x34, 0x07},
    {0x35, 0x07}, {0x36, 0x01}, {0x37, 0xFF}, {0x38, 0x36},
    {0x39, 0x07}, {0x3A, 0x00}, {0x3E, 0xFF}, {0x3F, 0x00},
    {0x40, 0x77}, {0x41, 0x40}, {0x42, 0x00}, {0x43, 0x30},
    {0x44, 0xA0}, {0x45, 0x5C}, {0x46, 0x00}, {0x47, 0x00},
    {0x48, 0x58}, {0x4A, 0x1E}, {0x4B, 0x1E}, {0x4C, 0x00},
    {0x4D, 0x00}, {0x4E, 0xA0}, {0x4F, 0x80}, {0x50, 0x00},
    {0x51, 0x00}, {0x52, 0x00}, {0x53, 0x00}, {0x54, 0x00},
    {0x57, 0x80}, {0x59, 0x10}, {0x5A, 0x08}, {0x5B, 0x94},
    {0x5C, 0xE8}, {0x5D, 0x08}, {0x5E, 0x3D}, {0x5F, 0x99},
    {0x60, 0x45}, {0x61, 0x40}, {0x63, 0x2D}, {0x64, 0x02},
    {0x65, 0x96}, {0x66, 0x00}, {0x67, 0x97}, {0x68, 0x01},
    {0x69, 0xCD}, {0x6A, 0x01}, {0x6B, 0xB0}, {0x6C, 0x04},
    {0x6D, 0x2C}, {0x6E, 0x01}, {0x6F, 0x32}, {0x71, 0x00},
    {0x72, 0x01}, {0x73, 0x35}, {0x74, 0x00}, {0x75, 0x33},
    {0x76, 0x31}, {0x77, 0x01}, {0x7C, 0x84}, {0x7D, 0x03},
    {0x7E, 0x01},
};

static constexpr int INIT_REGISTER_COUNT =
    sizeof(INIT_REGISTER_ARRAY) / sizeof(INIT_REGISTER_ARRAY[0]);

// Gesture flag-to-name lookup
struct GestureFlagName {
    uint8_t flag;
    const char *name;
};

static const GestureFlagName GESTURE_NAMES[] = {
    { GES_RIGHT,            "RIGHT" },
    { GES_LEFT,             "LEFT" },
    { GES_UP,               "UP" },
    { GES_DOWN,             "DOWN" },
    { GES_FORWARD,          "FORWARD" },
    { GES_BACKWARD,         "BACKWARD" },
    { GES_CLOCKWISE,        "CLOCKWISE" },
    { GES_COUNTERCLOCKWISE, "COUNTER-CLOCKWISE" },
};
static constexpr int GESTURE_NAMES_COUNT =
    sizeof(GESTURE_NAMES) / sizeof(GESTURE_NAMES[0]);

// ==================== GestureWorker ====================

GestureWorker::GestureWorker(QObject *parent)
    : QObject(parent)
    , m_running(false)
    , m_fd(-1)
    , m_cooldown(GESTURE_COOLDOWN)
{
}

GestureWorker::~GestureWorker()
{
    stop();
    closeBus();
}

void GestureWorker::stop()
{
    m_running = false;
}

void GestureWorker::setCooldown(double seconds)
{
    QMutexLocker locker(&m_configMutex);
    m_cooldown = seconds;
}

void GestureWorker::setGestureMapping(const QMap<QString, QString> &mapping)
{
    QMutexLocker locker(&m_configMutex);
    m_gestureMapping = mapping;
}

QMap<QString, QString> GestureWorker::gestureMapping() const
{
    QMutexLocker locker(&m_configMutex);
    return m_gestureMapping;
}

// ==================== I2C Helpers ====================

bool GestureWorker::openBus(bool &permissionDenied)
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
            qCWarning(lcGesture) << "permission denied on" << busPath
                                 << "- add the user to the i2c group; not retrying";
        } else {
            qCWarning(lcGesture) << "failed to open" << busPath;
        }
        return false;
    }
    return true;
#else
    qCInfo(lcGesture) << "I2C not available on this platform";
    return false;
#endif
}

void GestureWorker::closeBus()
{
#ifdef Q_OS_LINUX
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

bool GestureWorker::writeByteData(int addr, int reg, int value)
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

int GestureWorker::readByteData(int addr, int reg)
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

bool GestureWorker::readBlockData(int addr, int reg, uint8_t *buf, int len)
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

bool GestureWorker::wakeup()
{
    if (!writeByteData(PAJ7620_ADDR, REG_BANK_SEL, 0x00)) {
        QThread::msleep(50);
        if (!writeByteData(PAJ7620_ADDR, REG_BANK_SEL, 0x00))
            return false;
    }
    QThread::msleep(10);
    return true;
}

bool GestureWorker::readChipId(uint16_t &chipId)
{
    writeByteData(PAJ7620_ADDR, REG_BANK_SEL, 0x00);
    int idLow = readByteData(PAJ7620_ADDR, REG_CHIP_ID_L);
    int idHigh = readByteData(PAJ7620_ADDR, REG_CHIP_ID_H);
    if (idLow < 0 || idHigh < 0)
        return false;
    chipId = static_cast<uint16_t>((idHigh << 8) | idLow);
    return true;
}

bool GestureWorker::initRegisters()
{
    for (int i = 0; i < INIT_REGISTER_COUNT; i++) {
        if (!writeByteData(PAJ7620_ADDR, INIT_REGISTER_ARRAY[i][0], INIT_REGISTER_ARRAY[i][1]))
            return false;
    }
    // Switch back to Bank 0 for gesture reading
    writeByteData(PAJ7620_ADDR, REG_BANK_SEL, 0x00);
    return true;
}

// ==================== Gesture Reading ====================

bool GestureWorker::readGestureRaw(uint8_t &flag0, uint8_t &flag1)
{
    uint8_t data[2] = {0, 0};
    const bool ok = readBlockData(PAJ7620_ADDR, REG_GESTURE_0, data, 2);
    flag0 = ok ? data[0] : 0;
    flag1 = ok ? data[1] : 0;
    return ok;
}

QString GestureWorker::decodeGesture(uint8_t flag0, uint8_t flag1) const
{
    if (flag0) {
        for (int i = 0; i < GESTURE_NAMES_COUNT; i++) {
            if (flag0 & GESTURE_NAMES[i].flag)
                return QString::fromLatin1(GESTURE_NAMES[i].name);
        }
    }
    if (flag1 & GES_WAVE) {
        return QStringLiteral("WAVE");
    }
    return QString();
}

void GestureWorker::flushSensor(double duration)
{
    auto getTime = []() -> double {
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()) / 1e6;
    };

    double end = getTime() + duration;
    while (getTime() < end && m_running) {
        uint8_t data[2];
        readBlockData(PAJ7620_ADDR, REG_GESTURE_0, data, 2);
        QThread::msleep(10);
    }
}

// ==================== Main Run Loop ====================

void GestureWorker::run()
{
    // Open I2C bus
    bool permissionDenied = false;
    if (!openBus(permissionDenied)) {
        emit connectionStatusChanged(QStringLiteral("Disconnected"));
        emit stopped(!permissionDenied);
        return;
    }

    // Wakeup and verify chip ID
    if (!wakeup()) {
        qCWarning(lcGesture) << "PAJ7620 wakeup failed";
        closeBus();
        emit connectionStatusChanged(QStringLiteral("Error"));
        emit stopped(true);
        return;
    }

    uint16_t chipId = 0;
    if (!readChipId(chipId)) {
        qCWarning(lcGesture) << "failed to read chip ID";
        closeBus();
        emit connectionStatusChanged(QStringLiteral("Error"));
        emit stopped(true);
        return;
    }

    if (chipId != 0x7620) {
        qCWarning(lcGesture) << "expected chip ID 0x7620, got" << Qt::hex << chipId;
        closeBus();
        emit connectionStatusChanged(QStringLiteral("Error"));
        emit stopped(true);
        return;
    }

    if (!initRegisters()) {
        qCWarning(lcGesture) << "register init failed";
        closeBus();
        emit connectionStatusChanged(QStringLiteral("Error"));
        emit stopped(true);
        return;
    }

    qCInfo(lcGesture) << "PAJ7620U2 initialized (chip ID: 0x" << Qt::hex << chipId << ")";
    emit connectionStatusChanged(QStringLiteral("Connected"));
    emit started();

    m_running = true;

    auto getTime = []() -> double {
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()) / 1e6;
    };

    double lastGestureTime = 0.0;
    int readCount = 0;
    int consecutiveFailures = 0;
    bool busDead = false;
    int pollMs = static_cast<int>(POLL_INTERVAL * 1000);

    while (m_running) {
        uint8_t flag0, flag1;
        if (!readGestureRaw(flag0, flag1)) {
            // Dead-bus detection: surface a sustained failure as "Error" +
            // retry like the Python backend instead of polling zeros forever.
            if (++consecutiveFailures >= MAX_CONSECUTIVE_READ_FAILURES) {
                qCWarning(lcGesture) << "read loop error:" << consecutiveFailures
                                     << "consecutive I2C read failures";
                busDead = true;
                break;
            }
            QThread::msleep(pollMs);
            continue;
        }
        consecutiveFailures = 0;

        if (!flag0 && !(flag1 & GES_WAVE)) {
            QThread::msleep(pollMs);
            continue;
        }

        // Cooldown gate
        double now = getTime();
        double cooldown;
        {
            QMutexLocker locker(&m_configMutex);
            cooldown = m_cooldown;
        }
        if ((now - lastGestureTime) <= cooldown) {
            QThread::msleep(pollMs);
            continue;
        }

        // --- Two-pass disambiguation for forward/backward ---
        QString gesture;

        if (flag0 & GES_FORWARD) {
            QThread::msleep(static_cast<int>(GES_FORWARD_WAIT * 1000));
            if (!m_running) break;
            uint8_t f0, f1;
            readGestureRaw(f0, f1);
            QString resolved = decodeGesture(f0, f1);
            if (!resolved.isEmpty() && resolved != QStringLiteral("FORWARD")
                    && resolved != QStringLiteral("BACKWARD")) {
                gesture = resolved;
            } else {
                gesture = QStringLiteral("FORWARD");
            }
        } else if (flag0 & GES_BACKWARD) {
            QThread::msleep(static_cast<int>(GES_BACKWARD_WAIT * 1000));
            if (!m_running) break;
            uint8_t f0, f1;
            readGestureRaw(f0, f1);
            QString resolved = decodeGesture(f0, f1);
            if (!resolved.isEmpty() && resolved != QStringLiteral("FORWARD")
                    && resolved != QStringLiteral("BACKWARD")) {
                gesture = resolved;
            } else {
                gesture = QStringLiteral("BACKWARD");
            }
        } else {
            // Directional / rotation / wave — confirm with a second read to
            // reject single-sample glitches. The PAJ7620 clears 0x43/0x44 on
            // read, so a clean gesture re-reads as either the same flag or
            // nothing; a *different* flag 20 ms later means the sensor is
            // still settling on some other motion, so the sample is dropped.
            QThread::msleep(20);
            uint8_t f0Confirm, f1Confirm;
            readGestureRaw(f0Confirm, f1Confirm);
            QString first = decodeGesture(flag0, flag1);
            QString second = decodeGesture(f0Confirm, f1Confirm);

            if (second.isEmpty() || second == first) {
                gesture = first; // Agreed, or sensor already cleared itself
            } else {
                qCDebug(lcGesture) << "discarding" << first
                                   << "- confirmation read saw" << second;
            }
        }

        if (!gesture.isEmpty()) {
            readCount++;
            lastGestureTime = getTime();

            emit gestureDetected(gesture);

            QString action;
            {
                QMutexLocker locker(&m_configMutex);
                action = m_gestureMapping.value(gesture, QStringLiteral("none"));
            }
            if (!action.isEmpty() && action != QStringLiteral("none")) {
                emit actionTriggered(action);
                qCInfo(lcGesture) << gesture << "->" << action << "(#" << readCount << ")";
            }

            // Flush residual data
            flushSensor(GES_POST_FLUSH);
        }

        QThread::msleep(pollMs);
    }

    closeBus();
    qCInfo(lcGesture) << "read loop exited after" << readCount << "gestures";
    if (busDead)
        emit connectionStatusChanged(QStringLiteral("Error"));
    emit stopped(true);
}

// ==================== GestureManager ====================

QMap<QString, QString> GestureManager::defaultGestureMapping()
{
    QMap<QString, QString> mapping;
    mapping[QStringLiteral("RIGHT")]             = QStringLiteral("next_track");
    mapping[QStringLiteral("LEFT")]              = QStringLiteral("previous_track");
    mapping[QStringLiteral("UP")]                = QStringLiteral("volume_up");
    mapping[QStringLiteral("DOWN")]              = QStringLiteral("volume_down");
    mapping[QStringLiteral("FORWARD")]           = QStringLiteral("play_pause");
    mapping[QStringLiteral("BACKWARD")]          = QStringLiteral("mute_toggle");
    mapping[QStringLiteral("CLOCKWISE")]         = QStringLiteral("volume_up");
    mapping[QStringLiteral("COUNTER-CLOCKWISE")] = QStringLiteral("volume_down");
    mapping[QStringLiteral("WAVE")]              = QStringLiteral("play_pause");
    return mapping;
}

GestureManager::GestureManager(QObject *parent)
    : QObject(parent)
    , m_settingsManager(nullptr)
    , m_workerThread(nullptr)
    , m_worker(nullptr)
    , m_running(false)
    , m_enabled(true)
    , m_shuttingDown(false)
    , m_retryCount(0)
    , m_maxRetries(5)
    , m_retryDelayMs(5000)
    , m_cooldown(GESTURE_COOLDOWN)
    , m_gestureMapping(defaultGestureMapping())
{
    QTimer::singleShot(1000, this, &GestureManager::startSensor);
}

GestureManager::~GestureManager()
{
    cleanup();
}

void GestureManager::connect_settings_manager(SettingsManager *settingsManager)
{
    m_settingsManager = settingsManager;

    QVariant enabledVar = settingsManager->property("gestureSensorEnabled");
    if (enabledVar.isValid())
        m_enabled = enabledVar.toBool();

    QVariant cooldownVar = settingsManager->property("gestureCooldown");
    if (cooldownVar.isValid())
        m_cooldown = cooldownVar.toInt() / 1000.0;

    // Try to load gesture mapping from settings
    QVariantMap vmap;
    bool ok = QMetaObject::invokeMethod(settingsManager, "get_gesture_mapping_dict",
                                         Qt::DirectConnection,
                                         Q_RETURN_ARG(QVariantMap, vmap));
    if (ok) {
        if (!vmap.isEmpty()) {
            m_gestureMapping.clear();
            for (auto it = vmap.constBegin(); it != vmap.constEnd(); ++it) {
                m_gestureMapping[it.key()] = it.value().toString();
            }
        }
    }

    // Push to worker if already running
    if (m_worker) {
        m_worker->setCooldown(m_cooldown);
        m_worker->setGestureMapping(m_gestureMapping);
    }
}

void GestureManager::stopWorker()
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

void GestureManager::scheduleRetry()
{
    if (m_shuttingDown) return;
    if (m_retryCount >= m_maxRetries) {
        qCWarning(lcGesture) << "max retries (" << m_maxRetries << ") reached, giving up";
        return;
    }
    m_retryCount++;
    int delay = m_retryDelayMs * m_retryCount;
    qCInfo(lcGesture) << "retry" << m_retryCount << "/" << m_maxRetries << "in" << delay << "ms";
    QTimer::singleShot(delay, this, &GestureManager::startSensor);
}

void GestureManager::startSensor()
{
    if (m_shuttingDown) return;
    if (!m_enabled) {
        qCInfo(lcGesture) << "disabled in settings, skipping start";
        emit connectionStatusChanged(QStringLiteral("Disabled"));
        return;
    }

    // Clean up previous worker
    stopWorker();

#ifndef Q_OS_LINUX
    qCInfo(lcGesture) << "I2C sensors not available on this platform";
    emit connectionStatusChanged(QStringLiteral("Disconnected"));
    return;
#else
    m_worker = new GestureWorker();
    m_worker->setCooldown(m_cooldown);
    m_worker->setGestureMapping(m_gestureMapping);

    m_workerThread = new QThread(this);
    m_worker->moveToThread(m_workerThread);

    // Forward worker signals to manager signals
    connect(m_worker, &GestureWorker::gestureDetected,
            this, &GestureManager::gestureDetected);
    connect(m_worker, &GestureWorker::actionTriggered,
            this, &GestureManager::actionTriggered);
    connect(m_worker, &GestureWorker::connectionStatusChanged,
            this, &GestureManager::connectionStatusChanged);

    connect(m_worker, &GestureWorker::started, this, [this]() {
        m_running = true;
        m_retryCount = 0;
    });
    connect(m_worker, &GestureWorker::stopped, this, [this](bool retryable) {
        m_running = false;
        if (retryable)
            scheduleRetry();
    });

    connect(m_workerThread, &QThread::started, m_worker, &GestureWorker::run);
    m_workerThread->start();
#endif
}

// ==================== Public Slots ====================

QString GestureManager::getConnectionStatus()
{
    return m_running ? QStringLiteral("Connected") : QStringLiteral("Disconnected");
}

void GestureManager::setGestureAction(const QString &gesture, const QString &action)
{
    m_gestureMapping[gesture] = action;
    if (m_worker)
        m_worker->setGestureMapping(m_gestureMapping);
    if (m_settingsManager) {
        QVariantMap vmap;
        for (auto it = m_gestureMapping.constBegin(); it != m_gestureMapping.constEnd(); ++it)
            vmap[it.key()] = it.value();
        QMetaObject::invokeMethod(m_settingsManager, "save_gesture_mapping",
                                  Q_ARG(QVariant, QVariant::fromValue(vmap)));
    }
}

QString GestureManager::getGestureAction(const QString &gesture)
{
    return m_gestureMapping.value(gesture, QStringLiteral("none"));
}

void GestureManager::resetMappingToDefaults()
{
    m_gestureMapping = defaultGestureMapping();
    if (m_worker)
        m_worker->setGestureMapping(m_gestureMapping);
    if (m_settingsManager) {
        QVariantMap vmap;
        for (auto it = m_gestureMapping.constBegin(); it != m_gestureMapping.constEnd(); ++it)
            vmap[it.key()] = it.value();
        QMetaObject::invokeMethod(m_settingsManager, "save_gesture_mapping",
                                  Q_ARG(QVariant, QVariant::fromValue(vmap)));
    }
}

void GestureManager::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    if (m_settingsManager) {
        QMetaObject::invokeMethod(m_settingsManager, "save_gesture_sensor_enabled",
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

void GestureManager::setCooldown(int ms)
{
    m_cooldown = ms / 1000.0;
    if (m_worker)
        m_worker->setCooldown(m_cooldown);
}

bool GestureManager::isEnabled()
{
    return m_enabled;
}

void GestureManager::cleanup()
{
    qCInfo(lcGesture) << "cleaning up...";
    m_shuttingDown = true;
    stopWorker();
}

#endif // Q_OS_MOBILE
