#include "spotifymanager.h"

#include "settingsmanager.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrlQuery>

#include <memory>
#include <utility>

#ifdef OCTAVE_WASM
#include <emscripten/val.h>
#else
#include <QTcpServer>
#include <QTcpSocket>
#endif

Q_LOGGING_CATEGORY(lcSpotify, "octave.spotify")

namespace {

#ifdef OCTAVE_SPOTIFY_CLIENT_ID
const QString kBuiltInClientId = QStringLiteral(OCTAVE_SPOTIFY_CLIENT_ID);
#else
const QString kBuiltInClientId;
#endif

const QString kApiBase = QStringLiteral("https://api.spotify.com/v1");
const QString kTokenUrl = QStringLiteral("https://accounts.spotify.com/api/token");
constexpr int kSignInTimeoutMs = 5 * 60 * 1000;
// A command's own effect wins over a poll that may predate it
constexpr int kCommandHoldMs = 1500;
constexpr int kArtCacheFiles = 40;
const QString kUnreachable = QStringLiteral("Can't reach Spotify.");

#ifdef OCTAVE_WASM
const char *kStorageKey = "octave.spotify";

QString pageUrl()
{
    // The app's own address, as registered: origin + path, no query, no index.html
    const emscripten::val loc = emscripten::val::global("location");
    QString path = QString::fromStdString(loc["pathname"].as<std::string>());
    if (path.endsWith(QLatin1String("index.html")))
        path.chop(int(qstrlen("index.html")));
    return QString::fromStdString(loc["origin"].as<std::string>()) + path;
}
#endif

QString errorReason(const QJsonDocument &doc)
{
    return doc.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("reason")).toString();
}

QString errorMessage(const QJsonDocument &doc)
{
    const QJsonValue e = doc.object().value(QStringLiteral("error"));
    if (e.isObject())
        return e.toObject().value(QStringLiteral("message")).toString();
    return doc.object().value(QStringLiteral("error_description")).toString(e.toString());
}

} // namespace

SpotifyManager::SpotifyManager(QObject *parent)
    : QObject(parent)
{
    m_clientId = kBuiltInClientId;
    m_apiBase = kApiBase;
    m_poll.setSingleShot(true);
    connect(&m_poll, &QTimer::timeout, this, &SpotifyManager::poll);
    m_volumeSend.setSingleShot(true);
    m_volumeSend.setInterval(kVolumeSendMs);
    connect(&m_volumeSend, &QTimer::timeout, this, [this] {
        if (m_volumeWanted < 0 || !signedIn())
            return;
        m_volumeAge.start();
        command("PUT", QStringLiteral("/me/player/volume?volume_percent=%1").arg(m_volumeWanted));
    });
    m_signInTimeout.setSingleShot(true);
    m_signInTimeout.setInterval(kSignInTimeoutMs);
    connect(&m_signInTimeout, &QTimer::timeout, this, [this] {
        if (m_state != QLatin1String("signingIn"))
            return;
        stopLoopback();
        setState(QStringLiteral("signedOut"));
        setStatus(QStringLiteral("Spotify sign-in timed out. Try again."));
    });
    // After the event loop starts: a test's setStoreFile() comes first
    QTimer::singleShot(0, this, &SpotifyManager::restore);
}

SpotifyManager::~SpotifyManager()
{
    stopLoopback();
}

void SpotifyManager::setStoreFile(const QString &path)
{
    m_storeFile = path;
}

bool SpotifyManager::builtInClientId() const
{
    return !kBuiltInClientId.isEmpty() && m_clientId == kBuiltInClientId;
}

QString SpotifyManager::redirectUri() const
{
#ifdef OCTAVE_WASM
    return pageUrl();
#else
    return QStringLiteral("http://127.0.0.1:%1/callback").arg(kLoopbackPort);
#endif
}

// ── storage ────────────────────────────────────────────────────────────

SpotifyManager::Stored SpotifyManager::load() const
{
    QJsonObject o;
#ifdef OCTAVE_WASM
    const emscripten::val v = emscripten::val::global("localStorage").call<emscripten::val>("getItem", std::string(kStorageKey));
    if (v.isString())
        o = QJsonDocument::fromJson(QByteArray::fromStdString(v.as<std::string>())).object();
#else
    const QString path = m_storeFile.isEmpty() ? SettingsManager::getAppDataDir() + QStringLiteral("/spotify.json")
                                               : m_storeFile;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        o = QJsonDocument::fromJson(f.readAll()).object();
#endif
    Stored s;
    s.clientId = o.value(QStringLiteral("clientId")).toString();
    s.refreshToken = o.value(QStringLiteral("refreshToken")).toString();
    s.userId = o.value(QStringLiteral("userId")).toString();
    s.userName = o.value(QStringLiteral("userName")).toString();
    s.verifier = o.value(QStringLiteral("verifier")).toString();
    s.oauthState = o.value(QStringLiteral("oauthState")).toString();
    return s;
}

