#ifndef TRIPSYNC_H
#define TRIPSYNC_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#include "../service/staticfiles.h"

class QNetworkAccessManager;
class QNetworkReply;
class TripRecorder;

// ===========================================================================
// TripSync -- the black box's drives from one OCTAVE to another.
//
// A drive travels as the CSV + JSON pair TripStore writes when it ends, and
// arrives by being put in the receiver's inbox (TripStore::importInbox).
// Two ways get a pair there:
//
//   Pull, OCTAVE to OCTAVE. This OCTAVE pairs with another one's service
//   (the 6-digit code, scope `files`), asks it for its drives
//   (GET /trips, served by serve() below) and fetches the pairs it has not
//   had into its own inbox. The phone pulls from the car, a server from
//   the phone or the car.
//
//   Push over adb. While a phone is mirrored, the car's OCTAVE copies the
//   pairs that phone has not had into the inbox of the OCTAVE app on it
//   (kPhoneHistoryDir), app running or not, and leaves a note of its own
//   address there (car.json) so the app can offer to pair. The shutdown
//   button ends the drive and pushes it before the power goes
//   (finishBeforePowerOff). For a car computer that is cut off with no
//   shutdown, the drive being recorded can go in parts while it lasts
//   (snapshotNow, TripStore::writeSnapshot; off by default).
//
// Only closed drives travel (but for those parts), never one of the simulated car. A drive that
// has been here once is not fetched again, so one deleted or pruned here
// stays gone. Peers, their tokens and both "already had" lists are in
// <config>/trip-sync.json (owner-only).
//
// GUI thread. The adb work runs on a pool thread.
// ===========================================================================
class TripSync : public QObject
{
    Q_OBJECT
    // Service permissions (src/service/servicescopes.h): pairing this
    // OCTAVE with another one is the owner's to do, like pairing a device
    Q_CLASSINFO("octave.scope", "full")
    Q_CLASSINFO("octave.scope.files", "syncNow")
    Q_CLASSINFO("octave.experimental", "*")

