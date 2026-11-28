#include "tripsync.h"

#include "settingsmanager.h"
#include "triprecorder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#if QT_CONFIG(process)
#include <QProcess>
#endif
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>
#include <memory>

Q_LOGGING_CATEGORY(lcSync, "octave.tripsync")

using OctaveService::StaticFiles::Reply;

namespace {
const int kPushBatch = 100;   // drives per adb run

bool travels(const QVariantMap &trip)
{
    // Not the one being recorded, not a part of one still arriving
    return !trip.value(QStringLiteral("open")).toBool() && !trip.value(QStringLiteral("simulated")).toBool()
           && !trip.value(QStringLiteral("partial")).toBool()
           && trip.value(QStringLiteral("endMs")).isValid() && !trip.value(QStringLiteral("file")).toString().isEmpty();
}

Reply plain(int status, const QByteArray &reason, const QByteArray &body)
{
    return Reply{status, reason, "text/plain", body, {}};
}
} // namespace

// ---------------------------------------------------------------------------
// The serving side
// ---------------------------------------------------------------------------

Reply TripSync::serve(const QVariantList &trips, const QByteArray &target)
{
    using namespace OctaveService;
    const QString idText = StaticFiles::queryValue(target, QStringLiteral("id"));
    if (idText.isNull()) {
        QJsonArray list;
        for (const QVariant &v : trips) {
            const QVariantMap t = v.toMap();
            if (!travels(t))
                continue;
            list.append(QJsonObject{{QStringLiteral("id"), t.value(QStringLiteral("id")).toLongLong()},
                                    {QStringLiteral("startMs"), t.value(QStringLiteral("startMs")).toLongLong()},
                                    {QStringLiteral("endMs"), t.value(QStringLiteral("endMs")).toLongLong()},
                                    {QStringLiteral("sampleCount"), t.value(QStringLiteral("sampleCount")).toLongLong()},
                                    {QStringLiteral("vin"), t.value(QStringLiteral("vin")).toString()}});
        }
        return Reply{200, "OK", "application/json", QJsonDocument(list).toJson(QJsonDocument::Compact), {}};
    }
    bool ok = false;
    const qint64 id = idText.toLongLong(&ok);
    QVariantMap trip;
    for (const QVariant &v : trips) {
        const QVariantMap t = v.toMap();
        if (ok && t.value(QStringLiteral("id")).toLongLong() == id && travels(t)) {
            trip = t;
            break;
        }
    }
    if (trip.isEmpty())
        return plain(404, "Not Found", "no such drive\n");
    const QString format = StaticFiles::queryValue(target, QStringLiteral("format"));
    if (format == QLatin1String("json"))
        return StaticFiles::serveDownload(trip.value(QStringLiteral("summaryFile")).toString());
    const QString csv = trip.value(QStringLiteral("file")).toString();
    if (StaticFiles::queryValue(target, QStringLiteral("z")) != QLatin1String("1"))
        return StaticFiles::serveDownload(csv, kMaxCsvBytes);
    QFile f(csv);
    if (!f.open(QIODevice::ReadOnly))
        return plain(404, "Not Found", "no such drive\n");
    if (f.size() > kMaxCsvBytes)
        return plain(413, "Content Too Large", "drive too large\n");
    return Reply{200, "OK", "application/octet-stream", qCompress(f.readAll(), 6), {}};
}

// ---------------------------------------------------------------------------
// Set-up and state
// ---------------------------------------------------------------------------

TripSync::TripSync(TripRecorder *recorder, QObject *parent)
    : QObject(parent)
    , m_recorder(recorder)
    , m_configDir(SettingsManager::getAppDataDir())
{
    m_autoTimer.setInterval(kAutoIntervalMs);
    connect(&m_autoTimer, &QTimer::timeout, this, [this] {
        if (m_autoSync && !m_peers.isEmpty())
            syncNow();
    });
    m_pushTimer.setSingleShot(true);
    connect(&m_pushTimer, &QTimer::timeout, this, &TripSync::pushNow);
    connect(&m_snapshotTimer, &QTimer::timeout, this, &TripSync::snapshotNow);
}

TripSync::~TripSync() = default;

void TripSync::setConfigDir(const QString &dir)
{
    if (!m_started)
        m_configDir = dir;
}