void SpotifyManager::save() const
{
    QJsonObject o;
    // Only a client ID that isn't the build's is worth keeping
    if (m_clientId != kBuiltInClientId)
        o[QStringLiteral("clientId")] = m_clientId;
    if (!m_refreshToken.isEmpty()) {
        o[QStringLiteral("refreshToken")] = m_refreshToken;
        o[QStringLiteral("userId")] = m_userId;
        o[QStringLiteral("userName")] = m_userName;
    }
#ifdef OCTAVE_WASM
    // The browser sign-in leaves the page: what it needs on the way back
    if (!m_verifier.isEmpty()) {
        o[QStringLiteral("verifier")] = m_verifier;
        o[QStringLiteral("oauthState")] = m_oauthState;
    }
    emscripten::val::global("localStorage")
        .call<void>("setItem", std::string(kStorageKey),
                    QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString());
#else
    const QString path = m_storeFile.isEmpty() ? SettingsManager::getAppDataDir() + QStringLiteral("/spotify.json")
                                               : m_storeFile;
    // Atomic: the refresh token rotates about hourly, and the power can go
    // at any moment in a car. A torn file would mean signing in again there.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(lcSpotify) << "can't write" << path << f.errorString();
        return;
    }
    // The refresh token is a password to the account's player and library
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit())
        qCWarning(lcSpotify) << "can't write" << path << f.errorString();
#endif
}

void SpotifyManager::restore()
{
    const Stored s = load();
    qCInfo(lcSpotify) << "client id" << (s.clientId.isEmpty() && m_clientId.isEmpty() ? "none" : "set")
                      << "saved session" << !s.refreshToken.isEmpty() << "sign-in pending" << !s.verifier.isEmpty();
    if (!s.clientId.isEmpty() && s.clientId != m_clientId) {
        m_clientId = s.clientId;
        emit clientIdChanged();
    }
#ifdef OCTAVE_WASM
    // ?spotify=<Client ID> in the address: typing or pasting into a text
    // field is unreliable in the browser build on a phone. Taken, saved,
    // then dropped from the address bar.
    {
        const emscripten::val loc = emscripten::val::global("location");
        const QUrlQuery q(QString::fromStdString(loc["search"].as<std::string>()).mid(1));
        const QString fromUrl = q.queryItemValue(QStringLiteral("spotify")).trimmed();
        if (!fromUrl.isEmpty()) {
            static const QRegularExpression idPattern(QStringLiteral("^[0-9A-Za-z]{32}$"));
            if (idPattern.match(fromUrl).hasMatch()) {
                qCInfo(lcSpotify) << "client id from the address";
                if (fromUrl != m_clientId) {
                    m_clientId = fromUrl;
                    emit clientIdChanged();
                }
                // save() writes the members: keep the rest of what was stored
                m_refreshToken = s.refreshToken;
                m_userId = s.userId;
                m_userName = s.userName;
                m_verifier = s.verifier;
                m_oauthState = s.oauthState;
                save();
            } else {
                setStatus(QStringLiteral("The Client ID in the address doesn't look right (32 letters and digits)."));
            }
            emscripten::val::global("history").call<void>("replaceState", emscripten::val::null(), std::string(""),
                                                          pageUrl().toStdString());
        }
    }
    if (!s.verifier.isEmpty()) {
        finishBrowserSignIn(s);
        return;
    }
#endif
    if (s.refreshToken.isEmpty() || m_clientId.isEmpty())
        return;
    m_refreshToken = s.refreshToken;
    m_userId = s.userId;
    m_userName = s.userName;
    emit userChanged();
    setState(QStringLiteral("signedIn"));
    // The first call refreshes the access token
    onSignedIn();
}

void SpotifyManager::finishBrowserSignIn(const Stored &stored)
{
#ifdef OCTAVE_WASM
    emscripten::val loc = emscripten::val::global("location");
    const QUrlQuery q(QString::fromStdString(loc["search"].as<std::string>()).mid(1));
    const QString code = q.queryItemValue(QStringLiteral("code"));
    const QString state = q.queryItemValue(QStringLiteral("state"));
    const QString error = q.queryItemValue(QStringLiteral("error"));
    // Tidy the address bar: a reload must not try the used code again
    if (!code.isEmpty() || !error.isEmpty())
        emscripten::val::global("history").call<void>("replaceState", emscripten::val::null(), std::string(""),
                                                      pageUrl().toStdString());
    qCInfo(lcSpotify) << "back from Spotify sign-in: code" << !code.isEmpty() << "state matches"
                      << (state == stored.oauthState) << "error" << error;
    m_verifier.clear();
    m_oauthState.clear();
    // Keep a session that was there before this sign-in try
    m_refreshToken = stored.refreshToken;
    m_userId = stored.userId;
    m_userName = stored.userName;
    save();
    if (!code.isEmpty() && state == stored.oauthState) {
        setState(QStringLiteral("signingIn"));
        exchangeCode(code, stored.verifier);
        return;
    }
    if (!error.isEmpty())
        setStatus(error == QLatin1String("access_denied") ? QStringLiteral("Spotify sign-in was cancelled.")
                                                          : QStringLiteral("Spotify sign-in failed: ") + error);
    else if (!code.isEmpty())
        setStatus(QStringLiteral("Spotify sign-in didn't match this page (another tab?). Press Connect again."));
    else
        setStatus(QStringLiteral("Spotify sign-in didn't come back. Check the app's Redirect URI is exactly ")
                  + redirectUri());
    if (!m_refreshToken.isEmpty()) {
        emit userChanged();
        setState(QStringLiteral("signedIn"));
        onSignedIn();
    }
#else
    Q_UNUSED(stored);
#endif
}