    // The OCTAVEs this one pulls drives from:
    // [{id, name, host, port, lastSyncMs, lastError, drives}] (no tokens)
    Q_PROPERTY(QVariantList peers READ peers NOTIFY peersChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // One line for the Settings card: what is going on, or how the last run went
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // Pull by itself: soon after start, every few minutes, when the app comes to the front
    Q_PROPERTY(bool autoSync READ autoSync WRITE setAutoSync NOTIFY autoSyncChanged)
    // Copy drives to a mirrored phone over adb
    Q_PROPERTY(bool pushToPhone READ pushToPhone WRITE setPushToPhone NOTIFY pushToPhoneChanged)
    // An OCTAVE that left its address on this phone over adb and is not
    // paired yet: {name, host, port}, empty when none
    Q_PROPERTY(QVariantMap carHint READ carHint NOTIFY carHintChanged)

public:
    static constexpr int kDefaultPort = 9876;
    static constexpr int kAutoIntervalMs = 5 * 60 * 1000;
    static constexpr int kMaxRemembered = 20000;      // ids per "already had" list
    static constexpr qint64 kMaxCsvBytes = 512LL * 1024 * 1024;
    // The OCTAVE app's own folder on a phone, which adb may write to
    static constexpr const char *kPhoneHistoryDir = "/sdcard/Android/data/org.octave.app/files/history";
    static constexpr const char *kPhonePackage = "org.octave.app";

    explicit TripSync(TripRecorder *recorder, QObject *parent = nullptr);
    ~TripSync() override;

    // Where trip-sync.json is; default <app data>. Call before start().
    void setConfigDir(const QString &dir);
    // Read the state, add the phone's adb-reachable inbox (Android), start
    // the timer. `externalHistoryDir`: tests name the folder adb would
    // write to; empty = the platform's (Android only).
    void start(const QString &externalHistoryDir = QString());

    QVariantList peers() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    bool autoSync() const { return m_autoSync; }
    void setAutoSync(bool on);
    bool pushToPhone() const { return m_pushToPhone; }
    void setPushToPhone(bool on);
    QVariantMap carHint() const { return m_carHint; }

    // Pair with the OCTAVE at host[:port] using the code its Settings >
    // Service card (or `octave --pair-code`) shows -> pairFinished
    Q_INVOKABLE void addPeer(const QString &host, int port, const QString &code);
    Q_INVOKABLE void removePeer(const QString &id);
    // Pull from every peer now -> syncFinished
    Q_INVOKABLE void syncNow();

    // --- wiring (AppCore) ---
    // The mirrored phone: adb and its serial, "" when none is connected
    void setPhone(const QString &adbPath, const QString &serial);
    // Where this OCTAVE's own service is reached ("http://host:port",
    // network addresses first; empty unless it listens beyond loopback),
    // for the note left on the phone
    void setServiceUrls(const QStringList &urls, bool onNetwork);
    // Push the finished drives to the phone now (also after a drive ends
    // and when a phone connects) -> pushFinished. False when nothing was
    // started (no phone, switched off, nothing to send, a push running).
    bool pushNow();
    // The shutdown button (NetworkManager::setBeforePowerOff): end the
    // drive being recorded and get it on the mirrored phone, then call
    // `done`; at once when no phone is mirrored, after kPowerOffPushMs at
    // the latest
    static constexpr int kPowerOffPushMs = 25000;
    void finishBeforePowerOff(std::function<void()> done);
    // Push what is new of the drive being recorded to the phone now ->
    // pushFinished when something went. For a car computer that loses
    // power with no shutdown: off unless trip-sync.json has
    // "snapshotMinutes" > 0, which runs it on that interval.
    void snapshotNow();

    // --- the serving side ---
    // GET /trips (main.cpp's route): with no id, the closed real drives of
    // `trips` (TripRecorder::trips) as JSON [{id, startMs, endMs,
    // sampleCount, vin}]; with ?id=N&format=json|csv that drive's file.
    // format=csv&z=1 sends the CSV through qCompress (a tenth of the
    // size). A path is only ever one the trip list holds.
    static OctaveService::StaticFiles::Reply serve(const QVariantList &trips, const QByteArray &target);

    // The steps of one adb push, blocking (pool thread; tests call it).
    // `pairs`: (id, csv path, json path). Returns the ids that arrived;
    // *note says why none did.
    // `name`: what the pair is called on the phone, the id when empty.
    struct Pair { qint64 id = 0; QString csv, json, name; };
    static QList<qint64> pushOverAdb(const QString &adbPath, const QString &serial, const QList<Pair> &pairs,
                                     const QByteArray &carNote, QString *note);

signals:
    void peersChanged();
    void busyChanged();
    void statusChanged();
    void autoSyncChanged();
    void pushToPhoneChanged();
    void carHintChanged();
    void pairFinished(bool ok, const QString &error);
    // A pull ended: drives taken in, and the peers that could not be reached
    void syncFinished(int imported, int failedPeers);
    // An adb push ended: drives that reached the phone
    void pushFinished(int pushed, const QString &note);

private:
    struct Peer {
        QString id, name, host, token, lastError;
        int port = kDefaultPort;
        qint64 lastSyncMs = 0;
        int drives = 0;   // taken from it so far
    };
    void load();
    void save();
    void setBusy(bool busy);
    void setStatus(const QString &status);
    void remember(QList<qint64> &list, qint64 id);
    QNetworkReply *get(const Peer &peer, const QString &query, int timeoutMs);
    void nextPeer();
    void listArrived(QNetworkReply *reply);
    void nextTrip();
    void peerFailed(const QString &why);
    void peerDone();
    void finishSync();
    void readCarHint();
    Peer *currentPeer();

    TripRecorder *m_recorder;
    QNetworkAccessManager *m_net = nullptr;
    QString m_configDir, m_externalHistoryDir;
    bool m_started = false;
    QList<Peer> m_peers;
    QList<qint64> m_had;                         // ids that have been here once, oldest first
    QSet<qint64> m_hadSet;
    QHash<QString, QList<qint64>> m_pushed;      // phone serial -> ids it has
    bool m_autoSync = true;
    bool m_pushToPhone = true;
    bool m_busy = false;
    QString m_status;
    QVariantMap m_carHint;
    QTimer m_autoTimer;

    // One pull
    QStringList m_queue;        // peer ids still to do
    QString m_currentId;
    QList<qint64> m_wanted;     // drives to fetch from the current peer
    int m_fetched = 0;          // pairs put in the inbox this run
    int m_failedPeers = 0;
    bool m_pairing = false;

    // adb
    QString m_adbPath, m_serial;
    QStringList m_serviceUrls;
    bool m_pushing = false;
    QTimer m_pushTimer;
    QTimer m_snapshotTimer;
    int m_snapshotMinutes = 0;     // 0: no parts, the drive goes when it ends
    qint64 m_snapshotTrip = 0;     // the drive the two below are about
    qint64 m_snapshotSince = 0;    // its last sample the phone has
    int m_snapshotSeq = 0;
    QString m_snapshotSerial;
};

#endif // TRIPSYNC_H