void TripSync::start(const QString &externalHistoryDir)
{
    if (m_started)
        return;
    m_started = true;
    m_net = new QNetworkAccessManager(this);
    load();

    m_externalHistoryDir = externalHistoryDir;
#ifdef Q_OS_ANDROID
    if (m_externalHistoryDir.isEmpty()) {
        // The app's own folder on shared storage: the one adb can write to
        const QStringList places = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
        for (const QString &p : places) {
            if (p.contains(QLatin1String("/Android/data/"))) {
                m_externalHistoryDir = p + QStringLiteral("/history");
                break;
            }
        }
        if (m_externalHistoryDir.isEmpty())
            m_externalHistoryDir = QString::fromLatin1(kPhoneHistoryDir)
                                       .replace(QLatin1String("/sdcard/"), QLatin1String("/storage/emulated/0/"));
        qCInfo(lcSync) << "Drives pushed over adb arrive in" << m_externalHistoryDir + QStringLiteral("/inbox");
    }
#endif
    if (m_recorder) {
        if (!m_externalHistoryDir.isEmpty())
            m_recorder->addInboxDir(m_externalHistoryDir + QStringLiteral("/inbox"));
        // A drive ended here: the mirrored phone gets it
        connect(m_recorder, &TripRecorder::tripEnded, this, [this] {
            if (!m_serial.isEmpty())
                m_pushTimer.start(5000);
        });
    }
    readCarHint();

    m_autoTimer.start();
    if (m_snapshotMinutes > 0)
        m_snapshotTimer.start(m_snapshotMinutes * 60 * 1000);
    QTimer::singleShot(15000, this, [this] {
        if (m_autoSync && !m_peers.isEmpty())
            syncNow();
    });
    // The app comes to the front (a phone taken out in the car): look in
    // the inbox and ask the peers
    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(gui, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive)
                return;
            if (m_recorder && !m_externalHistoryDir.isEmpty())
                m_recorder->importInbox();
            readCarHint();
            if (m_autoSync && !m_peers.isEmpty())
                syncNow();
        });
    }
}

