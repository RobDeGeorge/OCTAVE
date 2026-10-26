#ifndef AIRPLAYRECEIVER_H
#define AIRPLAYRECEIVER_H

// OCTAVE as an AirPlay receiver: an iPhone (or iPad, or Mac) picks OCTAVE
// under Screen Mirroring or as an audio output, and its screen and sound
// come here. The protocol is UxPlay's library (third_party/uxplay/: RTSP,
// pairing, FairPlay, the RTP streams, its own mDNS responder); this class
// does what UxPlay's GStreamer renderers did: H.264 through the shared
// decoder onto a QVideoSink, ALAC / AAC through libavcodec into a
// QAudioSink. UxPlay calls back on its own threads; every signal here is
// emitted on the GUI thread.

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVideoFrame>

#include <atomic>
#include <memory>
#include <mutex>

class QAudioSink;
class QIODevice;
class QVideoSink;
class H264Decoder;
class AirPlayAudioDecoder;

struct raop_s;
struct dnssd_s;

class AirPlayReceiver : public QObject
{
    Q_OBJECT
public:
    struct Options {
        QString name = QStringLiteral("OCTAVE");   // what the iPhone lists
        QString deviceId;           // "aa:bb:cc:dd:ee:ff": the receiver's identity, keep it stable
        QString keyFile;            // the receiver's pairing key (PEM), created when missing
        bool requirePin = true;     // a new iPhone types the PIN shown on screen once
        int width = 1280;           // the display size and rate offered to the sender
        int height = 720;
        int fps = 30;
    };

    explicit AirPlayReceiver(QObject *parent = nullptr);
    ~AirPlayReceiver() override;

    // Starts the server and advertises it on the local network
    bool start(const Options &options, QString *error = nullptr);
    void stop();
    bool isRunning() const { return m_raop != nullptr; }
    quint16 port() const { return m_port; }

    void setVideoSink(QVideoSink *sink);
    // OCTAVE's own volume (linear 0..1, after the curve); the sender's volume
    // comes on top of it (senderVolumeChanged)
    void setVolume(float linear);
    void setAudioOutputDevice(const QString &id);
    // Public keys (base64) of the senders that paired with the PIN before
    void setRegisteredKeys(const QStringList &keys);

    // Frames decoded so far (tests, diagnostics)
    int frameCount() const { return m_frameCount.load(); }

    // A DMAP block (AirPlay's track metadata) to title / artist / album
    static void parseMetadata(const QByteArray &dmap, QString *title, QString *artist, QString *album);
    // The AirPlay volume (-30..0 dB, -144 mute) as a slider fraction 0..1
    static double volumeFraction(float airplayDb);

signals:
    // A sender asked to connect (it is admitted); deviceId is its own id
    void clientConnecting(const QString &name, const QString &model, const QString &deviceId);
    void connectionsChanged(int open);
    void mirroringChanged(bool mirroring);
    void audioStreamingChanged(bool streaming);
    void frameSizeChanged(int width, int height);
    // The sender shows a PIN field: show this PIN on OCTAVE's screen
    void pinRequested(const QString &pin);
    // A sender paired with the PIN: remember its key (setRegisteredKeys)
    void clientRegistered(const QString &publicKey, const QString &deviceId, const QString &name);
    void senderVolumeChanged(double fraction);
    void metadataChanged(const QString &title, const QString &artist, const QString &album);
    void coverArtChanged(const QByteArray &jpeg);
    // The network went away under a session
    void connectionLost();

private:
    friend struct AirPlayCallbacks;

    // UxPlay threads
    void onVideo(const unsigned char *data, int length);
    void onVideoReset();
    void onAudioFormat(int ct);
    void onAudio(const unsigned char *data, int length);
    bool isRegistered(const QString &key) const;

    // GUI thread
    void deliverFrame();
    void playPcm(const QByteArray &pcm);
    void drainAudio();
    void closeAudio();
    void setConnections(int open);

    raop_s *m_raop = nullptr;
    dnssd_s *m_dnssd = nullptr;
    quint16 m_port = 0;
    bool m_requirePin = true;

    // Video (decoded on UxPlay's mirror thread)
    std::mutex m_videoMutex;
    std::unique_ptr<H264Decoder> m_decoder;
    std::mutex m_frameMutex;
    QVideoFrame m_latestFrame;
    std::atomic<bool> m_framePending{false};
    std::atomic<int> m_frameCount{0};
    QSize m_frameSize;
    QPointer<QVideoSink> m_sink;

    // Audio (decoded on UxPlay's audio thread, played on the GUI thread)
    std::mutex m_audioMutex;
    std::unique_ptr<AirPlayAudioDecoder> m_audioDecoder;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioIo = nullptr;
    QAudioFormat m_audioFormat;
    QQueue<QByteArray> m_audioQueue;
    qsizetype m_audioQueued = 0;
    QTimer m_audioTimer;
    bool m_audioWarned = false;
    bool m_audioStreaming = false;
    float m_volume = 1.0f;
    QString m_audioDeviceId;

    mutable std::mutex m_keysMutex;
    QSet<QString> m_registeredKeys;
    std::atomic<int> m_connections{0};
};

#endif // AIRPLAYRECEIVER_H
