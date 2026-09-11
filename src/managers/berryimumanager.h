#ifndef BERRYIMUMANAGER_H
#define BERRYIMUMANAGER_H

// BerryIMU v3 uses Linux I2C — excluded on mobile builds.
// Android replacement (device sensors via QSensor) is Phase 2 work.
#ifndef Q_OS_MOBILE

#include <QObject>
#include <QString>
#include <QTimer>
#include <QMutex>
#include <QThread>
#include <array>
#include <atomic>

// Forward declaration
class SettingsManager;

// ==================== Madgwick AHRS Filter ====================

// Full MARG (9DOF) implementation, but the worker deliberately calls
// update() with mx=my=mz=0 so only the 6DOF accel+gyro branch runs —
// see the call site in BerryIMUWorker::run() for why.
class MadgwickAHRS
{
public:
    explicit MadgwickAHRS(double beta = 0.04);

    void update(double gx, double gy, double gz,
                double ax, double ay, double az,
                double mx, double my, double mz,
                double dt);

    // Currently unused: the worker derives pitch/roll from the tared,
    // yaw-stripped quaternion itself and heading from the raw magnetometer.
    // Only meaningful for heading if 9DOF fusion is ever enabled.
    void getEuler(double &pitch, double &roll, double &heading) const;

    // Quaternion: [w, x, y, z]
    std::array<double, 4> q;
    double beta;
};

// ==================== I2C Worker Thread ====================

class BerryIMUWorker : public QObject
{
    Q_OBJECT

public:
    explicit BerryIMUWorker(QObject *parent = nullptr);
    ~BerryIMUWorker() override;

    void stop();

    // Tare controls (thread-safe)
    void calibrateTare();
    void resetTare();
    void setEmitInterval(double interval);
    // No page is showing the values: emit at IDLE_EMIT_INTERVAL instead
    void setIdle(bool idle);

signals:
    void orientationChanged(float w, float x, float y, float z);
    void pitchChanged(float pitch);
    void rollChanged(float roll);
    void headingChanged(float heading);
    void altitudeChanged(float altitude);
    void accelMagnitudeChanged(float mag);
    void lateralGChanged(float g);
    void longitudinalGChanged(float g);
    void baroTempChanged(float temp);
    void connectionStatusChanged(const QString &status);
    void started();
    // retryable=false when the failure is permanent (e.g. permission denied
    // on the I2C device) and the manager must not schedule a retry.
    void stopped(bool retryable);

public slots:
    void run();

private:
    // I2C helpers
    bool openBus(bool &permissionDenied);
    void closeBus();
    bool initLSM6DSL();
    bool initLIS3MDL();
    bool initBMP388();
    bool writeByteData(int addr, int reg, int value);
    int readByteData(int addr, int reg);
    bool readBlockData(int addr, int reg, uint8_t *buf, int len);

    // Sensor reads — return false (and zero the outputs) when the I2C
    // transfer fails, so the loop can detect a dead bus.
    bool readAccel(double &ax, double &ay, double &az);
    bool readGyro(double &gx, double &gy, double &gz);
    bool readMag(double &mx, double &my, double &mz);
    void readBaro(double &pressure, double &temp, double &altitude);

    // Gyro calibration
    void calibrateGyro();

    std::atomic<bool> m_running;
    int m_fd; // I2C file descriptor

    // BMP388 calibration
    struct BMP388Calib {
        uint16_t T1;
        uint16_t T2;
        int8_t   T3;
        int16_t  P1;
        int16_t  P2;
        int8_t   P3;
        int8_t   P4;
        uint16_t P5;
        uint16_t P6;
        int8_t   P7;
        int8_t   P8;
        int16_t  P9;
        int8_t   P10;
        int8_t   P11;
    };
    BMP388Calib m_bmpCalib;

    // Gyro bias
    double m_gyroBiasX;
    double m_gyroBiasY;
    double m_gyroBiasZ;

    // Tare quaternion and accel bias
    QMutex m_tareMutex;
    std::array<double, 4> m_tareQ;
    double m_accelBiasX;
    double m_accelBiasY;
    std::atomic<bool> m_tareRequested;
    std::atomic<bool> m_tareResetRequested;