void TripSync::load()
{
    QFile f(m_configDir + QStringLiteral("/trip-sync.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_autoSync = o.value(QLatin1String("autoSync")).toBool(true);
    m_pushToPhone = o.value(QLatin1String("pushToPhone")).toBool(true);
    m_snapshotMinutes = qBound(0, o.value(QLatin1String("snapshotMinutes")).toInt(0), 60);
    const QJsonArray peers = o.value(QLatin1String("peers")).toArray();
    for (const QJsonValue &v : peers) {
        const QJsonObject p = v.toObject();
        Peer peer;
        peer.id = p.value(QLatin1String("id")).toString();
        peer.name = p.value(QLatin1String("name")).toString();
        peer.host = p.value(QLatin1String("host")).toString();
        peer.port = p.value(QLatin1String("port")).toInt(kDefaultPort);
        peer.token = p.value(QLatin1String("token")).toString();
        peer.lastSyncMs = p.value(QLatin1String("lastSyncMs")).toInteger();
        peer.drives = p.value(QLatin1String("drives")).toInt();
        if (!peer.id.isEmpty() && !peer.host.isEmpty() && !peer.token.isEmpty())
            m_peers.append(peer);
    }
    const QJsonArray had = o.value(QLatin1String("had")).toArray();
    for (const QJsonValue &v : had)
        remember(m_had, v.toInteger());
    const QJsonObject pushed = o.value(QLatin1String("pushed")).toObject();
    for (auto it = pushed.constBegin(); it != pushed.constEnd(); ++it) {
        QList<qint64> ids;
        const QJsonArray a = it.value().toArray();
        for (const QJsonValue &v : a)
            ids.append(v.toInteger());
        m_pushed.insert(it.key(), ids);
    }
}

void TripSync::save()
{
    QJsonArray peers;
    for (const Peer &p : std::as_const(m_peers)) {
        peers.append(QJsonObject{{QStringLiteral("id"), p.id},
                                 {QStringLiteral("name"), p.name},
                                 {QStringLiteral("host"), p.host},
                                 {QStringLiteral("port"), p.port},
                                 {QStringLiteral("token"), p.token},
                                 {QStringLiteral("lastSyncMs"), p.lastSyncMs},
                                 {QStringLiteral("drives"), p.drives}});
    }
    QJsonArray had;
    for (qint64 id : std::as_const(m_had))
        had.append(id);
    QJsonObject pushed;
    for (auto it = m_pushed.constBegin(); it != m_pushed.constEnd(); ++it) {
        QJsonArray a;
        for (qint64 id : it.value())
            a.append(id);
        pushed.insert(it.key(), a);
    }
    const QJsonObject o{{QStringLiteral("autoSync"), m_autoSync},
                        {QStringLiteral("pushToPhone"), m_pushToPhone},
                        {QStringLiteral("snapshotMinutes"), m_snapshotMinutes},
                        {QStringLiteral("peers"), peers},
                        {QStringLiteral("had"), had},
                        {QStringLiteral("pushed"), pushed}};
    QDir().mkpath(m_configDir);
    QSaveFile f(m_configDir + QStringLiteral("/trip-sync.json"));
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(lcSync) << "Cannot write" << f.fileName() << f.errorString();
        return;
    }
    // It holds the tokens of the paired OCTAVEs
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.commit();
}

void TripSync::remember(QList<qint64> &list, qint64 id)
{
    if (id <= 0)
        return;
    if (&list == &m_had) {
        if (m_hadSet.contains(id))
            return;
        m_hadSet.insert(id);
    } else if (list.contains(id)) {
        return;
    }
    list.append(id);
    while (list.size() > kMaxRemembered) {
        if (&list == &m_had)
            m_hadSet.remove(list.first());
        list.removeFirst();
    }
}

QVariantList TripSync::peers() const
{
    QVariantList out;
    for (const Peer &p : m_peers) {
        out.append(QVariantMap{{QStringLiteral("id"), p.id},
                               {QStringLiteral("name"), p.name},
                               {QStringLiteral("host"), p.host},
                               {QStringLiteral("port"), p.port},
                               {QStringLiteral("lastSyncMs"), p.lastSyncMs},
                               {QStringLiteral("lastError"), p.lastError},
                               {QStringLiteral("drives"), p.drives}});
    }
    return out;
}

void TripSync::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void TripSync::setStatus(const QString &status)
{
    if (status == m_status)
        return;
    m_status = status;
    emit statusChanged();
}

void TripSync::setAutoSync(bool on)
{
    if (on == m_autoSync)
        return;
    m_autoSync = on;
    save();
    emit autoSyncChanged();
}

void TripSync::setPushToPhone(bool on)
{
    if (on == m_pushToPhone)
        return;
    m_pushToPhone = on;
    save();
    emit pushToPhoneChanged();
    if (on)
        m_pushTimer.start(1000);
}

// ---------------------------------------------------------------------------
// Pairing with another OCTAVE
// ---------------------------------------------------------------------------

void TripSync::addPeer(const QString &hostText, int port, const QString &codeText)
{
    QString host = hostText.trimmed();
    host.remove(QStringLiteral("http://"));
    while (host.endsWith(QLatin1Char('/')))
        host.chop(1);
    // "host:port" typed in one field (not a bare IPv6 address)
    const int colon = host.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && host.count(QLatin1Char(':')) == 1) {
        bool ok = false;
        const int p = host.mid(colon + 1).toInt(&ok);
        if (ok)
            port = p;
        host = host.left(colon);
    }
    if (port <= 0 || port > 65535)
        port = kDefaultPort;
    QString code = codeText;
    code.remove(QLatin1Char(' '));
    if (host.isEmpty() || code.isEmpty()) {
        emit pairFinished(false, QStringLiteral("An address and the 6-digit code are needed"));
        return;
    }
    if (m_pairing || m_busy) {
        emit pairFinished(false, QStringLiteral("Busy, try again in a moment"));
        return;
    }
    m_pairing = true;
    setStatus(QStringLiteral("Pairing with %1…").arg(host));

    auto *socket = new QTcpSocket(this);
    auto *timeout = new QTimer(socket);
    timeout->setSingleShot(true);
    auto finish = [this, socket, host, port](const QString &token, const QString &error) {
        if (!m_pairing)
            return;
        m_pairing = false;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
        if (token.isEmpty()) {
            qCWarning(lcSync) << "Pairing with" << host << port << "failed:" << error;
            setStatus(error);
            emit pairFinished(false, error);
            return;
        }
        Peer peer;
        for (int i = 0; i < m_peers.size(); ++i) {
            if (m_peers.at(i).host == host && m_peers.at(i).port == port)
                peer = m_peers.takeAt(i--);
        }
        if (peer.id.isEmpty())
            peer.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        peer.host = host;
        peer.port = port;
        peer.name = !m_carHint.value(QStringLiteral("name")).toString().isEmpty()
                            && m_carHint.value(QStringLiteral("host")).toString() == host
                        ? m_carHint.value(QStringLiteral("name")).toString()
                        : host;
        peer.token = token;
        peer.lastError.clear();
        m_peers.append(peer);
        save();
        qCInfo(lcSync) << "Paired with the OCTAVE at" << host << port;
        emit peersChanged();
        readCarHint();
        setStatus(QStringLiteral("Paired with %1").arg(peer.name));
        emit pairFinished(true, QString());
        syncNow();
    };
    connect(timeout, &QTimer::timeout, this, [finish] { finish(QString(), QStringLiteral("No answer from that address")); });
    connect(socket, &QTcpSocket::errorOccurred, this, [finish, socket] {
        finish(QString(), QStringLiteral("Could not connect: %1").arg(socket->errorString()));
    });
    connect(socket, &QTcpSocket::connected, this, [socket, code] {
        const QJsonObject request{{QStringLiteral("id"), 1},
                                  {QStringLiteral("op"), QStringLiteral("pair")},
                                  {QStringLiteral("code"), code},
                                  {QStringLiteral("name"), QStringLiteral("OCTAVE on %1").arg(QSysInfo::machineHostName())},
                                  {QStringLiteral("scopes"), QJsonArray{QStringLiteral("files")}}};
        socket->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    });
    connect(socket, &QTcpSocket::readyRead, this, [finish, socket] {
        while (socket->canReadLine()) {
            const QJsonObject reply = QJsonDocument::fromJson(socket->readLine()).object();
            if (reply.value(QLatin1String("id")).toInt() != 1)
                continue;   // an event line
            const QString token = reply.value(QLatin1String("result")).toObject().value(QLatin1String("token")).toString();
            QString error = reply.value(QLatin1String("error")).toString();
            if (error.isEmpty())
                error = reply.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString();
            if (token.isEmpty() && error.isEmpty())
                error = QStringLiteral("That OCTAVE refused the code");
            finish(token, error);
            return;
        }
    });
    timeout->start(10000);
    socket->connectToHost(host, quint16(port));
}

