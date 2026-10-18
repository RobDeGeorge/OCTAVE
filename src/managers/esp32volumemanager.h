#ifndef ESP32VOLUMEMANAGER_H
#define ESP32VOLUMEMANAGER_H

// ESP32 volume knob is USB-serial — excluded on mobile builds.
#ifndef Q_OS_MOBILE

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QMutex>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QThread>
#include <QVariantList>

// Forward declaration — avoids circular include
class SettingsManager;

class ESP32VolumeManager : public QObject
{
    Q_OBJECT
    friend class TstEsp32VolumeManager;

    Q_PROPERTY(bool connected READ getConnected NOTIFY connectionStatusChanged)
    Q_PROPERTY(QString connectionStatus READ getConnectionStatusProp NOTIFY connectionStatusChanged)
    Q_PROPERTY(QString connectionDetail READ getConnectionDetailProp NOTIFY connectionDetailChanged)
    Q_PROPERTY(QVariantList availablePorts READ getAvailablePortsProp NOTIFY availablePortsChanged)

public:
    explicit ESP32VolumeManager(SettingsManager *settingsManager = nullptr,
                                QObject *parent = nullptr);
    ~ESP32VolumeManager() override;

    void connect_settings_manager(SettingsManager *settingsManager);

    // Public methods (called from C++ side, not slots)
    void send_volume_update(int volume);
    void send_mute_state(bool muted);
    void send_theme_color(int r, int g, int b);

    // Bytes read from the knob: split into lines, run each command, and drop
    // an overlong partial line. The serial read path; public for tests.
    void feedSerialData(QByteArray data);

    // Longest partial line kept while waiting for its newline; longer is
    // dropped.
    static constexpr int ESP32_MAX_LINE_BYTES = 256;

    // The knob's USB side: Espressif's VID (0x303A) or an ESP32-S3 / USB-JTAG
    // description. Only such a port is auto-selected or trusted on open.
    static bool looksLikeEsp32S3(const QSerialPortInfo &info);

signals:
    // Connection signals
    void connectionStatusChanged(const QString &status);
    void connectionDetailChanged(const QString &detail);
    void availablePortsChanged(const QVariantList &ports);

    // Volume control signals (emitted when ESP32 sends commands)
    void volumeChangeRequested(float delta);
    void muteToggleRequested();

public slots:
    void connect_device();
    void disconnect_device();
    QVariantList get_available_ports();
    QString get_ports_with_descriptions();
    bool is_connected();
    QString get_connection_status();
    QString get_connection_detail();
    void refresh_ports();
    void reset_reconnect_attempts();
    void cleanup();

private slots:
    void onSerialReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);
    void onReconnectTimeout();
    void onScanTimeout();
    void onStartupTimeout();
    void onVolumeSendTimeout();
    void onKeepaliveTimeout();

private:
    // Property getters
    bool getConnected() const;
    QString getConnectionStatusProp() const;
    QString getConnectionDetailProp() const;
    QVariantList getAvailablePortsProp() const;

    void connectSettingsSignals();
    QString detectPlatform() const;
    QStringList scanForDevices();
    void scheduleReconnect();
    void attemptReconnect();
    void handleDisconnect();
    // True for a recognised knob command (+, -, +N, -N, M, P)
    bool processCommand(const QString &command);
    bool isEsp32S3Port(const QString &port) const;
    void markKnobVerified();
    void sendPendingVolume();
    void sendKeepalive();
    // Writes to the port and checks the result; false means the write was
    // rejected (port gone / not open) and the caller should treat the link
    // as lost.
    bool writeCommand(const QByteArray &data, const char *what);

    // Settings change handlers
    void onPortChanged();
    void onEnabledChanged();
    void onStepSizeChanged();
    void onAutoReconnectChanged();
    void onLedSleepChanged();

    // Initial connection
    void initialConnect();

    SettingsManager *m_settingsManager;
    QSerialPort *m_serialPort;
    bool m_connected;      // link verified: the only state in which we write
    bool m_awaitingKnob = false;  // port open, no knob command seen yet
    QString m_openPort;
    QString m_connectionStatus;
    QString m_connectionDetail;
    QVariantList m_availablePorts;
    QString m_esp32S3Port;
    double m_stepSize;

    QMutex m_lock;
    QString m_readBuffer;
    bool m_readOverflowWarned = false;  // warn once per connection
    bool m_readResyncing = false;       // after an overflow, skip to the next newline


    // Timers
    QTimer m_reconnectTimer;
    QTimer m_scanTimer;
    QTimer m_startupTimer;
    QTimer m_volumeSendTimer;
    QTimer m_keepaliveTimer;

    // Reconnect state
    int m_reconnectDelay;
    int m_reconnectAttempts;
    int m_maxReconnectAttempts;

    // Volume rate-limiting
    int m_pendingVolume;
    bool m_hasPendingVolume;
};

#else // Q_OS_MOBILE — mobile stub
// Every slot/signal/property below must keep the exact desktop signature:
// QML binds the same names on both builds (SettingsHubLayout,
// AccessoriesSettingsPage parse get_ports_with_descriptions() as JSON text).

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class SettingsManager;

class ESP32VolumeManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ is_connected NOTIFY connectionStatusChanged)
    Q_PROPERTY(QString connectionStatus READ get_connection_status NOTIFY connectionStatusChanged)
    Q_PROPERTY(QString connectionDetail READ get_connection_detail NOTIFY connectionDetailChanged)
    Q_PROPERTY(QVariantList availablePorts READ get_available_ports NOTIFY availablePortsChanged)

public:
    explicit ESP32VolumeManager(SettingsManager * = nullptr, QObject *parent = nullptr)
        : QObject(parent) {}
    void connect_settings_manager(SettingsManager *) {}

    void send_volume_update(int) {}
    void send_mute_state(bool) {}
    void send_theme_color(int, int, int) {}

signals:
    void connectionStatusChanged(const QString &status);
    void connectionDetailChanged(const QString &detail);
    void availablePortsChanged(const QVariantList &ports);
    void volumeChangeRequested(float delta);
    void muteToggleRequested();

public slots:
    void connect_device() {}
    void disconnect_device() {}
    QVariantList get_available_ports() { return {}; }
    QString get_ports_with_descriptions() { return QStringLiteral("[]"); }
    bool is_connected() { return false; }
    QString get_connection_status() { return QStringLiteral("Disconnected"); }
    QString get_connection_detail() { return QStringLiteral("Not available on mobile"); }
    void refresh_ports() {}
    void reset_reconnect_attempts() {}
    void cleanup() {}
};

#endif // Q_OS_MOBILE
#endif // ESP32VOLUMEMANAGER_H