// ── sign-in ────────────────────────────────────────────────────────────

void SpotifyManager::setClientId(const QString &clientId)
{
    const QString id = clientId.trimmed().isEmpty() ? kBuiltInClientId : clientId.trimmed();
    if (id == m_clientId)
        return;
    qCInfo(lcSpotify) << "client id set," << id.size() << "characters";
    if (m_state != QLatin1String("signedOut"))
        signOut();
    m_clientId = id;
    save();
    emit clientIdChanged();
}

void SpotifyManager::signIn()
{
    if (m_clientId.isEmpty()) {
        setStatus(QStringLiteral("Enter the Client ID of your Spotify developer app first."));
        return;
    }
    m_verifier = SpotifyApi::makeVerifier();
    m_oauthState = SpotifyApi::makeState();
    const QUrl url = SpotifyApi::authorizeUrl(m_clientId, redirectUri(), SpotifyApi::challengeFor(m_verifier),
                                              m_oauthState);
    setStatus({});
#ifdef OCTAVE_WASM
    save();   // the verifier, for when the page comes back
    qCInfo(lcSpotify) << "going to Spotify sign-in, back at" << redirectUri();
    setState(QStringLiteral("signingIn"));
    emscripten::val::global("location").call<void>("assign", url.toString(QUrl::FullyEncoded).toStdString());
#else
    startLoopback();
    if (!m_loopback)
        return;
    setState(QStringLiteral("signingIn"));
    m_signInTimeout.start();
    qCInfo(lcSpotify) << "opening Spotify sign-in in the browser";
    if (!QDesktopServices::openUrl(url))
        setStatus(QStringLiteral("Couldn't open a browser. Open this address on this device: ")
                  + url.toString(QUrl::FullyEncoded));
#endif
}

void SpotifyManager::startLoopback()
{
#ifndef OCTAVE_WASM
    stopLoopback();
    m_loopback = new QTcpServer(this);
    if (!m_loopback->listen(QHostAddress::LocalHost, kLoopbackPort)) {
        setStatus(QStringLiteral("Can't wait for Spotify's answer: port %1 is in use.").arg(kLoopbackPort));
        delete m_loopback;
        m_loopback = nullptr;
        return;
    }
    connect(m_loopback, &QTcpServer::newConnection, this, [this] {
        while (m_loopback && m_loopback->hasPendingConnections()) {
            QTcpSocket *sock = m_loopback->nextPendingConnection();
            connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
                const QByteArray head = sock->peek(8192);
                if (!head.contains("\r\n\r\n") && head.size() < 8192)
                    return;   // the request line and headers aren't all here yet
                sock->readAll();
                // "GET /callback?code=...&state=... HTTP/1.1"
                const QList<QByteArray> line = head.left(head.indexOf("\r\n")).split(' ');
                const QUrl target(QString::fromLatin1(line.value(1)));
                const QUrlQuery q(target);
                const bool ours = target.path() == QLatin1String("/callback");
                const QString code = q.queryItemValue(QStringLiteral("code"));
                const QString error = q.queryItemValue(QStringLiteral("error"));
                const bool good = ours && !code.isEmpty() && q.queryItemValue(QStringLiteral("state")) == m_oauthState;

                const QByteArray text = good ? "Signed in to Spotify. You can close this tab and go back to OCTAVE."
                                        : ours ? "Spotify sign-in didn't finish. Go back to OCTAVE and try again."
                                               : "Not found.";
                const QByteArray body = "<!DOCTYPE html><meta charset=utf-8><meta name=viewport "
                                        "content='width=device-width'><title>OCTAVE</title><body style='font:18px "
                                        "system-ui;margin:3em;background:#111;color:#eee'><p>" + text + "</p>";
                sock->write((ours ? QByteArray("HTTP/1.1 200 OK\r\n") : QByteArray("HTTP/1.1 404 Not Found\r\n"))
                            + "Content-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
                            + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                sock->disconnectFromHost();
                if (!ours)
                    return;   // a favicon request: keep waiting

                const QString verifier = m_verifier;
                stopLoopback();
                m_signInTimeout.stop();
                if (good) {
                    exchangeCode(code, verifier);
                } else {
                    setState(m_refreshToken.isEmpty() ? QStringLiteral("signedOut") : QStringLiteral("signedIn"));
                    setStatus(error == QLatin1String("access_denied")
                                  ? QStringLiteral("Spotify sign-in was cancelled.")
                                  : QStringLiteral("Spotify sign-in failed%1.")
                                        .arg(error.isEmpty() ? QString() : QStringLiteral(": ") + error));
                }
            });
        }
    });
#endif
}

void SpotifyManager::stopLoopback()
{
#ifndef OCTAVE_WASM
    if (m_loopback) {
        m_loopback->close();
        m_loopback->deleteLater();
        m_loopback = nullptr;
    }
#endif
}