void TripSync::removePeer(const QString &id)
{
    for (int i = 0; i < m_peers.size(); ++i) {
        if (m_peers.at(i).id != id)
            continue;
        m_peers.removeAt(i);
        m_queue.removeAll(id);
        save();
        emit peersChanged();
        readCarHint();
        return;
    }
}

// ---------------------------------------------------------------------------
// Pull
// ---------------------------------------------------------------------------

TripSync::Peer *TripSync::currentPeer()
{
    for (Peer &p : m_peers) {
        if (p.id == m_currentId)
            return &p;
    }
    return nullptr;
}

QNetworkReply *TripSync::get(const Peer &peer, const QString &query, int timeoutMs)
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(peer.host);
    url.setPort(peer.port);
    url.setPath(QStringLiteral("/trips"));
    if (!query.isEmpty())
        url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + peer.token.toLatin1());
    request.setTransferTimeout(timeoutMs);
    return m_net->get(request);
}

void TripSync::syncNow()
{
    if (!m_started || m_busy || m_pairing)
        return;
    if (m_peers.isEmpty()) {
        setStatus(QStringLiteral("No other OCTAVE paired"));
        emit syncFinished(0, 0);
        return;
    }
    // What is here now has been here: never fetched, and not fetched again
    // once it is deleted
    if (m_recorder) {
        const QVariantList trips = m_recorder->trips();
        for (const QVariant &t : trips) {
            // (a part of a drive is not the drive: the whole one is still wanted)
            if (!t.toMap().value(QStringLiteral("partial")).toBool())
                remember(m_had, t.toMap().value(QStringLiteral("id")).toLongLong());
        }
    }
    m_queue.clear();
    for (const Peer &p : std::as_const(m_peers))
        m_queue.append(p.id);
    m_fetched = 0;
    m_failedPeers = 0;
    setBusy(true);
    nextPeer();
}

