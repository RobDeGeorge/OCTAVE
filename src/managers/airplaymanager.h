/*
 * AirPlay Manager for OCTAVE
 *
 * An iPhone mirrors its screen to OCTAVE, and can play its audio through it,
 * over AirPlay: Control Center > Screen Mirroring > "OCTAVE". The receiver is
 * src/airplay/airplayreceiver (UxPlay's library, OCTAVE_HAVE_AIRPLAY). The
 * first time, the iPhone asks for the PIN shown on OCTAVE's screen; it is
 * remembered after that (<config>/airplay/state.json).
 *
 * AirPlay carries no touch back, so OCTAVE is also a Bluetooth LE mouse
 * (src/platform/bluezhidmouse, BlueZ only): paired once from the iPhone's
 * Bluetooth settings, with AssistiveTouch on, a tap on the mirrored screen
 * moves the iPhone's pointer there and clicks (src/airplay/pointerpath).
 *
 * Settings (generic keys): airPlayEnabled, airPlayName, airPlayRequirePin,
 * airPlayResolution, airPlayTouchEnabled, airPlayTouchScale.
 */

#ifndef AIRPLAYMANAGER_H
#define AIRPLAYMANAGER_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVideoSink>

#include <QLoggingCategory>

#include "../airplay/pointerpath.h"

Q_DECLARE_LOGGING_CATEGORY(lcAirPlayManager)

class AirPlayReceiver;
class BluezHidMouse;
class SettingsManager;

class AirPlayManager : public QObject
{
    Q_OBJECT
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "phone")
    Q_CLASSINFO("octave.experimental",
                "available enabled state status receiverName deviceName deviceModel pin mirroring "
                "audioStreaming frameWidth frameHeight title artist album addresses videoSink "
                "touchAvailable touchEnabled touchState touchStatus touchDevice touchPairing touchCalibrating "
                "setEnabled setTouchEnabled pairTouch injectTouch scroll calibrateTouch cancelCalibration forgetDevices "
                "enabledChanged stateChanged mediaChanged frameSizeChanged videoSinkChanged touchChanged")

    // Built with the AirPlay receiver (Linux for now)
    Q_PROPERTY(bool    available      READ available      CONSTANT)
    Q_PROPERTY(bool    enabled        READ enabled        NOTIFY enabledChanged)
    // off | unavailable | error | waiting | pin | connected | mirroring | audio;
    // status says the same for a person
    Q_PROPERTY(QString state          READ state          NOTIFY stateChanged)
    Q_PROPERTY(QString status         READ status         NOTIFY stateChanged)
    Q_PROPERTY(QString receiverName   READ receiverName   NOTIFY stateChanged)
    Q_PROPERTY(QString deviceName     READ deviceName     NOTIFY stateChanged)
    Q_PROPERTY(QString deviceModel    READ deviceModel    NOTIFY stateChanged)
    // The PIN to show while a new iPhone pairs (empty otherwise)
    Q_PROPERTY(QString pin            READ pin            NOTIFY stateChanged)
    Q_PROPERTY(bool    mirroring      READ mirroring      NOTIFY stateChanged)
    Q_PROPERTY(bool    audioStreaming READ audioStreaming NOTIFY stateChanged)
    Q_PROPERTY(int     frameWidth     READ frameWidth     NOTIFY frameSizeChanged)
    Q_PROPERTY(int     frameHeight    READ frameHeight    NOTIFY frameSizeChanged)
    // Track info sent with AirPlay audio
    Q_PROPERTY(QString title          READ title          NOTIFY mediaChanged)
    Q_PROPERTY(QString artist         READ artist         NOTIFY mediaChanged)
    Q_PROPERTY(QString album          READ album          NOTIFY mediaChanged)
    // This machine's local addresses (the iPhone must be on one of these networks)
    Q_PROPERTY(QStringList addresses  READ addresses      NOTIFY stateChanged)
    Q_PROPERTY(QObject* videoSink     READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)

    // Touch through the Bluetooth mouse. touchState: unavailable | off | waiting | connected;
    // touchPairing: the pairing window is open (pairTouch)
    Q_PROPERTY(bool    touchAvailable   READ touchAvailable   CONSTANT)
    Q_PROPERTY(bool    touchEnabled     READ touchEnabled     NOTIFY touchChanged)
    Q_PROPERTY(QString touchState       READ touchState       NOTIFY touchChanged)
    Q_PROPERTY(QString touchStatus      READ touchStatus      NOTIFY touchChanged)
    Q_PROPERTY(QString touchDevice      READ touchDevice      NOTIFY touchChanged)
    Q_PROPERTY(bool    touchPairing     READ touchPairing     NOTIFY touchChanged)
    Q_PROPERTY(bool    touchCalibrating READ touchCalibrating NOTIFY touchChanged)