void SpotifyManager::exchangeCode(const QString &code, const QString &verifier)
{
    m_verifier.clear();
    m_oauthState.clear();
    tokenRequest({{QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
                  {QStringLiteral("code"), code},
                  {QStringLiteral("redirect_uri"), redirectUri()},
                  {QStringLiteral("client_id"), m_clientId},
                  {QStringLiteral("code_verifier"), verifier}},
                 false);
}

void SpotifyManager::refreshToken()
{
    if (m_refreshing)
        return;
    if (m_refreshToken.isEmpty()) {
        failWaiting();
        return;
    }
    tokenRequest({{QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
                  {QStringLiteral("refresh_token"), m_refreshToken},
                  {QStringLiteral("client_id"), m_clientId}},
                 true);
}

void SpotifyManager::tokenRequest(const QList<QPair<QString, QString>> &form, bool isRefresh)
{
    m_refreshing = true;
    QByteArray body;
    for (const auto &kv : form) {
        if (!body.isEmpty())
            body += '&';
        body += QUrl::toPercentEncoding(kv.first) + '=' + QUrl::toPercentEncoding(kv.second);
    }
    QNetworkRequest req{QUrl(kTokenUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    QNetworkReply *reply = m_net.post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, isRefresh] {
        reply->deleteLater();
        m_refreshing = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject o = doc.object();
        const QString access = o.value(QStringLiteral("access_token")).toString();
        if (status != 200 || access.isEmpty()) {
            const QString why = errorMessage(doc);
            qCWarning(lcSpotify) << "token request failed" << status << reply->errorString() << why;
            // invalid_grant: the refresh token was revoked (or the app's
            // user list changed); anything else may pass, keep the session
            if (status == 400 || !isRefresh) {
                clearSession();
                setStatus(isRefresh ? QStringLiteral("Spotify signed OCTAVE out. Sign in again.")
                                    : QStringLiteral("Spotify sign-in failed%1.")
                                          .arg(why.isEmpty() ? QString() : QStringLiteral(": ") + why));
            } else {
                failWaiting();
                setStatus(kUnreachable);
            }
            return;
        }
        m_accessToken = access;
        m_tokenLifeMs = o.value(QStringLiteral("expires_in")).toInt(3600) * 1000;
        m_tokenAge.start();
        const QString refresh = o.value(QStringLiteral("refresh_token")).toString();
        if (!refresh.isEmpty() && refresh != m_refreshToken) {
            m_refreshToken = refresh;   // PKCE refresh tokens rotate
            save();
        }
        if (!isRefresh) {
            setState(QStringLiteral("signedIn"));
            onSignedIn();
        }
        if (m_status == kUnreachable)
            setStatus({});
        for (const TokenWaiter &w : std::exchange(m_waiting, {}))
            w.ready();
    });
}

void SpotifyManager::failWaiting()
{
    for (const TokenWaiter &w : std::exchange(m_waiting, {}))
        w.failed();
}

void SpotifyManager::onSignedIn()
{
    ensureLibrary();
    poll();
}

void SpotifyManager::ensureLibrary()
{
    if (!signedIn() || m_libraryLoaded || m_libraryLoading || m_profileLoading)
        return;
    m_profileLoading = true;
    get(QStringLiteral("/me"), [this](int status, const QJsonDocument &doc) {
        m_profileLoading = false;
        if (status != 200) {
            if (status == 403) {
                m_libraryLoaded = true;   // an answer, not an outage: no asking again
                setStatus(QStringLiteral("Spotify refused this account. It has to be added under User Management "
                                         "in the Spotify developer app."));
            }
            return;
        }
        const QJsonObject me = doc.object();
        const QString id = me.value(QStringLiteral("id")).toString();
        const QString name = me.value(QStringLiteral("display_name")).toString(id);
        if (id != m_userId || name != m_userName) {
            m_userId = id;
            m_userName = name;
            emit userChanged();
        }
        save();
        refreshLibrary();
    });
}

void SpotifyManager::signOut()
{
    stopLoopback();
    m_signInTimeout.stop();
    clearSession();
    setStatus({});
}

void SpotifyManager::clearSession()
{
    m_refreshToken.clear();
    m_accessToken.clear();
    m_verifier.clear();
    m_oauthState.clear();
    m_userId.clear();
    m_userName.clear();
    m_playlists.clear();
    m_albums.clear();
    m_liked.clear();
    m_collection.clear();
    m_collectionTracks.clear();
    m_devices.clear();
    m_libraryLoaded = false;
    m_poll.stop();
    save();
    emit userChanged();
    emit libraryChanged();
    emit collectionChanged();
    emit collectionTracksChanged();
    emit devicesChanged();
    applyPlayer({});
    setState(QStringLiteral("signedOut"));
    failWaiting();
}

// ── requests ───────────────────────────────────────────────────────────

void SpotifyManager::withToken(std::function<void()> ready, std::function<void()> failed)
{
    if (m_refreshToken.isEmpty()) {
        failed();
        return;
    }
    // A minute's margin: the token must still be good when Spotify reads it
    if (!m_accessToken.isEmpty() && m_tokenAge.isValid() && m_tokenAge.elapsed() < m_tokenLifeMs - 60000) {
        ready();
        return;
    }
    m_waiting << TokenWaiter{std::move(ready), std::move(failed)};
    refreshToken();
}

void SpotifyManager::api(const QByteArray &verb, const QString &path, const QJsonDocument &body, Done done,
                         bool retried)
{
    if (m_backoff.isValid() && m_backoff.elapsed() < m_backoffMs) {
        if (done)
            done(429, {});
        return;
    }
    withToken([this, verb, path, body, done, retried] {
        QNetworkRequest req{QUrl(path.startsWith(QLatin1String("https://")) ? path : m_apiBase + path)};
        req.setRawHeader("Authorization", "Bearer " + m_accessToken.toLatin1());
        QByteArray data;
        if (!body.isNull()) {
            req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
            data = body.toJson(QJsonDocument::Compact);
        }
        QNetworkReply *reply = m_net.sendCustomRequest(req, verb, data);
        connect(reply, &QNetworkReply::finished, this, [this, reply, verb, path, body, done, retried] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (status == 401 && !retried) {
                m_accessToken.clear();   // expired early: refresh and try once more
                api(verb, path, body, done, true);
                return;
            }
            if (status == 429) {
                const int secs = qBound(1, reply->rawHeader("Retry-After").toInt(), 120);
                m_backoff.start();
                m_backoffMs = secs * 1000;
                qCWarning(lcSpotify) << "rate limited for" << secs << "s";
            }
            if (status == 0 || status >= 400)
                qCInfo(lcSpotify) << verb << path << "->" << status << errorMessage(doc) << reply->errorString();
            if (done)
                done(status, doc);
        });
    }, [done] {
        if (done)
            done(0, {});
    });
}

void SpotifyManager::command(const QByteArray &verb, const QString &path, const QJsonDocument &body,
                             std::function<void()> accepted)
{
    m_commandAge.start();
    api(verb, path, body, [this, verb, path, body, accepted](int status, const QJsonDocument &doc) {
        if (status >= 200 && status < 300) {
            setStatus({});
            if (accepted)
                accepted();
            pollSoon();
            return;
        }
        if (!signedIn())
            return;   // the session ended: that is the news, not this command
        // A control the user pressed: they hear why, on whichever page they are
        auto fail = [this](const QString &message) {
            setStatus(message);
            emit commandFailed(message);
        };
        const QString reason = errorReason(doc);
        if (status == 404 && reason == QLatin1String("NO_ACTIVE_DEVICE") && !path.contains(QLatin1String("device_id="))) {
            // Nothing is playing anywhere: wake a device that is around
            get(QStringLiteral("/me/player/devices"), [this, verb, path, body, accepted, fail](int st, const QJsonDocument &d) {
                const QVariantList devs = st == 200 ? SpotifyApi::readDevices(d.object()) : QVariantList();
                if (m_devices != devs) {
                    m_devices = devs;
                    emit devicesChanged();
                }
                if (devs.isEmpty()) {
                    m_commandAge.invalidate();
                    undoOptimisticPlay(path);
                    fail(QStringLiteral("No Spotify device is on. Open Spotify on your phone, then try again."));
                    pollSoon();
                    return;
                }
                command(verb, path + (path.contains(QLatin1Char('?')) ? QStringLiteral("&") : QStringLiteral("?"))
                                  + QStringLiteral("device_id=") + SpotifyApi::pickDevice(devs),
                        body, accepted);
            });
            return;
        }
        m_commandAge.invalidate();
        undoOptimisticPlay(path);
        if (path.startsWith(QLatin1String("/me/player/volume"))) {
            // Status only: a knob turning would queue one notice per click
            if (status == 403 || status == 400)
                setStatus(QStringLiteral("%1 doesn't let Spotify change its volume. Use the phone's buttons.")
                              .arg(m_player.deviceName.isEmpty() ? QStringLiteral("This device") : m_player.deviceName));
            else if (status == 0)
                setStatus(kUnreachable);
        } else if (status == 403 && (reason == QLatin1String("PREMIUM_REQUIRED") || reason.isEmpty())) {
            fail(QStringLiteral("Spotify Premium is needed to control playback."));
        } else if (status == 429) {
            fail(QStringLiteral("Spotify asked OCTAVE to slow down. Try again in a moment."));
        } else if (status == 0) {
            fail(kUnreachable);
        } else {
            fail(QStringLiteral("Spotify: ") + (errorMessage(doc).isEmpty() ? QString::number(status) : errorMessage(doc)));
        }
        pollSoon();
    });
}

void SpotifyManager::undoOptimisticPlay(const QString &path)
{
    // Offline no poll corrects a play shown before it was taken
    if (!path.startsWith(QLatin1String("/me/player/play")) || !m_player.playing || m_wasPlaying)
        return;
    m_player.positionMs = positionMs();
    m_player.playing = false;
    emit playingChanged();
}

// ── library ────────────────────────────────────────────────────────────

void SpotifyManager::refreshLibrary()
{
    if (!signedIn() || m_libraryLoading)
        return;
    m_libraryPending = 3;
    m_libraryFailed = false;
    m_libraryLoading = true;
    emit libraryLoadingChanged();
    get(QStringLiteral("/me/tracks?limit=1"), [this](int status, const QJsonDocument &doc) {
        if (status == 200) {
            QVariantMap liked;
            liked[QStringLiteral("uri")] = SpotifyApi::likedUri(m_userId);
            liked[QStringLiteral("id")] = QStringLiteral("liked");
            liked[QStringLiteral("name")] = QStringLiteral("Liked Songs");
            liked[QStringLiteral("owner")] = m_userName;
            liked[QStringLiteral("imageUrl")] = QString();
            liked[QStringLiteral("trackCount")] = doc.object().value(QStringLiteral("total")).toInt();
            liked[QStringLiteral("kind")] = QStringLiteral("liked");
            liked[QStringLiteral("tracksListed")] = true;
            m_liked = liked;
            QVariantList list = m_playlists;
            if (!list.isEmpty() && list.first().toMap().value(QStringLiteral("kind")) == QLatin1String("liked"))
                list.removeFirst();
            list.prepend(m_liked);
            m_playlists = list;
            emit libraryChanged();
        }
        libraryPartDone(status == 200);
    });
    loadPlaylistPage(QStringLiteral("/me/playlists?limit=50"), {});
    loadAlbumPage(QStringLiteral("/me/albums?limit=50"), {});
}

void SpotifyManager::libraryPartDone(bool ok)
{
    m_libraryFailed = m_libraryFailed || !ok;
    if (--m_libraryPending == 0) {
        m_libraryLoaded = !m_libraryFailed;
        m_libraryLoading = false;
        emit libraryLoadingChanged();
    }
}

void SpotifyManager::loadPlaylistPage(const QString &path, QVariantList acc)
{
    get(path, [this, acc](int status, const QJsonDocument &doc) mutable {
        const QJsonObject page = doc.object();
        if (status == 200) {
            for (const QJsonValue &v : page.value(QStringLiteral("items")).toArray()) {
                const QVariantMap row = SpotifyApi::readPlaylist(v.toObject(), m_userId);
                if (!row.value(QStringLiteral("uri")).toString().isEmpty())
                    acc << row;
            }
            const QString next = page.value(QStringLiteral("next")).toString();
            if (!next.isEmpty() && acc.size() < 500) {
                loadPlaylistPage(next, acc);
                return;
            }
        }
        // A reload the network cut short keeps the list that was there
        if (status == 200 || m_playlists.isEmpty()) {
            if (!m_liked.isEmpty())
                acc.prepend(m_liked);
            m_playlists = acc;
            emit libraryChanged();
        }
        libraryPartDone(status == 200);
    });
}

void SpotifyManager::loadAlbumPage(const QString &path, QVariantList acc)
{
    get(path, [this, acc](int status, const QJsonDocument &doc) mutable {
        const QJsonObject page = doc.object();
        if (status == 200) {
            for (const QJsonValue &v : page.value(QStringLiteral("items")).toArray())
                acc << SpotifyApi::readAlbum(v.toObject());
            const QString next = page.value(QStringLiteral("next")).toString();
            if (!next.isEmpty() && acc.size() < 500) {
                loadAlbumPage(next, acc);
                return;
            }
        }
        if (status == 200 || m_albums.isEmpty()) {
            m_albums = acc;
            emit libraryChanged();
        }
        libraryPartDone(status == 200);
    });
}

void SpotifyManager::openCollection(const QString &uri)
{
    ++m_collectionGen;
    QVariantMap row;
    for (const QVariantList *list : {&m_playlists, &m_albums}) {
        for (const QVariant &v : *list) {
            if (v.toMap().value(QStringLiteral("uri")).toString() == uri) {
                row = v.toMap();
                break;
            }
        }
        if (!row.isEmpty())
            break;
    }
    if (row.isEmpty())
        row[QStringLiteral("uri")] = uri;
    m_collection = row;
    m_collectionTracks.clear();
    emit collectionChanged();

    QString path;
    const QString id = SpotifyApi::idOf(uri);
    if (SpotifyApi::isLiked(uri))
        path = QStringLiteral("/me/tracks?limit=50");
    else if (uri.startsWith(QLatin1String("spotify:album:")))
        path = QStringLiteral("/albums/%1/tracks?limit=50").arg(id);
    else if (uri.startsWith(QLatin1String("spotify:playlist:")) && row.value(QStringLiteral("tracksListed"), true).toBool())
        path = QStringLiteral("/playlists/%1/items?limit=50").arg(id);

    m_collectionLoading = !path.isEmpty() && signedIn();
    emit collectionTracksChanged();
    if (m_collectionLoading)
        loadTracksPage(uri, path, row.value(QStringLiteral("imageUrl")).toString());
}

void SpotifyManager::loadTracksPage(const QString &uri, const QString &path, const QString &albumImage)
{
    const int gen = m_collectionGen;
    get(path, [this, uri, albumImage, gen](int status, const QJsonDocument &doc) {
        if (gen != m_collectionGen)
            return;   // another collection was opened meanwhile
        const QJsonObject page = doc.object();
        if (status == 200) {
            m_collectionTracks += SpotifyApi::readTracks(page, albumImage);
            emit collectionTracksChanged();
            const QString next = page.value(QStringLiteral("next")).toString();
            if (!next.isEmpty() && m_collectionTracks.size() < kMaxCollectionTracks) {
                loadTracksPage(uri, next, albumImage);
                return;
            }
        } else if (status == 403 || status == 404) {
            // Spotify only lists playlists the user owns or collaborates on
            m_collection[QStringLiteral("tracksListed")] = false;
            emit collectionChanged();
        }
        m_collectionLoading = false;
        emit collectionTracksChanged();
    });
}

void SpotifyManager::refreshDevices()
{
    get(QStringLiteral("/me/player/devices"), [this](int status, const QJsonDocument &doc) {
        if (status != 200)
            return;
        const QVariantList devs = SpotifyApi::readDevices(doc.object());
        if (devs != m_devices) {
            m_devices = devs;
            emit devicesChanged();
        }
    });
}

// ── player ─────────────────────────────────────────────────────────────

int SpotifyManager::positionMs() const
{
    if (!m_player.playing || !m_positionAge.isValid())
        return m_player.positionMs;
    const qint64 at = m_player.positionMs + m_positionAge.elapsed();
    return int(m_player.durationMs > 0 ? qMin<qint64>(at, m_player.durationMs) : at);
}

void SpotifyManager::playCollection(const QString &uri, int index)
{
    if (!signedIn() || uri.isEmpty())
        return;
    const QJsonObject body = SpotifyApi::playBody(uri, index, uri == collectionUri() ? m_collectionTracks : QVariantList());
    m_wasPlaying = m_player.playing;
    m_player.playing = true;
    m_positionAge.start();
    emit playingChanged();
    command("PUT", QStringLiteral("/me/player/play"), QJsonDocument(body), [this] { emit playbackRequested(); });
}

void SpotifyManager::play()
{
    if (!signedIn())
        return;
    m_player.positionMs = positionMs();
    m_wasPlaying = m_player.playing;
    m_player.playing = true;
    m_positionAge.start();
    emit playingChanged();
    command("PUT", QStringLiteral("/me/player/play"), {}, [this] { emit playbackRequested(); });
}

void SpotifyManager::pause()
{
    if (!signedIn())
        return;
    m_player.positionMs = positionMs();
    m_player.playing = false;
    m_positionAge.start();
    emit playingChanged();
    command("PUT", QStringLiteral("/me/player/pause"));
}

void SpotifyManager::pauseBeforeExit(int timeoutMs)
{
    if (!signedIn())
        return;
    qCInfo(lcSpotify) << "OCTAVE is closing: pausing Spotify";
    m_poll.stop();
    QEventLoop loop;
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    // Shared: a late reply must find neither a dead loop nor a dead flag, and
    // api() can answer before exec() (no token, rate limited)
    const auto answered = std::make_shared<bool>(false);
    const QPointer<QEventLoop> waiting(&loop);
    api("PUT", QStringLiteral("/me/player/pause"), {}, [answered, waiting](int status, const QJsonDocument &) {
        qCInfo(lcSpotify) << "pause before exit ->" << status;
        *answered = true;
        if (waiting)
            waiting->quit();
    });
    if (!*answered)
        loop.exec();
    if (!*answered)
        qCWarning(lcSpotify) << "no answer from Spotify before exit: it may keep playing";
}

void SpotifyManager::togglePlay()
{
    if (m_player.playing)
        pause();
    else
        play();
}

void SpotifyManager::next()
{
    if (signedIn())
        command("POST", QStringLiteral("/me/player/next"));
}

void SpotifyManager::previous()
{
    if (signedIn())
        command("POST", QStringLiteral("/me/player/previous"));
}

void SpotifyManager::seek(int positionMs)
{
    if (!signedIn())
        return;
    m_player.positionMs = qMax(0, positionMs);
    m_positionAge.start();
    emit positionReported();
    command("PUT", QStringLiteral("/me/player/seek?position_ms=%1").arg(m_player.positionMs));
}

void SpotifyManager::setShuffle(bool on)
{
    if (!signedIn())
        return;
    if (m_player.shuffle != on) {
        m_player.shuffle = on;
        emit modesChanged();
    }
    command("PUT", QStringLiteral("/me/player/shuffle?state=%1").arg(on ? QStringLiteral("true") : QStringLiteral("false")));
}

void SpotifyManager::setVolume(int percent)
{
    if (!signedIn() || !m_player.supportsVolume)
        return;
    percent = qBound(0, percent, 100);
    if (percent == m_volumeWanted && percent == m_player.volumePercent)
        return;
    m_volumeWanted = percent;
    if (m_player.volumePercent != percent) {
        m_player.volumePercent = percent;
        emit deviceChanged();
    }
    // The first change goes now, a drag's later ones once per interval
    if (!m_volumeSend.isActive()) {
        if (!m_volumeAge.isValid() || m_volumeAge.elapsed() >= kVolumeSendMs) {
            m_volumeAge.start();
            command("PUT", QStringLiteral("/me/player/volume?volume_percent=%1").arg(percent));
        } else {
            m_volumeSend.start(kVolumeSendMs - int(m_volumeAge.elapsed()));
        }
    }
}

void SpotifyManager::toggleShuffle()
{
    setShuffle(!m_player.shuffle);
}

void SpotifyManager::cycleRepeat()
{
    if (!signedIn())
        return;
    const QString next = m_player.repeat == QLatin1String("off")       ? QStringLiteral("context")
                         : m_player.repeat == QLatin1String("context") ? QStringLiteral("track")
                                                                       : QStringLiteral("off");
    m_player.repeat = next;
    emit modesChanged();
    command("PUT", QStringLiteral("/me/player/repeat?state=") + next);
}

void SpotifyManager::transferTo(const QString &deviceId)
{
    if (!signedIn() || deviceId.isEmpty())
        return;
    command("PUT", QStringLiteral("/me/player"),
            QJsonDocument(QJsonObject{{QStringLiteral("device_ids"), QJsonArray{deviceId}},
                                      {QStringLiteral("play"), true}}),
            [this] { emit playbackRequested(); });
}

void SpotifyManager::poll()
{
    if (!signedIn() || m_pollInFlight)
        return;
    m_pollInFlight = true;
    get(QStringLiteral("/me/player?additional_types=episode"), [this](int status, const QJsonDocument &doc) {
        m_pollInFlight = false;
        if (!signedIn())
            return;
        if (status == 200)
            applyPlayer(SpotifyApi::readPlayer(doc.object()));
        else if (status == 204)
            applyPlayer({});   // nothing active on any device
        if (status == 200 || status == 204) {
            // Spotify answers: whatever an outage kept away can come now
            if (m_status == kUnreachable)
                setStatus({});
            ensureLibrary();
        }
        schedulePoll();
    });
}

void SpotifyManager::pollSoon(int ms)
{
    if (!signedIn())
        return;
    m_poll.start(ms);
}

void SpotifyManager::schedulePoll()
{
    if (!signedIn())
        return;
    int ms = m_player.playing ? kPollPlayingMs : kPollIdleMs;
    if (m_backoff.isValid() && m_backoff.elapsed() < m_backoffMs)
        ms = qMax(ms, int(m_backoffMs - m_backoff.elapsed()) + 100);
    if (!m_poll.isActive() || m_poll.remainingTime() > ms)
        m_poll.start(ms);
}

void SpotifyManager::applyPlayer(const SpotifyApi::PlayerState &in)
{
    SpotifyApi::PlayerState s = in;
    const bool held = m_commandAge.isValid() && m_commandAge.elapsed() < kCommandHoldMs;
    if (held && s.valid) {
        // A poll sent before the command landed: keep what the user asked for
        s.playing = m_player.playing;
        s.shuffle = m_player.shuffle;
        s.repeat = m_player.repeat;
    }
    // A poll from before the last volume change: keep the volume asked for
    if (s.valid && (m_volumeSend.isActive() || (m_volumeAge.isValid() && m_volumeAge.elapsed() < kCommandHoldMs)))
        s.volumePercent = m_player.volumePercent;
    const bool wasPlaying = m_player.playing;
    const bool trackDiffers = s.valid != m_player.valid || s.trackUri != m_player.trackUri || s.title != m_player.title
                              || s.artist != m_player.artist || s.album != m_player.album
                              || s.durationMs != m_player.durationMs || s.contextUri != m_player.contextUri;
    const bool modesDiffer = s.shuffle != m_player.shuffle || s.repeat != m_player.repeat;
    const bool deviceDiffers = s.deviceName != m_player.deviceName || s.volumePercent != m_player.volumePercent
                               || s.supportsVolume != m_player.supportsVolume;
    const QString oldImage = m_player.imageUrl;
    // Held: the extrapolated position stays (the poll's may predate a seek)
    const int keptPosition = positionMs();
    m_player = s;
    if (held && s.valid)
        m_player.positionMs = keptPosition;
    m_positionAge.start();

    if (trackDiffers)
        emit trackChanged();
    if (wasPlaying != m_player.playing)
        emit playingChanged();
    if (modesDiffer)
        emit modesChanged();
    if (deviceDiffers)
        emit deviceChanged();
    emit positionReported();
    if (m_player.imageUrl != oldImage || (m_player.imageUrl.isEmpty() && !m_artUrl.isEmpty()))
        fetchArt(m_player.imageUrl);
    if (!wasPlaying && m_player.playing && !held)
        emit startedElsewhere();
}

void SpotifyManager::fetchArt(const QString &url)
{
    m_artSource = url;
    if (url.isEmpty()) {
        if (!m_artUrl.isEmpty()) {
            m_artUrl.clear();
            emit artChanged();
        }
        return;
    }
    // On disk: Album Art Capture reads colours from a local file
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/spotify-art");
    QDir().mkpath(dir);
    const QString file = dir + QLatin1Char('/')
                         + QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex())
                         + QStringLiteral(".jpg");
    auto use = [this, file] {
        const QString local = QUrl::fromLocalFile(file).toString();
        if (local != m_artUrl) {
            m_artUrl = local;
            emit artChanged();
        }
    };
    if (QFileInfo::exists(file)) {
        use();
        return;
    }
    QNetworkReply *reply = m_net.get(QNetworkRequest{QUrl(url)});
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, file, dir, use] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || url != m_artSource)
            return;
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly))
            return;
        f.write(reply->readAll());
        f.close();
        // Keep the newest covers only
        const QFileInfoList all = QDir(dir).entryInfoList(QDir::Files, QDir::Time);
        for (int i = kArtCacheFiles; i < all.size(); ++i)
            QFile::remove(all.at(i).absoluteFilePath());
        use();
    });
}

// ── small setters ──────────────────────────────────────────────────────

void SpotifyManager::setState(const QString &s)
{
    if (s == m_state)
        return;
    m_state = s;
    qCInfo(lcSpotify) << "state" << s;
    emit stateChanged();
}

void SpotifyManager::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    if (!s.isEmpty())
        qCInfo(lcSpotify) << s;
    emit statusChanged();
}