void TripSync::nextPeer()
{
    m_wanted.clear();
    m_currentId.clear();
    while (!m_queue.isEmpty() && m_currentId.isEmpty()) {
        m_currentId = m_queue.takeFirst();
        if (!currentPeer())
            m_currentId.clear();   // removed meanwhile
    }
    Peer *peer = currentPeer();
    if (!peer) {
        finishSync();
        return;
    }
    setStatus(QStringLiteral("Asking %1…").arg(peer->name));
    QNetworkReply *reply = get(*peer, QString(), 20000);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { listArrived(reply); });
}

void TripSync::listArrived(QNetworkReply *reply)
{
    reply->deleteLater();
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || http != 200) {
        peerFailed(http == 401 || http == 403 ? QStringLiteral("Not paired any more: pair again")
                   : http == 404              ? QStringLiteral("That OCTAVE is too old to share drives")
                                              : QStringLiteral("Not in reach"));
        return;
    }
    const QJsonArray list = QJsonDocument::fromJson(reply->readAll()).array();
    for (const QJsonValue &v : list) {
        const qint64 id = v.toObject().value(QLatin1String("id")).toInteger();
        if (id > 0 && !m_hadSet.contains(id))
            m_wanted.append(id);
    }
    std::sort(m_wanted.begin(), m_wanted.end());
    nextTrip();
}

void TripSync::nextTrip()
{
    Peer *peer = currentPeer();
    if (!peer) {
        nextPeer();
        return;
    }
    if (m_wanted.isEmpty()) {
        peerDone();
        return;
    }
    const qint64 id = m_wanted.first();
    setStatus(QStringLiteral("Getting drives from %1 (%2 left)").arg(peer->name).arg(m_wanted.size()));
    QNetworkReply *jsonReply = get(*peer, QStringLiteral("id=%1&format=json").arg(id), 30000);
    connect(jsonReply, &QNetworkReply::finished, this, [this, jsonReply, id] {
        jsonReply->deleteLater();
        Peer *peer = currentPeer();
        QJsonObject summary = QJsonDocument::fromJson(jsonReply->readAll()).object();
        if (!peer || jsonReply->error() != QNetworkReply::NoError
            || summary.value(QLatin1String("id")).toInteger() != id) {
            peerFailed(QStringLiteral("The connection dropped"));
            return;
        }
        QNetworkReply *csvReply = get(*peer, QStringLiteral("id=%1&format=csv&z=1").arg(id), 300000);
        connect(csvReply, &QNetworkReply::finished, this, [this, csvReply, id, summary]() mutable {
            csvReply->deleteLater();
            Peer *peer = currentPeer();
            const QByteArray csv = csvReply->error() == QNetworkReply::NoError ? qUncompress(csvReply->readAll()) : QByteArray();
            if (!peer || csv.isEmpty()) {
                peerFailed(QStringLiteral("The connection dropped"));
                return;
            }
            // Named by id in the inbox, the CSV first: the JSON is what the
            // importer looks for
            const QString inbox = m_recorder ? m_recorder->historyDir() + QStringLiteral("/inbox") : QString();
            const QString base = inbox + QLatin1Char('/') + QString::number(id);
            summary[QLatin1String("csv")] = QString::number(id) + QStringLiteral(".csv");
            QSaveFile csvFile(base + QStringLiteral(".csv"));
            QSaveFile jsonFile(base + QStringLiteral(".json"));
            const bool written = !inbox.isEmpty() && QDir().mkpath(inbox)
                                 && csvFile.open(QIODevice::WriteOnly) && csvFile.write(csv) == csv.size() && csvFile.commit()
                                 && jsonFile.open(QIODevice::WriteOnly)
                                 && jsonFile.write(QJsonDocument(summary).toJson()) > 0 && jsonFile.commit();
            if (!written) {
                peerFailed(QStringLiteral("Could not save the drive (storage full?)"));
                return;
            }
            remember(m_had, id);
            ++m_fetched;
            ++peer->drives;
            m_wanted.removeAll(id);
            if (m_fetched % 10 == 0) {
                save();
                if (m_recorder)
                    m_recorder->importInbox();
            }
            nextTrip();
        });
    });
}

void TripSync::peerFailed(const QString &why)
{
    if (Peer *peer = currentPeer()) {
        qCInfo(lcSync) << peer->name << ":" << why;
        peer->lastError = why;
    }
    ++m_failedPeers;
    emit peersChanged();
    nextPeer();
}

