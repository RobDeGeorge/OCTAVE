#ifndef GESTUREMANAGER_H
#define GESTUREMANAGER_H

#include "gesturevocabulary.h"

// PAJ7620U2 gesture sensor uses Linux I2C — excluded on mobile builds.
#ifndef Q_OS_MOBILE

#include <QObject>
#include <QString>
#include <QMap>
#include <QTimer>
#include <QMutex>
#include <QThread>
#include <atomic>

// Forward declaration
class SettingsManager;

// ==================== I2C Worker Thread ====================

class GestureWorker : public QObject
{
    Q_OBJECT

public:
    explicit GestureWorker(QObject *parent = nullptr);
    ~GestureWorker() override;

    void stop();

    // Thread-safe configuration
    void setCooldown(double seconds);
    void setGestureMapping(const QMap<QString, QString> &mapping);
    QMap<QString, QString> gestureMapping() const;

signals:
    void gestureDetected(const QString &gesture);
    void actionTriggered(const QString &action);
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
    bool writeByteData(int addr, int reg, int value);
    int readByteData(int addr, int reg);
    bool readBlockData(int addr, int reg, uint8_t *buf, int len);

    // Sensor init
    bool wakeup();
    bool readChipId(uint16_t &chipId);
    bool initRegisters();

    // Gesture reading — readGestureRaw returns false (flags zeroed) when the
    // I2C transfer fails, so the loop can detect a dead bus.
    bool readGestureRaw(uint8_t &flag0, uint8_t &flag1);
    QString decodeGesture(uint8_t flag0, uint8_t flag1) const;
    void flushSensor(double duration);

    std::atomic<bool> m_running;
    std::atomic<bool> m_stopRequested{false};   // sticky: a stop() during init must win
    int m_fd;

    mutable QMutex m_configMutex;
    double m_cooldown;
    QMap<QString, QString> m_gestureMapping;
};

// ==================== Manager ====================

class GestureManager : public QObject
{
    Q_OBJECT
    // Read by AccessoriesWidget.qml; mirrors the mobile stub's property.
    Q_PROPERTY(QString connectionStatus READ getConnectionStatus NOTIFY connectionStatusChanged)
    // The gestures the sensor reports and the actions they can map to
    // ({key, label}); see gesturevocabulary.h
    Q_PROPERTY(QStringList gestures READ gestures CONSTANT)
    Q_PROPERTY(QVariantList actions READ actions CONSTANT)

public:
    explicit GestureManager(QObject *parent = nullptr);
    ~GestureManager() override;

    void connect_settings_manager(SettingsManager *settingsManager);
    QStringList gestures() const { return GestureVocabulary::gestures(); }
    QVariantList actions() const { return GestureVocabulary::actions(); }

signals:
    void gestureDetected(const QString &gesture);
    void actionTriggered(const QString &action);
    void connectionStatusChanged(const QString &status);

public slots:
    QString getConnectionStatus();
    void setGestureAction(const QString &gesture, const QString &action);
    QString getGestureAction(const QString &gesture);
    void resetMappingToDefaults();
    // Runtime switches; they do not save. connect_settings_manager() makes the
    // gestureSensorEnabled / gestureCooldown settings drive them.
    void setEnabled(bool enabled);
    void setCooldown(int ms);
    bool isEnabled();
    int cooldownMs() const { return qRound(m_cooldown * 1000.0); }
    void cleanup();

private slots:
    void startSensor();

private:
    void stopWorker();
    void scheduleRetry();

    static QMap<QString, QString> defaultGestureMapping();

    SettingsManager *m_settingsManager;
    QThread *m_workerThread;
    GestureWorker *m_worker;
    bool m_running;
    bool m_enabled;
    bool m_shuttingDown;
    int m_retryCount;
    int m_maxRetries;
    int m_retryDelayMs;
    double m_cooldown;
    QMap<QString, QString> m_gestureMapping;
};

#else // Q_OS_MOBILE — mobile stub
// Every slot/signal/property below must keep the exact desktop signature:
// QML binds the same names on both builds.

#include <QObject>
#include <QString>

class SettingsManager;

class GestureManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString connectionStatus READ getConnectionStatus NOTIFY connectionStatusChanged)
    // The gestures the sensor reports and the actions they can map to
    // ({key, label}); see gesturevocabulary.h
    Q_PROPERTY(QStringList gestures READ gestures CONSTANT)
    Q_PROPERTY(QVariantList actions READ actions CONSTANT)
public:
    explicit GestureManager(QObject *parent = nullptr) : QObject(parent) {}
    QStringList gestures() const { return GestureVocabulary::gestures(); }
    QVariantList actions() const { return GestureVocabulary::actions(); }
    void connect_settings_manager(SettingsManager *) {}

signals:
    void gestureDetected(const QString &gesture);
    void actionTriggered(const QString &action);
    void connectionStatusChanged(const QString &status);

public slots:
    QString getConnectionStatus() { return QStringLiteral("Disabled on mobile"); }
    void setGestureAction(const QString &, const QString &) {}
    QString getGestureAction(const QString &) { return QStringLiteral("none"); }
    void resetMappingToDefaults() {}
    void setEnabled(bool) {}
    void setCooldown(int) {}
    bool isEnabled() { return false; }
    int cooldownMs() const { return 0; }
    void cleanup() {}
};

#endif // Q_OS_MOBILE
#endif // GESTUREMANAGER_H