    double m_lastAx;
    double m_lastAy;

    // Emit interval
    std::atomic<double> m_emitInterval;
    std::atomic<bool> m_idle{false};

    MadgwickAHRS m_ahrs;
};

// ==================== Manager ====================

class BerryIMUManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ getConnected NOTIFY connectionStatusChanged)
    Q_PROPERTY(bool hasTemperature READ hasTemperature NOTIFY hasTemperatureChanged)

public:
    explicit BerryIMUManager(QObject *parent = nullptr);
    ~BerryIMUManager() override;

    void connect_settings_manager(SettingsManager *settingsManager);

    // Desktop BerryIMU always ships with a barometer that exposes temperature.
    bool hasTemperature() const { return true; }

signals:
    // Quaternion for 3D model (no gimbal lock)
    void orientationChanged(float w, float x, float y, float z);

    // Gauge display values
    void pitchChanged(float pitch);
    void rollChanged(float roll);
    void headingChanged(float heading);
    void altitudeChanged(float altitude);
    void accelMagnitudeChanged(float mag);
    void lateralGChanged(float g);
    void longitudinalGChanged(float g);
    void baroTempChanged(float temp);
    void connectionStatusChanged(const QString &status);
    void hasTemperatureChanged(bool has);

public slots:
    QString getConnectionStatus();
    void setEnabled(bool enabled);
    bool isEnabled();
    void setEmitRate(int hz);
    // Called by Main.qml on page changes: true while a sensor page is showing
    void setActive(bool active);
    void calibrateTare();
    void resetTare();
    void cleanup();

private slots:
    void startSensor();

private:
    bool getConnected() const;
    void scheduleRetry();
    void stopWorker();

    SettingsManager *m_settingsManager;
    QThread *m_workerThread;
    BerryIMUWorker *m_worker;
    bool m_active = true;
    // Requested emit interval (s); applied to every worker we create so a
    // setEmitRate() call before start or across a retry is not lost.
    double m_emitInterval;
    bool m_running;
    bool m_enabled;
    bool m_shuttingDown;
    int m_retryCount;
    int m_maxRetries;
    int m_retryDelayMs;
};

#else // Q_OS_MOBILE — QtSensors-backed implementation (Android)

#include <QObject>
#include <QString>

class QAccelerometer;
class QCompass;
class QPressureSensor;
class SettingsManager;

class BerryIMUManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ getConnected NOTIFY connectionStatusChanged)
    Q_PROPERTY(bool hasTemperature READ hasTemperature NOTIFY hasTemperatureChanged)

public:
    explicit BerryIMUManager(QObject *parent = nullptr);
    ~BerryIMUManager() override;

    void connect_settings_manager(SettingsManager *sm);

    bool hasTemperature() const { return m_hasTemperature; }

signals:
    void orientationChanged(float w, float x, float y, float z);
    void pitchChanged(float pitch);
    void rollChanged(float roll);
    void headingChanged(float heading);
    void altitudeChanged(float altitude);
    void accelMagnitudeChanged(float mag);
    void lateralGChanged(float g);
    void longitudinalGChanged(float g);
    void baroTempChanged(float temp);
    void connectionStatusChanged(const QString &status);
    void hasTemperatureChanged(bool has);

public slots:
    QString getConnectionStatus();
    void setEnabled(bool enabled);
    bool isEnabled();
    void setEmitRate(int hz);
    // Called by Main.qml on page changes: true while a sensor page is showing
    void setActive(bool active);
    void calibrateTare();
    void resetTare();
    void cleanup();

private slots:
    void onAccelReading();
    void onCompassReading();
    void onPressureReading();

private:
    bool getConnected() const;

    SettingsManager *m_settingsManager;
    QAccelerometer  *m_accel;
    QCompass        *m_compass;
    QPressureSensor *m_pressure;

    int    m_emitRate;
    bool   m_active = true;
    double m_tarePitch;
    double m_tareRoll;
    bool   m_enabled;
    bool   m_connected;
    bool   m_hasPressure;
    bool   m_hasTemperature;
};

#endif // Q_OS_MOBILE
#endif // BERRYIMUMANAGER_H