void TripSync::peerDone()
{
    if (Peer *peer = currentPeer()) {
        peer->lastSyncMs = QDateTime::currentMSecsSinceEpoch();
        peer->lastError.clear();
    }
    emit peersChanged();
    nextPeer();
}

void TripSync::finishSync()
{
    save();
    if (m_fetched > 0 && m_recorder)
        m_recorder->importInbox();
    if (m_fetched > 0) {
        qCInfo(lcSync) << m_fetched << "drives fetched";
        setStatus(m_fetched == 1 ? QStringLiteral("1 drive taken in") : QStringLiteral("%1 drives taken in").arg(m_fetched));
    } else if (m_failedPeers > 0 && m_failedPeers == m_peers.size()) {
        setStatus(m_peers.size() == 1 ? QStringLiteral("%1: %2").arg(m_peers.first().name, m_peers.first().lastError)
                                      : QStringLiteral("No paired OCTAVE in reach"));
    } else {
        setStatus(QStringLiteral("Up to date"));
    }
    setBusy(false);
    emit syncFinished(m_fetched, m_failedPeers);
}

// ---------------------------------------------------------------------------
// The note a car left on this phone
// ---------------------------------------------------------------------------

void TripSync::readCarHint()
{
    QVariantMap hint;
    QFile f(m_externalHistoryDir + QStringLiteral("/car.json"));
    if (!m_externalHistoryDir.isEmpty() && f.open(QIODevice::ReadOnly) && f.size() < 65536) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonArray urls = o.value(QLatin1String("urls")).toArray();
        for (const QJsonValue &u : urls) {
            const QUrl url(u.toString());
            if (!url.isValid() || url.host().isEmpty())
                continue;
            hint = {{QStringLiteral("name"), o.value(QLatin1String("name")).toString().left(64)},
                    {QStringLiteral("host"), url.host()},
                    {QStringLiteral("port"), url.port(kDefaultPort)}};
            break;
        }
    }
    for (const Peer &p : std::as_const(m_peers)) {
        if (p.host == hint.value(QStringLiteral("host")).toString() && p.port == hint.value(QStringLiteral("port")).toInt())
            hint.clear();   // already paired
    }
    if (hint == m_carHint)
        return;
    m_carHint = hint;
    emit carHintChanged();
}

// ---------------------------------------------------------------------------
// Push over adb
// ---------------------------------------------------------------------------

void TripSync::setPhone(const QString &adbPath, const QString &serial)
{
    m_adbPath = adbPath;
    if (serial == m_serial)
        return;
    m_serial = serial;
    if (m_serial.isEmpty())
        m_pushTimer.stop();
    else
        m_pushTimer.start(20000);   // let the mirror session settle first
}

void TripSync::setServiceUrls(const QStringList &urls, bool onNetwork)
{
    m_serviceUrls.clear();
    if (!onNetwork)
        return;
    for (const QString &u : urls) {
        const QUrl url(u);
        if (!QHostAddress(url.host()).isLoopback() && url.host() != QLatin1String("localhost"))
            m_serviceUrls.append(u);
    }
}

bool TripSync::pushNow()
{
#if QT_CONFIG(process)
    if (!m_started || !m_pushToPhone || m_pushing || m_adbPath.isEmpty() || m_serial.isEmpty() || !m_recorder)
        return false;
    const QList<qint64> &has = m_pushed[m_serial];
    QList<Pair> pairs;
    const QVariantList trips = m_recorder->trips();
    // Oldest first, so a run that is cut short goes on where it stopped
    for (auto it = trips.crbegin(); it != trips.crend() && pairs.size() < kPushBatch; ++it) {
        const QVariantMap t = it->toMap();
        const qint64 id = t.value(QStringLiteral("id")).toLongLong();
        const QString csv = t.value(QStringLiteral("file")).toString();
        const QString json = t.value(QStringLiteral("summaryFile")).toString();
        if (travels(t) && !has.contains(id) && QFile::exists(csv) && QFile::exists(json))
            pairs.append({id, csv, json});
    }
    QByteArray note;
    if (!m_serviceUrls.isEmpty()) {
        note = QJsonDocument(QJsonObject{{QStringLiteral("name"), QSysInfo::machineHostName()},
                                         {QStringLiteral("urls"), QJsonArray::fromStringList(m_serviceUrls)}})
                   .toJson();
    }
    if (pairs.isEmpty() && note.isEmpty())
        return false;
    m_pushing = true;
    const bool more = pairs.size() >= kPushBatch;
    const QString adb = m_adbPath, serial = m_serial;
    QPointer<TripSync> self(this);
    (void)QtConcurrent::run([self, adb, serial, pairs, note, more] {
        QString why;
        const QList<qint64> arrived = pushOverAdb(adb, serial, pairs, note, &why);
        QMetaObject::invokeMethod(qApp, [self, serial, arrived, why, more] {
            if (!self)
                return;
            self->m_pushing = false;
            for (qint64 id : arrived)
                self->remember(self->m_pushed[serial], id);
            if (!arrived.isEmpty()) {
                self->save();
                qCInfo(lcSync) << arrived.size() << "drives pushed to the phone" << serial;
            } else if (!why.isEmpty()) {
                qCInfo(lcSync) << "Nothing pushed to the phone" << serial << ":" << why;
            }
            emit self->pushFinished(int(arrived.size()), why);
            if (more && !arrived.isEmpty() && self->m_serial == serial)
                self->m_pushTimer.start(2000);
        }, Qt::QueuedConnection);
    });
    return true;
#else
    return false;
#endif
}