public:
    explicit AirPlayManager(QObject *parent = nullptr);
    ~AirPlayManager() override;

    bool    available() const;
    bool    enabled() const { return m_enabled; }
    QString state() const { return m_state; }
    QString status() const { return m_status; }
    QString receiverName() const { return m_receiverName; }
    QString deviceName() const { return m_deviceName; }
    QString deviceModel() const { return m_deviceModel; }
    QString pin() const { return m_pin; }
    bool    mirroring() const { return m_mirroring; }
    bool    audioStreaming() const { return m_audioStreaming; }
    int     frameWidth() const { return m_frameWidth; }
    int     frameHeight() const { return m_frameHeight; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QStringList addresses() const;
    QObject *videoSink() const { return m_videoSink; }
    void    setVideoSink(QObject *sink);

    bool    touchAvailable() const;
    bool    touchEnabled() const { return m_touchEnabled; }
    QString touchState() const;
    QString touchStatus() const;
    QString touchDevice() const;
    bool    touchPairing() const;
    bool    touchCalibrating() const { return m_calibrating; }

    // Wired by AppCore
    void setSettingsManager(SettingsManager *settings);
    void setVolume(float linear);
    void setMuted(bool muted);
    void setAudioOutputDevice(const QString &id);
    // The JPEG sent with the current track (empty when none)
    QByteArray coverArt() const { return m_coverArt; }

    // Advertise the receiver (saved as airPlayEnabled)
    Q_INVOKABLE void setEnabled(bool enabled);
    // The Bluetooth mouse (saved as airPlayTouchEnabled)
    Q_INVOKABLE void setTouchEnabled(bool enabled);
    // Lets an iPhone pair with the mouse for the next few minutes (only then:
    // a stray device can't pair with the head unit); false closes it early
    Q_INVOKABLE void pairTouch(bool open = true);
    // A touch on the mirrored picture: action 0 down / 1 up / 2 move, x / y
    // in 0..1 of the picture. Only the first finger moves the pointer.
    Q_INVOKABLE void injectTouch(int pointerId, int action, double x, double y);
    // A scroll (wheel steps; positive dy scrolls down, positive dx right)
    Q_INVOKABLE void scroll(int dx, int dy);
    // Puts the pointer at a known spot; the next tap on the picture marks
    // where it shows, which sets airPlayTouchScale
    Q_INVOKABLE void calibrateTouch();
    Q_INVOKABLE void cancelCalibration();
    // Forget every iPhone that paired with the PIN
    Q_INVOKABLE void forgetDevices();

    // How far calibrateTouch() moves from the corner, in counts on each axis
    static constexpr int kCalibrationCounts = 240;
    // The calibration scale from where the pointer showed after
    // calibrateTouch() (x / y in 0..1 of a picture of `screen`); 0 when unusable
    static double scaleFromCalibration(QPointF shownAt, const QSizeF &screen);

signals:
    void enabledChanged();
    void stateChanged();
    void mediaChanged();
    void frameSizeChanged();
    void videoSinkChanged();
    void touchChanged();
    // Screen mirroring started (the UI shows the iPhone)
    void mirroringStarted();
    // AirPlay audio started or stopped (true: the user picked OCTAVE on the iPhone)
    void audioStreamingChanged(bool streaming);

private:
    QVariant setting(const QString &key, const QVariant &fallback) const;
    void applySettings();
    void startReceiver();
    void stopReceiver();
    void updateState();
    void updateMouse();
    void applyVolume();
    void loadState();
    void saveState();
    QString stateDir() const;

    void queueReports(const QList<PointerPath::Report> &reports);
    void sendNextReport();
    QSizeF pointerCounts() const;

    SettingsManager *m_settings = nullptr;
    AirPlayReceiver *m_receiver = nullptr;
    BluezHidMouse *m_mouse = nullptr;
    QPointer<QVideoSink> m_videoSink;

    bool m_enabled = false;
    bool m_touchEnabled = true;
    QString m_state = QStringLiteral("off");
    QString m_status;
    QString m_error;
    QString m_receiverName = QStringLiteral("OCTAVE");
    QString m_receiverConfig;       // name / PIN / size the receiver runs with
    QString m_deviceName;
    QString m_deviceModel;
    QString m_pin;
    bool m_mirroring = false;
    bool m_audioStreaming = false;
    int m_connections = 0;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    QString m_title, m_artist, m_album;
    QByteArray m_coverArt;

    float m_volume = 1.0f;
    bool m_muted = false;
    double m_senderVolume = 1.0;
    QString m_audioDeviceId;

    QString m_deviceId;             // the receiver's own id (stable, state.json)
    QVariantList m_clients;         // [{publicKey, deviceId, name}] paired with the PIN

    // Pointer: where it is (0..1 of the screen) when known
    bool m_pointerKnown = false;
    QPointF m_pointer;
    QPointF m_carry;
    int m_activePointer = -1;
    bool m_calibrating = false;
    QList<PointerPath::Report> m_reports;
    QTimer m_reportTimer;
    QTimer m_pairingTimer;
};

#endif // AIRPLAYMANAGER_H