void TripSync::finishBeforePowerOff(std::function<void()> done)
{
    if (!m_started || !m_recorder || !m_pushToPhone || m_adbPath.isEmpty() || m_serial.isEmpty()) {
        if (m_recorder)
            m_recorder->stopForPowerOff();
        done();
        return;
    }
    setStatus(QStringLiteral("Sending the drive to the phone…"));
    // One-shot: whichever comes first, the push or the time limit
    auto finished = std::make_shared<bool>(false);
    auto finish = [this, finished, done] {
        if (*finished)
            return;
        *finished = true;
        done();
    };
    QTimer::singleShot(kPowerOffPushMs, this, finish);
    // The drive is on disk (tripEnded), then it goes; a push already under
    // way is waited for and followed by one that has the drive
    auto push = std::make_shared<std::function<void()>>();
    *push = [this, finish, finished, push] {
        if (*finished)
            return;
        if (m_pushing) {
            connect(this, &TripSync::pushFinished, this, [push] { (*push)(); }, Qt::SingleShotConnection);
            return;
        }
        if (!pushNow()) {
            finish();   // nothing to send
            return;
        }
        connect(this, &TripSync::pushFinished, this, [finish] { finish(); }, Qt::SingleShotConnection);
    };
    m_pushTimer.stop();
    if (m_recorder->recording()) {
        // tripEnded says the files are written; the list that names them
        // (trips) follows it
        connect(m_recorder, &TripRecorder::tripEnded, this, [this, push] {
            connect(m_recorder, &TripRecorder::tripsChanged, this, [push] { (*push)(); }, Qt::SingleShotConnection);
        }, Qt::SingleShotConnection);
        m_recorder->stopForPowerOff();
    } else {
        m_recorder->stopForPowerOff();
        (*push)();
    }
}

void TripSync::snapshotNow()
{
#if QT_CONFIG(process)
    if (!m_started || !m_pushToPhone || m_pushing || m_adbPath.isEmpty() || m_serial.isEmpty() || !m_recorder
        || !m_recorder->recording())
        return;
    const qint64 trip = m_recorder->currentTripId();
    if (trip != m_snapshotTrip || m_serial != m_snapshotSerial) {
        // Another drive, or another phone: from its beginning
        m_snapshotTrip = trip;
        m_snapshotSerial = m_serial;
        m_snapshotSince = 0;
        m_snapshotSeq = 0;
    }
    // "<id>-p00001" sorts before "<id>": the parts are taken in before the whole
    const QString name = QStringLiteral("%1-p%2").arg(trip).arg(m_snapshotSeq + 1, 5, 10, QLatin1Char('0'));
    const QString base = m_recorder->historyDir() + QStringLiteral("/outbox/") + name;
    const QString adb = m_adbPath, serial = m_serial;
    m_pushing = true;
    QPointer<TripSync> self(this);
    m_recorder->snapshot(m_snapshotSince, base, [self, trip, name, base, adb, serial](qint64 id, qint64 lastTs) {
        if (!self)
            return;
        if (id != trip || lastTs <= 0) {
            self->m_pushing = false;   // the drive ended meanwhile, or nothing new
            return;
        }
        (void)QtConcurrent::run([self, trip, name, base, adb, serial, lastTs] {
            QString why;
            const Pair pair{trip, base + QStringLiteral(".csv"), base + QStringLiteral(".json"), name};
            const bool arrived = !pushOverAdb(adb, serial, {pair}, QByteArray(), &why).isEmpty();
            QFile::remove(pair.csv);
            QFile::remove(pair.json);
            QMetaObject::invokeMethod(qApp, [self, trip, serial, lastTs, arrived, why] {
                if (!self)
                    return;
                self->m_pushing = false;
                if (arrived && self->m_snapshotTrip == trip && self->m_snapshotSerial == serial) {
                    self->m_snapshotSince = lastTs;
                    ++self->m_snapshotSeq;
                }
                emit self->pushFinished(arrived ? 1 : 0, why);
            }, Qt::QueuedConnection);
        });
    });
#endif
}

QList<qint64> TripSync::pushOverAdb(const QString &adbPath, const QString &serial, const QList<Pair> &pairs,
                                    const QByteArray &carNote, QString *note)
{
    QList<qint64> arrived;
#if QT_CONFIG(process)
    auto adb = [&](const QStringList &args, int timeoutMs, QString *out = nullptr) {
        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
        proc.setCreateProcessArgumentsModifier(
            [](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
#endif
        proc.start(adbPath, QStringList{QStringLiteral("-s"), serial} + args);
        if (!proc.waitForFinished(timeoutMs)) {
            proc.kill();
            proc.waitForFinished(2000);
            return false;
        }
        if (out)
            *out = QString::fromUtf8(proc.readAllStandardOutput());
        return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    };
    const QString history = QString::fromLatin1(kPhoneHistoryDir);
    const QString inbox = history + QStringLiteral("/inbox");
    QString out;
    if (!adb({QStringLiteral("shell"), QStringLiteral("pm"), QStringLiteral("path"), QString::fromLatin1(kPhonePackage)}, 15000, &out)
        || !out.contains(QLatin1String("package:"))) {
        *note = QStringLiteral("the OCTAVE app is not on this phone");
        return arrived;
    }
    if (!adb({QStringLiteral("shell"), QStringLiteral("mkdir"), QStringLiteral("-p"), inbox}, 15000)) {
        *note = QStringLiteral("the app's folder on the phone cannot be written");
        return arrived;
    }
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        *note = QStringLiteral("no temporary folder");
        return arrived;
    }
    // A file appears under its name only when it is whole
    auto put = [&](const QString &local, const QString &remote, int timeoutMs) {
        return adb({QStringLiteral("push"), local, remote + QStringLiteral(".part")}, timeoutMs)
               && adb({QStringLiteral("shell"), QStringLiteral("mv"), remote + QStringLiteral(".part"), remote}, 15000);
    };
    if (!carNote.isEmpty()) {
        QFile f(tmp.filePath(QStringLiteral("car.json")));
        if (f.open(QIODevice::WriteOnly)) {
            f.write(carNote);
            f.close();
            put(f.fileName(), history + QStringLiteral("/car.json"), 30000);
        }
    }
    for (const Pair &pair : pairs) {
        // Named by id on the phone; the JSON says which CSV is its own
        QFile in(pair.json);
        if (!in.open(QIODevice::ReadOnly))
            continue;
        QJsonObject summary = QJsonDocument::fromJson(in.readAll()).object();
        const QString name = pair.name.isEmpty() ? QString::number(pair.id) : pair.name;
        summary[QLatin1String("csv")] = name + QStringLiteral(".csv");
        QFile json(tmp.filePath(name + QStringLiteral(".json")));
        if (!json.open(QIODevice::WriteOnly))
            continue;
        json.write(QJsonDocument(summary).toJson());
        json.close();
        const bool ok = put(pair.csv, inbox + QLatin1Char('/') + name + QStringLiteral(".csv"), 300000)
                        && put(json.fileName(), inbox + QLatin1Char('/') + name + QStringLiteral(".json"), 30000);
        json.remove();
        if (!ok) {
            *note = QStringLiteral("the phone stopped answering");
            break;
        }
        arrived.append(pair.id);
    }
#else
    Q_UNUSED(adbPath) Q_UNUSED(serial) Q_UNUSED(pairs) Q_UNUSED(carNote)
    *note = QStringLiteral("no adb in this build");
#endif
    return arrived;
}
