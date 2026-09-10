#include "spotifymanager.h"

#ifndef Q_OS_MOBILE

#include "settingsmanager.h"

#include <QDesktopServices>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>
#include <QDateTime>
#include <QImage>
#include <QColor>
#include <QLoggingCategory>

#include <cmath>
#include <algorithm>
#include <random>

Q_LOGGING_CATEGORY(lcSpotify, "octave.spotify")

// ══════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ══════════════════════════════════════════════════════════════════════

SpotifyManager::SpotifyManager(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_redirectUri(QStringLiteral("http://127.0.0.1:8888/callback"))
{
    // Playback state polling timer
    m_pollTimer.setInterval(3000);
    connect(&m_pollTimer, &QTimer::timeout, this, &SpotifyManager::onPollTimeout);

    // Smooth position interpolation timer (no API calls)
    m_interpolationTimer.setInterval(250);
    connect(&m_interpolationTimer, &QTimer::timeout, this, &SpotifyManager::onInterpolationTimeout);
}

SpotifyManager::~SpotifyManager()
{
    cleanup();
}

// ══════════════════════════════════════════════════════════════════════
// Settings manager wiring
// ══════════════════════════════════════════════════════════════════════

void SpotifyManager::setSettingsManager(SettingsManager *sm)
{
    m_settingsManager = sm;
    loadCredentials();

    // Track theme changes for Album Art Capture
    if (sm) {
        connect(sm, &SettingsManager::themeSettingChanged,
                this, &SpotifyManager::onThemeChanged);
        m_albumArtCaptureActive = (sm->themeSetting() == QStringLiteral("Album Art Capture"));
    }
}

void SpotifyManager::loadCredentials()
{
    if (!m_settingsManager) return;
    m_clientId     = m_settingsManager->get_spotify_client_id();
    m_clientSecret = m_settingsManager->get_spotify_client_secret();
}

// ══════════════════════════════════════════════════════════════════════
// Token cache (file-based in config directory)
// ══════════════════════════════════════════════════════════════════════

QString SpotifyManager::tokenCachePath() const
{
    return SettingsManager::getAppDataDir()
           + QStringLiteral("/.spotify_token_cache");
}

void SpotifyManager::saveTokenToCache(const QJsonObject &tokenInfo)
{
    const QString path = tokenCachePath();
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(tokenInfo).toJson(QJsonDocument::Compact));
        f.close();
    } else {
        qCWarning(lcSpotify) << "Failed to write token cache:" << path;
    }
}

QJsonObject SpotifyManager::loadTokenFromCache()
{
    const QString path = tokenCachePath();
    QFile f(path);
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) return {};

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError) {
        qCWarning(lcSpotify) << "Token cache parse error:" << err.errorString();
        return {};
    }
    return doc.object();
}

void SpotifyManager::deleteTokenCache()
{
    const QString path = tokenCachePath();
    if (QFile::exists(path))
        QFile::remove(path);
}

bool SpotifyManager::isTokenExpired() const
{
    if (m_accessToken.isEmpty()) return true;
    return QDateTime::currentSecsSinceEpoch() >= m_tokenExpiresAt;
}

// ══════════════════════════════════════════════════════════════════════
// OAuth2 flow
// ══════════════════════════════════════════════════════════════════════

void SpotifyManager::authenticate()
{
    emit statusProgress(QStringLiteral("[INFO] Starting Spotify authentication..."));

    if (!has_credentials()) {
        emit statusProgress(QStringLiteral("[ERROR] Spotify credentials not configured"));
        emit errorOccurred(QStringLiteral("Spotify credentials not configured"));
        return;
    }

    // Try cached token first
    tryConnectWithCachedToken();
}

void SpotifyManager::tryConnectWithCachedToken()
{
    QJsonObject cached = loadTokenFromCache();
    if (cached.isEmpty() || !cached.contains(QStringLiteral("access_token"))) {
        // No cached token — start full OAuth flow
        emit statusProgress(QStringLiteral("[INFO] No cached token, starting OAuth flow..."));

        // Build the authorization URL
        m_oauthState = QUuid::createUuid().toString(QUuid::WithoutBraces);

        QUrl authUrl(QStringLiteral("https://accounts.spotify.com/authorize"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("client_id"), m_clientId);
        q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
        q.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
        q.addQueryItem(QStringLiteral("scope"),
            QStringLiteral("user-library-read user-follow-read playlist-read-private "
                           "user-read-playback-state user-modify-playback-state "
                           "user-read-currently-playing"));
        q.addQueryItem(QStringLiteral("state"), m_oauthState);
        authUrl.setQuery(q);

        emit authUrlReady(authUrl.toString());

        // Start local redirect server
        startAuthServer();

        // Open browser
        emit statusProgress(QStringLiteral("[INFO] Opening browser for authentication..."));
        QDesktopServices::openUrl(authUrl);

        return;
    }

    // We have a cached token
    emit statusProgress(QStringLiteral("[INFO] Found cached token, connecting..."));

    m_accessToken  = cached.value(QStringLiteral("access_token")).toString();
    m_refreshToken = cached.value(QStringLiteral("refresh_token")).toString();
    m_tokenExpiresAt = cached.value(QStringLiteral("expires_at")).toInteger(0);

    // Refresh if expired
    if (isTokenExpired()) {
        emit statusProgress(QStringLiteral("[INFO] Token expired, refreshing..."));
        refreshAccessToken();
        return;
    }

    // Good to go
    m_isConnected = true;
    emit connectionStateChanged(true);
    m_pollTimer.start();
    m_interpolationTimer.start();
    refreshDevices();
    refreshPlaylists();
    emit statusProgress(QStringLiteral("[SUCCESS] Connected with cached token"));
    qCInfo(lcSpotify) << "Connected with cached token";
}

void SpotifyManager::startAuthServer()
{
    // Clean up any previous server
    if (m_authServer) {
        m_authServer->close();
        m_authServer->deleteLater();
        m_authServer = nullptr;
    }

    m_authServer = new QTcpServer(this);
    connect(m_authServer, &QTcpServer::newConnection,
            this, &SpotifyManager::onNewOAuthConnection);

    if (!m_authServer->listen(QHostAddress::LocalHost, 8888)) {
        emit errorOccurred(QStringLiteral("Failed to start OAuth redirect server on port 8888"));
        qCWarning(lcSpotify) << "Could not listen on port 8888";
        return;
    }

    emit statusProgress(QStringLiteral("[INFO] Waiting for browser authentication..."));
}

void SpotifyManager::onNewOAuthConnection()
{
    if (!m_authServer) return;

    QTcpSocket *socket = m_authServer->nextPendingConnection();
    if (!socket) return;

    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        QByteArray data = socket->readAll();
        handleOAuthCallback(data);

        // Always send an HTML response then close
        // (the actual content depends on whether auth succeeded — we
        //  just send a generic success here; errors are signalled separately.)
        const QByteArray html =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n\r\n"
            "<html><body style=\"font-family:sans-serif;text-align:center;padding-top:50px;\">"
            "<h1>Success!</h1>"
            "<p>Spotify connected. You can close this window.</p>"
            "</body></html>";
        socket->write(html);
        socket->flush();
        socket->disconnectFromHost();

        // Shut down the one-shot server
        m_authServer->close();
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

void SpotifyManager::handleOAuthCallback(const QByteArray &requestData)
{
    // Parse the GET request line: "GET /path?query HTTP/1.1\r\n..."
    const QString request = QString::fromUtf8(requestData);
    const int getEnd = request.indexOf(QLatin1String(" HTTP/"));
    if (getEnd < 0) return;
    const QString getLine = request.mid(4, getEnd - 4).trimmed(); // skip "GET "

    QUrl url(QStringLiteral("http://localhost") + getLine);
    QUrlQuery query(url);

    // Validate CSRF state
    const QString receivedState = query.queryItemValue(QStringLiteral("state"));
    if (receivedState != m_oauthState) {
        emit errorOccurred(QStringLiteral("OAuth state mismatch - possible CSRF attack"));
        return;
    }

    const QString code = query.queryItemValue(QStringLiteral("code"));
    if (code.isEmpty()) {
        const QString error = query.queryItemValue(QStringLiteral("error"));
        emit errorOccurred(QStringLiteral("OAuth error: ") + error);
        return;
    }

    // Exchange authorization code for token
    m_oauthState.clear();
    exchangeCodeForToken(code);
}

void SpotifyManager::exchangeCodeForToken(const QString &code)
{
    emit statusProgress(QStringLiteral("[SUCCESS] OAuth completed, exchanging code for token..."));

    QUrl tokenUrl(QStringLiteral("https://accounts.spotify.com/api/token"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("code"), code);
    body.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
    body.addQueryItem(QStringLiteral("client_id"), m_clientId);
    body.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);

    QNetworkRequest req(tokenUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply *reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            const QString msg = QStringLiteral("Token exchange failed: ") + reply->errorString();
            emit errorOccurred(msg);
            emit statusProgress(QStringLiteral("[ERROR] ") + msg);
            return;
        }

        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(reply->readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred(QStringLiteral("Token exchange: invalid JSON response"));
            return;
        }

        QJsonObject obj = doc.object();
        m_accessToken  = obj.value(QStringLiteral("access_token")).toString();
        m_refreshToken = obj.value(QStringLiteral("refresh_token")).toString();

        const int expiresIn = obj.value(QStringLiteral("expires_in")).toInt(3600);
        m_tokenExpiresAt = QDateTime::currentSecsSinceEpoch() + expiresIn;

        // Persist with computed absolute expiry
        obj[QStringLiteral("expires_at")] = m_tokenExpiresAt;
        saveTokenToCache(obj);

        m_isConnected = true;
        emit connectionStateChanged(true);
        m_pollTimer.start();
        m_interpolationTimer.start();
        refreshDevices();
        refreshPlaylists();
        emit statusProgress(QStringLiteral("[DONE] Successfully connected to Spotify!"));
        qCInfo(lcSpotify) << "Post-auth setup completed";
    });
}

void SpotifyManager::refreshAccessToken()
{
    if (m_refreshToken.isEmpty()) {
        emit statusProgress(QStringLiteral("[ERROR] No refresh token available"));
        emit errorOccurred(QStringLiteral("No refresh token available"));
        return;
    }

    QUrl tokenUrl(QStringLiteral("https://accounts.spotify.com/api/token"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    body.addQueryItem(QStringLiteral("refresh_token"), m_refreshToken);
    body.addQueryItem(QStringLiteral("client_id"), m_clientId);
    body.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);

    QNetworkRequest req(tokenUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply *reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            const QString msg = QStringLiteral("Token refresh failed: ") + reply->errorString();
            emit errorOccurred(msg);
            emit statusProgress(QStringLiteral("[ERROR] ") + msg);
            // Token refresh failed — force re-auth
            m_accessToken.clear();
            m_isConnected = false;
            emit connectionStateChanged(false);
            return;
        }

        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(reply->readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred(QStringLiteral("Token refresh: invalid JSON response"));
            return;
        }

        QJsonObject obj = doc.object();
        m_accessToken = obj.value(QStringLiteral("access_token")).toString();

        // Spotify may or may not return a new refresh token
        if (obj.contains(QStringLiteral("refresh_token")))
            m_refreshToken = obj.value(QStringLiteral("refresh_token")).toString();

        const int expiresIn = obj.value(QStringLiteral("expires_in")).toInt(3600);
        m_tokenExpiresAt = QDateTime::currentSecsSinceEpoch() + expiresIn;

        // Save updated token info
        QJsonObject cached = loadTokenFromCache();
        cached[QStringLiteral("access_token")]  = m_accessToken;
        cached[QStringLiteral("refresh_token")] = m_refreshToken;
        cached[QStringLiteral("expires_at")]    = m_tokenExpiresAt;
        saveTokenToCache(cached);

        // Finish connecting
        m_isConnected = true;
        emit connectionStateChanged(true);
        m_pollTimer.start();
        m_interpolationTimer.start();
        refreshDevices();
        refreshPlaylists();
        emit statusProgress(QStringLiteral("[SUCCESS] Token refreshed, connected!"));
        qCInfo(lcSpotify) << "Token refreshed successfully";
    });
}

// ══════════════════════════════════════════════════════════════════════
// Logout / Disconnect
// ══════════════════════════════════════════════════════════════════════

void SpotifyManager::logout()
{
    emit statusProgress(QStringLiteral("[INFO] Disconnecting from Spotify..."));

    m_pollTimer.stop();
    m_interpolationTimer.stop();

    m_accessToken.clear();
    m_refreshToken.clear();
    m_tokenExpiresAt = 0;
    m_isConnected = false;
    m_devices.clear();
    m_currentTrack.clear();
    m_cachedQueue.clear();
    m_previousTrack.clear();

    deleteTokenCache();
    emit statusProgress(QStringLiteral("[INFO] Cleared cached token"));
    emit connectionStateChanged(false);
    emit statusProgress(QStringLiteral("[DONE] Disconnected from Spotify"));
    qCInfo(lcSpotify) << "Disconnected";
}

void SpotifyManager::cleanup()
{
    qCDebug(lcSpotify) << "Cleaning up...";

    m_pollTimer.stop();
    m_interpolationTimer.stop();
    m_isConnected = false;
    m_accessToken.clear();

    if (m_authServer) {
        m_authServer->close();
        m_authServer->deleteLater();
        m_authServer = nullptr;
    }

    qCDebug(lcSpotify) << "Cleanup complete";
}

// ══════════════════════════════════════════════════════════════════════
// REST API helpers
// ══════════════════════════════════════════════════════════════════════

QNetworkRequest SpotifyManager::buildApiRequest(const QString &endpoint) const
{
    QUrl url(QStringLiteral("https://api.spotify.com") + endpoint);
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_accessToken).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return req;
}

void SpotifyManager::apiGet(const QString &endpoint,
                            std::function<void(const QJsonDocument &)> onSuccess,
                            std::function<void(const QString &)> onError)
{
    if (m_accessToken.isEmpty()) return;

    // Auto-refresh if token is about to expire (60s buffer)
    if (QDateTime::currentSecsSinceEpoch() >= m_tokenExpiresAt - 60) {
        refreshAccessToken();
        return;
    }

    QNetworkReply *reply = m_nam->get(buildApiRequest(endpoint));
    connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess, onError]() {
        handleApiReply(reply, onSuccess, onError);
    });
}

void SpotifyManager::apiPut(const QString &endpoint,
                            const QByteArray &body,
                            std::function<void()> onSuccess,
                            std::function<void(const QString &)> onError)
{
    if (m_accessToken.isEmpty()) return;

    if (QDateTime::currentSecsSinceEpoch() >= m_tokenExpiresAt - 60) {
        refreshAccessToken();
        return;
    }

    QNetworkReply *reply = m_nam->put(buildApiRequest(endpoint), body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess, onError]() {
        handleApiReplyNoBody(reply, onSuccess, onError);
    });
}

void SpotifyManager::apiPost(const QString &endpoint,
                             const QByteArray &body,
                             std::function<void()> onSuccess,
                             std::function<void(const QString &)> onError)
{
    if (m_accessToken.isEmpty()) return;

    if (QDateTime::currentSecsSinceEpoch() >= m_tokenExpiresAt - 60) {
        refreshAccessToken();
        return;
    }

    QNetworkReply *reply = m_nam->post(buildApiRequest(endpoint), body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess, onError]() {
        handleApiReplyNoBody(reply, onSuccess, onError);
    });
}

void SpotifyManager::handleApiReply(QNetworkReply *reply,
                                    std::function<void(const QJsonDocument &)> onSuccess,
                                    std::function<void(const QString &)> onError)
{
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        const QString msg = reply->errorString();
        qCDebug(lcSpotify) << "API error:" << status << msg;
        if (onError) onError(msg);
        return;
    }

    const QByteArray data = reply->readAll();
    if (data.isEmpty()) {
        // Some endpoints return 204 No Content
        if (onSuccess) onSuccess(QJsonDocument());
        return;
    }

    QJsonParseError parseErr;
    auto doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        if (onError) onError(QStringLiteral("JSON parse error: ") + parseErr.errorString());
        return;
    }

    if (onSuccess) onSuccess(doc);
}

void SpotifyManager::handleApiReplyNoBody(QNetworkReply *reply,
                                          std::function<void()> onSuccess,
                                          std::function<void(const QString &)> onError)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const QString msg = reply->errorString();
        qCDebug(lcSpotify) << "API error:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() << msg;
        if (onError) onError(msg);
        return;
    }

    if (onSuccess) onSuccess();
}

// ══════════════════════════════════════════════════════════════════════
// Playback Control
// ══════════════════════════════════════════════════════════════════════

void SpotifyManager::play()
{
    if (!m_isConnected) return;

    // Optimistic UI update
    m_isPlaying = true;
    emit playStateChanged(true);

    QString endpoint = QStringLiteral("/v1/me/player/play");
    if (!m_activeDeviceId.isEmpty())
        endpoint += QStringLiteral("?device_id=") + m_activeDeviceId;

    apiPut(endpoint, QByteArray(), nullptr,
           [this](const QString &err) {
               emit errorOccurred(QStringLiteral("Play failed: ") + err);
           });
}

void SpotifyManager::pause()
{
    if (!m_isConnected) return;

    m_isPlaying = false;
    emit playStateChanged(false);

    QString endpoint = QStringLiteral("/v1/me/player/pause");
    if (!m_activeDeviceId.isEmpty())
        endpoint += QStringLiteral("?device_id=") + m_activeDeviceId;

    apiPut(endpoint, QByteArray(), nullptr,
           [this](const QString &err) {
               emit errorOccurred(QStringLiteral("Pause failed: ") + err);
           });
}

void SpotifyManager::toggle_play()
{
    if (m_isPlaying)
        pause();
    else
        play();
}

void SpotifyManager::next_track()
{
    if (!m_isConnected) return;

    QString endpoint = QStringLiteral("/v1/me/player/next");
    if (!m_activeDeviceId.isEmpty())
        endpoint += QStringLiteral("?device_id=") + m_activeDeviceId;

    apiPost(endpoint, QByteArray(), nullptr,
            [this](const QString &err) {
                emit errorOccurred(QStringLiteral("Next track failed: ") + err);
            });
}

void SpotifyManager::previous_track()
{
    if (!m_isConnected) return;

    QString endpoint = QStringLiteral("/v1/me/player/previous");
    if (!m_activeDeviceId.isEmpty())
        endpoint += QStringLiteral("?device_id=") + m_activeDeviceId;

    apiPost(endpoint, QByteArray(), nullptr,
            [this](const QString &err) {
                emit errorOccurred(QStringLiteral("Previous track failed: ") + err);
            });
}

void SpotifyManager::set_position(int positionMs)
{
    if (!m_isConnected) return;

    // Optimistic UI update
    m_currentPositionMs = positionMs;
    m_lastKnownPositionMs = positionMs;
    m_lastPollTimestamp = QDateTime::currentMSecsSinceEpoch();

    QString endpoint = QStringLiteral("/v1/me/player/seek?position_ms=%1").arg(positionMs);
    if (!m_activeDeviceId.isEmpty())
        endpoint += QStringLiteral("&device_id=") + m_activeDeviceId;

    apiPut(endpoint, QByteArray(), nullptr,
           [this](const QString &err) {
               emit errorOccurred(QStringLiteral("Seek failed: ") + err);
           });
}

void SpotifyManager::set_volume(int volumePercent)
{
    if (!m_isConnected) return;

    const int vol = qBound(0, volumePercent, 100);
    m_currentVolume = vol;
    emit volumeChanged(vol);

    QString endpoint = QStringLiteral("/v1/me/player/volume?volume_percent=%1").arg(vol);
    if (!m_activeDeviceId.isEmpty())
        endpoint += QStringLiteral("&device_id=") + m_activeDeviceId;

    apiPut(endpoint, QByteArray(), nullptr,
           [this](const QString &err) {
               emit errorOccurred(QStringLiteral("Volume failed: ") + err);
           });
}

void SpotifyManager::toggle_shuffle()
{
    if (!m_isConnected) return;

    const bool newState = !m_isShuffled;
    m_isShuffled = newState;
    emit shuffleStateChanged(newState);

    QString endpoint = QStringLiteral("/v1/me/player/shuffle?state=%1")
                           .arg(newState ? QStringLiteral("true") : QStringLiteral("false"));
    if (!m_activeDeviceId.isEmpty())
        endpoint += QStringLiteral("&device_id=") + m_activeDeviceId;

    apiPut(endpoint, QByteArray(), nullptr,
           [this](const QString &err) {
               emit errorOccurred(QStringLiteral("Shuffle toggle failed: ") + err);
           });
}

void SpotifyManager::play_uri(const QString &uri)
{
    if (!m_isConnected) return;

    m_isPlaying = true;
    emit playStateChanged(true);

    const QString deviceId = m_activeDeviceId;
    const QString playlistId = m_currentSpotifyPlaylistId;

    QString endpoint = QStringLiteral("/v1/me/player/play");
    if (!deviceId.isEmpty())
        endpoint += QStringLiteral("?device_id=") + deviceId;

    QJsonObject body;

    if (uri.startsWith(QStringLiteral("spotify:track:"))) {
        if (!playlistId.isEmpty()) {
            // Play within playlist context
            const QString playlistUri = QStringLiteral("spotify:playlist:") + playlistId;
            body[QStringLiteral("context_uri")] = playlistUri;

            // Find offset of this track in the playlist
            int offset = -1;
            for (int i = 0; i < m_spotifyTracks.size(); ++i) {
                const auto track = m_spotifyTracks.at(i).toMap();
                if (track.value(QStringLiteral("uri")).toString() == uri) {
                    offset = i;
                    break;
                }
            }

            if (offset >= 0) {
                QJsonObject offsetObj;
                offsetObj[QStringLiteral("position")] = offset;
                body[QStringLiteral("offset")] = offsetObj;
            } else {
                // Track not found in playlist — play standalone
                body.remove(QStringLiteral("context_uri"));
                body.remove(QStringLiteral("offset"));
                QJsonArray uris;
                uris.append(uri);
                body[QStringLiteral("uris")] = uris;
            }
        } else {
            QJsonArray uris;
            uris.append(uri);
            body[QStringLiteral("uris")] = uris;
        }
    } else {
        // Album or playlist URI
        body[QStringLiteral("context_uri")] = uri;
    }

    apiPut(endpoint,
           QJsonDocument(body).toJson(QJsonDocument::Compact),
           nullptr,
           [this](const QString &err) {
               emit errorOccurred(QStringLiteral("Play URI failed: ") + err);
           });
}

// ══════════════════════════════════════════════════════════════════════
// Device Management
// ══════════════════════════════════════════════════════════════════════

void SpotifyManager::refresh_devices()
{
    refreshDevices();
}

void SpotifyManager::refreshDevices()
{
    if (!m_isConnected) return;

    emit statusProgress(QStringLiteral("[INFO] Scanning for Spotify devices..."));

    apiGet(QStringLiteral("/v1/me/player/devices"),
           [this](const QJsonDocument &doc) {
               const QJsonArray devices = doc.object()
                   .value(QStringLiteral("devices")).toArray();
               handleDevicesResult(devices);
           },
           [this](const QString &err) {
               emit errorOccurred(QStringLiteral("Could not load Spotify devices: ") + err);
           });
}

void SpotifyManager::handleDevicesResult(const QJsonArray &devices)
{
    m_devices.clear();
    for (const auto &val : devices) {
        const QJsonObject d = val.toObject();
        QVariantMap dev;
        dev[QStringLiteral("id")]        = d.value(QStringLiteral("id")).toString();
        dev[QStringLiteral("name")]      = d.value(QStringLiteral("name")).toString();
        dev[QStringLiteral("type")]      = d.value(QStringLiteral("type")).toString();
        dev[QStringLiteral("is_active")] = d.value(QStringLiteral("is_active")).toBool();
        dev[QStringLiteral("volume")]    = d.value(QStringLiteral("volume_percent")).toInt(0);
        m_devices.append(dev);

        if (d.value(QStringLiteral("is_active")).toBool()) {
            m_activeDeviceId = d.value(QStringLiteral("id")).toString();
            emit activeDeviceChanged(d.value(QStringLiteral("name")).toString());
        }
    }

    emit devicesChanged(m_devices);

    if (m_devices.isEmpty())
        emit statusProgress(QStringLiteral("[WARN] No devices found - open Spotify on a device"));
    else
        emit statusProgress(QStringLiteral("[FOUND] %1 device(s) available").arg(m_devices.size()));
}

void SpotifyManager::set_active_device(const QString &deviceId)
{
    if (!m_isConnected) return;

    QJsonObject body;
    QJsonArray ids;
    ids.append(deviceId);
    body[QStringLiteral("device_ids")] = ids;
    body[QStringLiteral("play")] = false;

    apiPut(QStringLiteral("/v1/me/player"),
           QJsonDocument(body).toJson(QJsonDocument::Compact),
           [this, deviceId]() {
               m_activeDeviceId = deviceId;
               for (const auto &d : m_devices) {
                   const auto dm = d.toMap();
                   if (dm.value(QStringLiteral("id")).toString() == deviceId) {
                       emit activeDeviceChanged(dm.value(QStringLiteral("name")).toString());
                       break;
                   }
               }
           },
           [this](const QString &err) {
               emit errorOccurred(QStringLiteral("Device transfer failed: ") + err);
           });
}

// ══════════════════════════════════════════════════════════════════════
// Playlist Management
// ══════════════════════════════════════════════════════════════════════

void SpotifyManager::refreshPlaylists()
{
    if (!m_isConnected) return;

    emit statusProgress(QStringLiteral("[INFO] Loading Spotify playlists..."));

    apiGet(QStringLiteral("/v1/me/playlists?limit=50"),
           [this](const QJsonDocument &doc) {
               const QJsonArray items = doc.object()
                   .value(QStringLiteral("items")).toArray();
               handlePlaylistsResult(items);
           },
           [this](const QString &err) {
               emit errorOccurred(QStringLiteral("Could not load Spotify playlists: ") + err);
           });
}

void SpotifyManager::handlePlaylistsResult(const QJsonArray &items)
{
    m_playlists.clear();
    for (const auto &val : items) {
        const QJsonObject p = val.toObject();
        QVariantMap pl;
        pl[QStringLiteral("id")]          = p.value(QStringLiteral("id")).toString();
        pl[QStringLiteral("name")]        = p.value(QStringLiteral("name")).toString();
        pl[QStringLiteral("uri")]         = p.value(QStringLiteral("uri")).toString();
        pl[QStringLiteral("track_count")] = p.value(QStringLiteral("tracks")).toObject()
                                                .value(QStringLiteral("total")).toInt();
        const QJsonArray images = p.value(QStringLiteral("images")).toArray();
        pl[QStringLiteral("image")] = images.isEmpty()
            ? QString()
            : images.first().toObject().value(QStringLiteral("url")).toString();
        m_playlists.append(pl);
    }

    emit playlistsChanged(m_playlists);
    emit statusProgress(QStringLiteral("[FOUND] %1 playlist(s) loaded").arg(m_playlists.size()));
}

void SpotifyManager::select_spotify_playlist(const QString &playlistId)
{
    qCDebug(lcSpotify) << "select_spotify_playlist called with ID:" << playlistId;
    if (!m_isConnected) {
        qCWarning(lcSpotify) << "Spotify client not connected";
        return;
    }

    // Find playlist name
    QString playlistName;
    for (const auto &p : m_playlists) {
        const auto pm = p.toMap();
        if (pm.value(QStringLiteral("id")).toString() == playlistId) {
            playlistName = pm.value(QStringLiteral("name")).toString();
            break;
        }
    }

    qCDebug(lcSpotify) << "Found playlist name:" << playlistName;

    // Fetch tracks for this playlist. February 2026 Web API: the path is
    // /items (was /tracks), pages are capped at 50 and the track lives under
    // "item" ("track" is deprecated). Only playlists the user owns or
    // collaborates on are readable.
    m_spotifyTracks.clear();
    fetchPlaylistItemsPage(playlistId, playlistName, 0);
}

void SpotifyManager::fetchPlaylistItemsPage(const QString &playlistId, const QString &playlistName, int offset)
{
    constexpr int kPageSize = 50;
    constexpr int kMaxTracks = 1000;
    const QString endpoint = QStringLiteral("/v1/playlists/%1/items?limit=%2&offset=%3&additional_types=track")
                                 .arg(playlistId).arg(kPageSize).arg(offset);
    apiGet(endpoint,
           [this, playlistId, playlistName, offset](const QJsonDocument &doc) {
               const QJsonObject page = doc.object();
               const QJsonArray items = page.value(QStringLiteral("items")).toArray();

               for (const auto &val : items) {
                   const QJsonObject item = val.toObject();
                   QJsonObject track = item.value(QStringLiteral("item")).toObject();
                   if (track.isEmpty())
                       track = item.value(QStringLiteral("track")).toObject();
                   if (track.isEmpty()) continue;
                   if (track.value(QStringLiteral("type")).toString(QStringLiteral("track")) != QLatin1String("track"))
                       continue;

                   // Build artist string
                   const QJsonArray artists = track.value(QStringLiteral("artists")).toArray();
                   QStringList artistNames;
                   for (const auto &a : artists)
                       artistNames.append(a.toObject().value(QStringLiteral("name")).toString());

                   // Album art
                   const QJsonArray albumImages = track.value(QStringLiteral("album")).toObject()
                       .value(QStringLiteral("images")).toArray();
                   const QString imageUrl = albumImages.isEmpty()
                       ? QString()
                       : albumImages.first().toObject().value(QStringLiteral("url")).toString();

                   QVariantMap t;
                   t[QStringLiteral("id")]          = track.value(QStringLiteral("id")).toString();
                   t[QStringLiteral("name")]        = track.value(QStringLiteral("name")).toString();
                   t[QStringLiteral("uri")]         = track.value(QStringLiteral("uri")).toString();
                   t[QStringLiteral("artist")]      = artistNames.join(QStringLiteral(", "));
                   t[QStringLiteral("album")]       = track.value(QStringLiteral("album")).toObject()
                                                          .value(QStringLiteral("name")).toString();
                   t[QStringLiteral("duration_ms")] = track.value(QStringLiteral("duration_ms")).toInt();
                   t[QStringLiteral("image")]       = imageUrl;
                   m_spotifyTracks.append(t);
               }

               const bool more = !page.value(QStringLiteral("next")).isNull()
                                 && !items.isEmpty()
                                 && m_spotifyTracks.size() < kMaxTracks;
               if (more) {
                   fetchPlaylistItemsPage(playlistId, playlistName, offset + kPageSize);
                   return;
               }

               m_currentSpotifyPlaylistId   = playlistId;
               m_currentSpotifyPlaylistName = playlistName;

               qCDebug(lcSpotify) << "Loaded" << m_spotifyTracks.size() << "tracks";

               emit spotifyTracksChanged(m_spotifyTracks);
               emit currentSpotifyPlaylistChanged(playlistName);
           },
           [this](const QString &err) {
               qCWarning(lcSpotify) << "Playlist tracks fetch error:" << err;
           });
}

// ══════════════════════════════════════════════════════════════════════
// Playback State Polling
// ══════════════════════════════════════════════════════════════════════

void SpotifyManager::onPollTimeout()
{
    pollPlaybackState();
}

void SpotifyManager::pollPlaybackState()
{
    if (!m_isConnected || m_pollInProgress) return;

    m_pollInProgress = true;

    apiGet(QStringLiteral("/v1/me/player"),
           [this](const QJsonDocument &doc) {
               m_pollInProgress = false;
               m_consecutivePlaybackErrors = 0;

               if (doc.isNull() || doc.isEmpty()) {
                   // No active playback — slow down polling
                   if (m_pollTimer.interval() != 10000)
                       m_pollTimer.setInterval(10000);
                   return;
               }
               handlePlaybackState(doc.object());
           },
           [this](const QString &err) {
               m_pollInProgress = false;
               m_consecutivePlaybackErrors++;
               qCDebug(lcSpotify) << "Playback poll error"
                                  << m_consecutivePlaybackErrors << "consecutive:" << err;

               if (m_consecutivePlaybackErrors >= POLL_ERROR_THRESHOLD) {
                   const qint64 now = QDateTime::currentSecsSinceEpoch();
                   if (now - m_lastPlaybackErrorTs >= POLL_ERROR_EMIT_INTERVAL_S) {
                       m_lastPlaybackErrorTs = now;
                       qCWarning(lcSpotify) << "Spotify playback poll failed"
                                            << m_consecutivePlaybackErrors
                                            << "times in a row:" << err;
                       emit errorOccurred(QStringLiteral("Spotify connection lost: ") + err);
                   }
               }
           });
}

void SpotifyManager::handlePlaybackState(const QJsonObject &playback)
{
    // Play state
    const bool isPlaying = playback.value(QStringLiteral("is_playing")).toBool();

    // Adjust poll rate
    if (isPlaying) {
        if (m_pollTimer.interval() != 3000)
            m_pollTimer.setInterval(3000);
    } else {
        if (m_pollTimer.interval() != 5000)
            m_pollTimer.setInterval(5000);
    }

    if (isPlaying != m_isPlaying) {
        m_isPlaying = isPlaying;
        emit playStateChanged(isPlaying);
    }

    // Shuffle
    const bool shuffleState = playback.value(QStringLiteral("shuffle_state")).toBool();
    if (shuffleState != m_isShuffled) {
        m_isShuffled = shuffleState;
        emit shuffleStateChanged(shuffleState);
    }

    // Volume from device
    const QJsonObject device = playback.value(QStringLiteral("device")).toObject();
    if (!device.isEmpty()) {
        const int volume = device.value(QStringLiteral("volume_percent")).toInt(100);
        if (volume != m_currentVolume) {
            m_currentVolume = volume;
            emit volumeChanged(volume);
        }
    }

    // Position
    const int progressMs = playback.value(QStringLiteral("progress_ms")).toInt(0);
    m_lastKnownPositionMs = progressMs;
    m_lastPollTimestamp = QDateTime::currentMSecsSinceEpoch();
    m_currentPositionMs = progressMs;
    emit positionChanged(progressMs);

    // Track info
    const QJsonObject track = playback.value(QStringLiteral("item")).toObject();
    if (!track.isEmpty()) {
        const QString trackId = track.value(QStringLiteral("id")).toString();
        if (trackId != m_currentTrack.value(QStringLiteral("id")).toString()) {
            // Save current as previous
            if (!m_currentTrack.value(QStringLiteral("name")).toString().isEmpty()) {
                m_previousTrack.clear();
                m_previousTrack[QStringLiteral("name")]   = m_currentTrack.value(QStringLiteral("name"));
                m_previousTrack[QStringLiteral("artist")] = m_currentTrack.value(QStringLiteral("artist"));
                m_previousTrack[QStringLiteral("album")]  = m_currentTrack.value(QStringLiteral("album"));
                m_previousTrack[QStringLiteral("image")]  = m_currentTrack.value(QStringLiteral("image"));
            }

            // Build artist string
            const QJsonArray artists = track.value(QStringLiteral("artists")).toArray();
            QStringList artistNames;
            for (const auto &a : artists)
                artistNames.append(a.toObject().value(QStringLiteral("name")).toString());

            // Album art
            const QJsonArray images = track.value(QStringLiteral("album")).toObject()
                .value(QStringLiteral("images")).toArray();
            const QString imageUrl = images.isEmpty()
                ? QString()
                : images.first().toObject().value(QStringLiteral("url")).toString();

            m_currentTrack.clear();
            m_currentTrack[QStringLiteral("id")]          = trackId;
            m_currentTrack[QStringLiteral("name")]        = track.value(QStringLiteral("name")).toString();
            m_currentTrack[QStringLiteral("artist")]      = artistNames.join(QStringLiteral(", "));
            m_currentTrack[QStringLiteral("album")]       = track.value(QStringLiteral("album")).toObject()
                                                                .value(QStringLiteral("name")).toString();
            m_currentTrack[QStringLiteral("duration_ms")] = track.value(QStringLiteral("duration_ms")).toInt();
            m_currentTrack[QStringLiteral("image")]       = imageUrl;

            emit currentTrackChanged(
                m_currentTrack.value(QStringLiteral("name")).toString(),
                m_currentTrack.value(QStringLiteral("artist")).toString(),
                m_currentTrack.value(QStringLiteral("album")).toString(),
                imageUrl);
            emit durationChanged(m_currentTrack.value(QStringLiteral("duration_ms")).toInt());

            // Album art color extraction
            if (!imageUrl.isEmpty())
                onTrackChangedForTheme(imageUrl);

            // Fetch queue for side-card art when no playlist loaded
            if (m_spotifyTracks.isEmpty())
                fetchQueue();
        }
    }
}

// ══════════════════════════════════════════════════════════════════════
// Position interpolation
// ══════════════════════════════════════════════════════════════════════

void SpotifyManager::onInterpolationTimeout()
{
    if (!m_isPlaying || m_lastPollTimestamp == 0) return;

    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_lastPollTimestamp;
    qint64 estimated = m_lastKnownPositionMs + elapsedMs;

    // Don't exceed track duration
    const int duration = m_currentTrack.value(QStringLiteral("duration_ms")).toInt();
    if (duration > 0 && estimated > duration)
        estimated = duration;

    if (std::abs(estimated - m_currentPositionMs) >= 200) {
        m_currentPositionMs = static_cast<int>(estimated);
        emit positionChanged(m_currentPositionMs);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Queue / neighbor art
// ══════════════════════════════════════════════════════════════════════

void SpotifyManager::fetchQueue()
{
    apiGet(QStringLiteral("/v1/me/player/queue"),
           [this](const QJsonDocument &doc) {
               const QJsonArray queue = doc.object()
                   .value(QStringLiteral("queue")).toArray();
               handleQueueResult(queue);
           },
           [](const QString &err) {
               qCDebug(lcSpotify) << "Spotify queue fetch failed:" << err;
           });
}

void SpotifyManager::handleQueueResult(const QJsonArray &queueTracks)
{
    m_cachedQueue.clear();
    const int limit = qMin(queueTracks.size(), 5);
    for (int i = 0; i < limit; ++i) {
        const QJsonObject track = queueTracks.at(i).toObject();
        const QJsonArray images = track.value(QStringLiteral("album")).toObject()
            .value(QStringLiteral("images")).toArray();

        const QJsonArray artists = track.value(QStringLiteral("artists")).toArray();
        QStringList artistNames;
        for (const auto &a : artists)
            artistNames.append(a.toObject().value(QStringLiteral("name")).toString());

        QVariantMap entry;
        entry[QStringLiteral("name")]   = track.value(QStringLiteral("name")).toString();
        entry[QStringLiteral("artist")] = artistNames.join(QStringLiteral(", "));
        entry[QStringLiteral("album")]  = track.value(QStringLiteral("album")).toObject()
                                              .value(QStringLiteral("name")).toString();
        entry[QStringLiteral("image")]  = images.isEmpty()
            ? QString()
            : images.first().toObject().value(QStringLiteral("url")).toString();
        m_cachedQueue.append(entry);
    }
    emit queueUpdated();
}

// ══════════════════════════════════════════════════════════════════════
// Getter slots (match Python @Slot(result=...) signatures)
// ══════════════════════════════════════════════════════════════════════

bool SpotifyManager::is_available()
{
    return true; // Always available in C++ build (no optional spotipy)
}

bool SpotifyManager::is_connected()
{
    return m_isConnected;
}

bool SpotifyManager::is_playing()
{
    return m_isPlaying;
}

bool SpotifyManager::is_shuffled()
{
    return m_isShuffled;
}

bool SpotifyManager::has_credentials()
{
    return !m_clientId.isEmpty() && !m_clientSecret.isEmpty();
}

bool SpotifyManager::has_spotify_playlist_loaded()
{
    return !m_currentSpotifyPlaylistId.isEmpty() && !m_spotifyTracks.isEmpty();
}

QString SpotifyManager::get_current_track_name()
{
    return m_currentTrack.value(QStringLiteral("name")).toString();
}

QString SpotifyManager::get_current_artist()
{
    return m_currentTrack.value(QStringLiteral("artist")).toString();
}

QString SpotifyManager::get_current_album()
{
    return m_currentTrack.value(QStringLiteral("album")).toString();
}

QString SpotifyManager::get_current_album_art()
{
    return m_currentTrack.value(QStringLiteral("image")).toString();
}

QString SpotifyManager::get_current_file()
{
    return get_current_track_name();
}

int SpotifyManager::get_duration()
{
    return m_currentTrack.value(QStringLiteral("duration_ms")).toInt();
}

int SpotifyManager::get_position()
{
    return m_currentPositionMs;
}

int SpotifyManager::get_volume()
{
    return m_currentVolume;
}

QVariantList SpotifyManager::get_devices()
{
    return m_devices;
}

QVariantList SpotifyManager::get_playlists()
{
    return m_playlists;
}

QStringList SpotifyManager::get_spotify_playlist_names()
{
    QStringList names;
    for (const auto &p : m_playlists)
        names.append(p.toMap().value(QStringLiteral("name")).toString());
    return names;
}

QVariantList SpotifyManager::get_spotify_tracks()
{
    return m_spotifyTracks;
}

QString SpotifyManager::get_current_spotify_playlist_name()
{
    return m_currentSpotifyPlaylistName;
}

QString SpotifyManager::get_current_spotify_playlist_id()
{
    return m_currentSpotifyPlaylistId;
}

QString SpotifyManager::get_spotify_playlist_id(const QString &playlistName)
{
    for (const auto &p : m_playlists) {
        const auto pm = p.toMap();
        if (pm.value(QStringLiteral("name")).toString() == playlistName)
            return pm.value(QStringLiteral("id")).toString();
    }
    return {};
}

QString SpotifyManager::get_spotify_track_image(const QVariant &trackName)
{
    const QString name = trackName.toString();
    for (const auto &t : m_spotifyTracks) {
        const auto tm = t.toMap();
        if (tm.value(QStringLiteral("name")).toString() == name)
            return tm.value(QStringLiteral("image")).toString();
    }
    return {};
}

QString SpotifyManager::get_spotify_track_duration_formatted(const QVariant &trackName)
{
    const QString name = trackName.toString();
    for (const auto &t : m_spotifyTracks) {
        const auto tm = t.toMap();
        if (tm.value(QStringLiteral("name")).toString() == name) {
            const int durationMs = tm.value(QStringLiteral("duration_ms")).toInt();
            const int minutes = durationMs / 60000;
            const int seconds = (durationMs % 60000) / 1000;
            return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
        }
    }
    return QStringLiteral("0:00");
}

QString SpotifyManager::get_spotify_track_artist(const QVariant &trackName)
{
    const QString name = trackName.toString();
    for (const auto &t : m_spotifyTracks) {
        const auto tm = t.toMap();
        if (tm.value(QStringLiteral("name")).toString() == name)
            return tm.value(QStringLiteral("artist"), QStringLiteral("Unknown Artist")).toString();
    }
    return QStringLiteral("Unknown Artist");
}

QString SpotifyManager::get_spotify_track_album(const QVariant &trackName)
{
    const QString name = trackName.toString();
    for (const auto &t : m_spotifyTracks) {
        const auto tm = t.toMap();
        if (tm.value(QStringLiteral("name")).toString() == name)
            return tm.value(QStringLiteral("album"), QStringLiteral("Unknown Album")).toString();
    }
    return QStringLiteral("Unknown Album");
}

QString SpotifyManager::get_spotify_track_uri(const QVariant &trackName)
{
    const QString name = trackName.toString();
    for (const auto &t : m_spotifyTracks) {
        const auto tm = t.toMap();
        if (tm.value(QStringLiteral("name")).toString() == name)
            return tm.value(QStringLiteral("uri")).toString();
    }
    return {};
}

QString SpotifyManager::get_next_track_info()
{
    if (!m_spotifyTracks.isEmpty() && !m_currentTrack.value(QStringLiteral("name")).toString().isEmpty()) {
        const QString currentName = m_currentTrack.value(QStringLiteral("name")).toString();
        for (int i = 0; i < m_spotifyTracks.size(); ++i) {
            const auto tm = m_spotifyTracks.at(i).toMap();
            if (tm.value(QStringLiteral("name")).toString() == currentName) {
                const int nextIndex = (i + 1) % m_spotifyTracks.size();
                const auto next = m_spotifyTracks.at(nextIndex).toMap();
                QJsonObject obj;
                obj[QStringLiteral("name")]   = next.value(QStringLiteral("name")).toString();
                obj[QStringLiteral("artist")] = next.value(QStringLiteral("artist")).toString();
                obj[QStringLiteral("album")]  = next.value(QStringLiteral("album")).toString();
                obj[QStringLiteral("image")]  = next.value(QStringLiteral("image")).toString();
                return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            }
        }
    }

    // Fallback: cached queue (index 0 = next up)
    if (!m_cachedQueue.isEmpty()) {
        const auto q = m_cachedQueue.first().toMap();
        QJsonObject obj;
        obj[QStringLiteral("name")]   = q.value(QStringLiteral("name")).toString();
        obj[QStringLiteral("artist")] = q.value(QStringLiteral("artist")).toString();
        obj[QStringLiteral("album")]  = q.value(QStringLiteral("album")).toString();
        obj[QStringLiteral("image")]  = q.value(QStringLiteral("image")).toString();
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }
    return {};
}

QString SpotifyManager::get_previous_track_info()
{
    if (!m_spotifyTracks.isEmpty() && !m_currentTrack.value(QStringLiteral("name")).toString().isEmpty()) {
        const QString currentName = m_currentTrack.value(QStringLiteral("name")).toString();
        for (int i = 0; i < m_spotifyTracks.size(); ++i) {
            const auto tm = m_spotifyTracks.at(i).toMap();
            if (tm.value(QStringLiteral("name")).toString() == currentName) {
                const int prevIndex = (i - 1 + m_spotifyTracks.size()) % m_spotifyTracks.size();
                const auto prev = m_spotifyTracks.at(prevIndex).toMap();
                QJsonObject obj;
                obj[QStringLiteral("name")]   = prev.value(QStringLiteral("name")).toString();
                obj[QStringLiteral("artist")] = prev.value(QStringLiteral("artist")).toString();
                obj[QStringLiteral("album")]  = prev.value(QStringLiteral("album")).toString();
                obj[QStringLiteral("image")]  = prev.value(QStringLiteral("image")).toString();
                return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            }
        }
    }

    // Fallback: last played track
    if (!m_previousTrack.value(QStringLiteral("name")).toString().isEmpty()) {
        QJsonObject obj;
        obj[QStringLiteral("name")]   = m_previousTrack.value(QStringLiteral("name")).toString();
        obj[QStringLiteral("artist")] = m_previousTrack.value(QStringLiteral("artist")).toString();
        obj[QStringLiteral("album")]  = m_previousTrack.value(QStringLiteral("album")).toString();
        obj[QStringLiteral("image")]  = m_previousTrack.value(QStringLiteral("image")).toString();
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }
    return {};
}

// ══════════════════════════════════════════════════════════════════════
// Album Art Color Extraction
// ══════════════════════════════════════════════════════════════════════

void SpotifyManager::onThemeChanged(const QString &theme)
{
    m_albumArtCaptureActive = (theme == QStringLiteral("Album Art Capture"));
    qCDebug(lcSpotify) << "Theme changed to:" << theme
                       << ", Album Art Capture active:" << m_albumArtCaptureActive;

    if (m_albumArtCaptureActive) {
        const QString imageUrl = get_current_album_art();
        if (!imageUrl.isEmpty())
            extractColorsFromUrl(imageUrl);
    }
}

void SpotifyManager::onTrackChangedForTheme(const QString &imageUrl)
{
    if (m_albumArtCaptureActive && !imageUrl.isEmpty() && imageUrl != m_lastExtractedImageUrl) {
        qCDebug(lcSpotify) << "Track changed, extracting colors for Album Art Capture";
        extractColorsFromUrl(imageUrl);
    }
}

void SpotifyManager::extractColorsFromUrl(const QString &imageUrl)
{
    if (imageUrl.isEmpty()) return;
    if (imageUrl == m_lastExtractedImageUrl) return;

    m_lastExtractedImageUrl = imageUrl;
    qCDebug(lcSpotify) << "Extracting colors from:" << imageUrl;

    QNetworkRequest req{QUrl(imageUrl)};
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleImageDownloaded(reply);
    });
}

void SpotifyManager::handleImageDownloaded(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(lcSpotify) << "Album art download failed:" << reply->errorString();
        return;
    }

    QImage img;
    if (!img.loadFromData(reply->readAll())) {
        qCWarning(lcSpotify) << "Failed to decode album art image";
        return;
    }

    img = img.scaled(100, 100, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    img = img.convertToFormat(QImage::Format_RGBA8888);

    auto colors = kmeansColors(img, 5, 10);

    // Sort by vibrancy (saturation * value)
    std::sort(colors.begin(), colors.end(), [](const QVector<int> &a, const QVector<int> &b) {
        auto vibrancy = [](const QVector<int> &rgb) -> double {
            const double r = rgb[0] / 255.0;
            const double g = rgb[1] / 255.0;
            const double bl = rgb[2] / 255.0;
            const double maxC = std::max({r, g, bl});
            const double minC = std::min({r, g, bl});
            const double s = (maxC == 0.0) ? 0.0 : (maxC - minC) / maxC;
            return s * maxC;
        };
        return vibrancy(a) > vibrancy(b);
    });

    // Calculate average luminance to determine if dark image
    double totalLum = 0.0;
    const int pixelCount = img.width() * img.height();
    const uchar *bits = img.constBits();
    const int sampleLimit = qMin(pixelCount, 1000);
    const int step = qMax(1, pixelCount / sampleLimit);
    int sampled = 0;
    for (int i = 0; i < pixelCount && sampled < sampleLimit; i += step) {
        const int offset = i * 4;
        const double r = bits[offset]     / 255.0;
        const double g = bits[offset + 1] / 255.0;
        const double b = bits[offset + 2] / 255.0;
        totalLum += 0.299 * r + 0.587 * g + 0.114 * b;
        sampled++;
    }
    const bool isDark = (sampled > 0) ? (totalLum / sampled < 0.5) : true;

    const QString themeJson = generateThemeFromColors(colors, isDark);
    if (!themeJson.isEmpty()) {
        qCDebug(lcSpotify) << "Generated theme, length:" << themeJson.size();
        if (m_settingsManager)
            m_settingsManager->set_album_art_colors(themeJson);
        emit albumColorsExtracted(themeJson);
    }
}

// ── K-means clustering ────────────────────────────────────────────

QVector<QVector<int>> SpotifyManager::kmeansColors(const QImage &img, int k, int maxIterations)
{
    const int w = img.width();
    const int h = img.height();
    const int pixelCount = w * h;
    const uchar *bits = img.constBits();

    // Read pixels into a flat array [pixelCount][3]
    QVector<QVector<double>> pixels(pixelCount, QVector<double>(3));
    for (int i = 0; i < pixelCount; ++i) {
        const int off = i * 4; // RGBA8888
        pixels[i][0] = bits[off];
        pixels[i][1] = bits[off + 1];
        pixels[i][2] = bits[off + 2];
    }

    // Initialize centroids by picking k random pixels (seeded for reproducibility)
    std::mt19937 rng(42);
    QVector<int> indices(pixelCount);
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), rng);

    QVector<QVector<double>> centroids(k, QVector<double>(3, 0.0));
    for (int c = 0; c < k; ++c)
        centroids[c] = pixels[indices[c]];

    QVector<int> labels(pixelCount, 0);

    for (int iter = 0; iter < maxIterations; ++iter) {
        // Assign labels
        for (int i = 0; i < pixelCount; ++i) {
            double bestDist = std::numeric_limits<double>::max();
            int bestLabel = 0;
            for (int c = 0; c < k; ++c) {
                const double dr = pixels[i][0] - centroids[c][0];
                const double dg = pixels[i][1] - centroids[c][1];
                const double db = pixels[i][2] - centroids[c][2];
                const double dist = dr * dr + dg * dg + db * db;
                if (dist < bestDist) {
                    bestDist = dist;
                    bestLabel = c;
                }
            }
            labels[i] = bestLabel;
        }

        // Recompute centroids
        QVector<QVector<double>> newCentroids(k, QVector<double>(3, 0.0));
        QVector<int> counts(k, 0);
        for (int i = 0; i < pixelCount; ++i) {
            const int lbl = labels[i];
            newCentroids[lbl][0] += pixels[i][0];
            newCentroids[lbl][1] += pixels[i][1];
            newCentroids[lbl][2] += pixels[i][2];
            counts[lbl]++;
        }

        bool converged = true;
        for (int c = 0; c < k; ++c) {
            if (counts[c] > 0) {
                newCentroids[c][0] /= counts[c];
                newCentroids[c][1] /= counts[c];
                newCentroids[c][2] /= counts[c];
            } else {
                newCentroids[c] = centroids[c];
            }
            // Check convergence
            const double dr = centroids[c][0] - newCentroids[c][0];
            const double dg = centroids[c][1] - newCentroids[c][1];
            const double db = centroids[c][2] - newCentroids[c][2];
            if (dr * dr + dg * dg + db * db > 1.0)
                converged = false;
        }
        centroids = newCentroids;
        if (converged) break;
    }

    // Convert to int vectors
    QVector<QVector<int>> result(k);
    for (int c = 0; c < k; ++c) {
        result[c] = { static_cast<int>(std::round(centroids[c][0])),
                      static_cast<int>(std::round(centroids[c][1])),
                      static_cast<int>(std::round(centroids[c][2])) };
    }
    return result;
}

// ── Theme generation from extracted colors ────────────────────────

// Static color-math helpers

QString SpotifyManager::rgbToHex(const QVector<int> &rgb)
{
    return QStringLiteral("#%1%2%3")
        .arg(qBound(0, rgb[0], 255), 2, 16, QLatin1Char('0'))
        .arg(qBound(0, rgb[1], 255), 2, 16, QLatin1Char('0'))
        .arg(qBound(0, rgb[2], 255), 2, 16, QLatin1Char('0'));
}

QVector<int> SpotifyManager::adjustBrightness(const QVector<int> &rgb, double factor)
{
    const double r = rgb[0] / 255.0;
    const double g = rgb[1] / 255.0;
    const double b = rgb[2] / 255.0;

    QColor c = QColor::fromRgbF(r, g, b);
    double h = c.hsvHueF();
    double s = c.hsvSaturationF();
    double v = c.valueF();

    v = qBound(0.0, v * factor, 1.0);
    c = QColor::fromHsvF(h < 0 ? 0.0 : h, s, v);
    return { c.red(), c.green(), c.blue() };
}

QVector<int> SpotifyManager::adjustSaturation(const QVector<int> &rgb, double factor)
{
    const double r = rgb[0] / 255.0;
    const double g = rgb[1] / 255.0;
    const double b = rgb[2] / 255.0;

    QColor c = QColor::fromRgbF(r, g, b);
    double h = c.hsvHueF();
    double s = c.hsvSaturationF();
    double v = c.valueF();

    s = qBound(0.0, s * factor, 1.0);
    c = QColor::fromHsvF(h < 0 ? 0.0 : h, s, v);
    return { c.red(), c.green(), c.blue() };
}

double SpotifyManager::getLuminance(const QVector<int> &rgb)
{
    auto channelLuminance = [](int channel) -> double {
        const double c = channel / 255.0;
        return (c <= 0.03928) ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channelLuminance(rgb[0])
         + 0.7152 * channelLuminance(rgb[1])
         + 0.0722 * channelLuminance(rgb[2]);
}

double SpotifyManager::getContrastRatio(const QVector<int> &rgb1, const QVector<int> &rgb2)
{
    const double l1 = getLuminance(rgb1);
    const double l2 = getLuminance(rgb2);
    const double lighter = std::max(l1, l2);
    const double darker  = std::min(l1, l2);
    return (lighter + 0.05) / (darker + 0.05);
}

QVector<int> SpotifyManager::getReadableTextColor(const QVector<int> &bgRgb)
{
    return (getLuminance(bgRgb) < 0.3) ? QVector<int>{245, 245, 245}
                                        : QVector<int>{25, 25, 25};
}

QVector<int> SpotifyManager::getSecondaryTextColor(const QVector<int> &bgRgb)
{
    return (getLuminance(bgRgb) < 0.3) ? QVector<int>{210, 210, 210}
                                        : QVector<int>{60, 60, 60};
}

QVector<int> SpotifyManager::capBrightness(const QVector<int> &rgb, double maxBrightness)
{
    QColor c = QColor(rgb[0], rgb[1], rgb[2]);
    double h = c.hsvHueF();
    double s = c.hsvSaturationF();
    double v = c.valueF();

    if (v > maxBrightness) {
        v = maxBrightness;
        s = qMin(1.0, s * 1.1);
    }
    c = QColor::fromHsvF(h < 0 ? 0.0 : h, s, v);
    return { c.red(), c.green(), c.blue() };
}

QVector<int> SpotifyManager::ensureIconContrast(const QVector<int> &iconRgb,
                                                 const QVector<int> &bgRgb,
                                                 double minContrast)
{
    if (getContrastRatio(iconRgb, bgRgb) >= minContrast)
        return capBrightness(iconRgb);

    const double bgLum = getLuminance(bgRgb);
    if (bgLum < 0.5) {
        for (double factor : {1.3, 1.5, 1.8, 2.0, 2.5, 3.0}) {
            auto adjusted = adjustBrightness(iconRgb, factor);
            if (getContrastRatio(adjusted, bgRgb) >= minContrast)
                return capBrightness(adjusted);
        }
        return {200, 180, 130}; // Fallback muted gold
    } else {
        for (double factor : {0.7, 0.5, 0.4, 0.3, 0.2}) {
            auto adjusted = adjustBrightness(iconRgb, factor);
            if (getContrastRatio(adjusted, bgRgb) >= minContrast)
                return adjusted;
        }
        return {50, 40, 30};
    }
}

QString SpotifyManager::generateThemeFromColors(const QVector<QVector<int>> &colors, bool isDark)
{
    if (colors.isEmpty()) return {};

    const auto &primaryRgb    = colors[0];
    const auto &secondaryRgb  = colors.size() > 1 ? colors[1] : colors[0];
    const auto &tertiaryRgb   = colors.size() > 2 ? colors[2] : secondaryRgb;
    const auto &quaternaryRgb = colors.size() > 3 ? colors[3] : tertiaryRgb;

    // Accent colors
    auto accent  = adjustBrightness(adjustSaturation(primaryRgb, 1.4), 1.2);
    auto accent2 = adjustBrightness(adjustSaturation(secondaryRgb, 1.3), 1.15);
    auto accent3 = adjustBrightness(adjustSaturation(tertiaryRgb, 1.25), 1.1);
    auto accent4 = adjustBrightness(adjustSaturation(quaternaryRgb, 1.2), 1.05);
    auto navColor  = adjustBrightness(accent, 0.7);
    auto navColor2 = adjustBrightness(accent2, 0.75);

    QVector<int> base, baseAlt, hover, paused, playing;
    if (isDark) {
        base    = adjustBrightness(primaryRgb, 0.15);
        baseAlt = adjustBrightness(primaryRgb, 0.22);
        hover   = adjustBrightness(primaryRgb, 0.30);
        paused  = adjustBrightness(primaryRgb, 0.25);
        playing = adjustBrightness(primaryRgb, 0.35);
    } else {
        base    = adjustSaturation(adjustBrightness(primaryRgb, 2.5), 0.3);
        baseAlt = adjustBrightness(base, 0.92);
        hover   = adjustBrightness(base, 0.88);
        paused  = adjustBrightness(base, 0.85);
        playing = adjustBrightness(base, 0.80);
    }

    auto textPrimary   = getReadableTextColor(base);
    auto textSecondary = getSecondaryTextColor(base);

    auto accentVisible  = ensureIconContrast(accent, base);
    auto accent2Visible = ensureIconContrast(accent2, base);
    auto accent3Visible = ensureIconContrast(accent3, base);
    auto accent4Visible = ensureIconContrast(accent4, base);
    auto navColorVisible  = ensureIconContrast(navColor, base);
    auto navColor2Visible = ensureIconContrast(navColor2, base);

    QJsonObject theme;
    theme[QStringLiteral("base")]    = rgbToHex(base);
    theme[QStringLiteral("baseAlt")] = rgbToHex(baseAlt);
    theme[QStringLiteral("accent")]  = rgbToHex(accentVisible);

    {
        QJsonObject text;
        text[QStringLiteral("primary")]   = rgbToHex(textPrimary);
        text[QStringLiteral("secondary")] = rgbToHex(textSecondary);
        theme[QStringLiteral("text")]     = text;
    }
    {
        QJsonObject states;
        states[QStringLiteral("hover")]   = rgbToHex(hover);
        states[QStringLiteral("paused")]  = rgbToHex(paused);
        states[QStringLiteral("playing")] = rgbToHex(playing);
        theme[QStringLiteral("states")]   = states;
    }
    {
        QJsonObject sliders;
        sliders[QStringLiteral("volume")]   = rgbToHex(accentVisible);
        sliders[QStringLiteral("media")]    = rgbToHex(accent2Visible);
        sliders[QStringLiteral("settings")] = rgbToHex(accent3Visible);
        theme[QStringLiteral("sliders")]    = sliders;
    }
    {
        QJsonObject bb;
        bb[QStringLiteral("previous")]           = rgbToHex(navColorVisible);
        bb[QStringLiteral("play")]               = rgbToHex(accentVisible);
        bb[QStringLiteral("pause")]              = rgbToHex(accentVisible);
        bb[QStringLiteral("next")]               = rgbToHex(navColorVisible);
        bb[QStringLiteral("volume")]             = rgbToHex(accent2Visible);
        bb[QStringLiteral("shuffle")]            = rgbToHex(accent3Visible);
        bb[QStringLiteral("toggleShade")]        = rgbToHex(hover);
        bb[QStringLiteral("homeButton")]         = rgbToHex(accentVisible);
        bb[QStringLiteral("obdButton")]          = rgbToHex(accent4Visible);
        bb[QStringLiteral("mediaButton")]        = rgbToHex(accent2Visible);
        bb[QStringLiteral("settingsButton")]     = rgbToHex(accent3Visible);
        bb[QStringLiteral("sensorButton")]       = rgbToHex(accentVisible);
        bb[QStringLiteral("androidAutoButton")]  = rgbToHex(navColor2Visible);
        bb[QStringLiteral("phoneMirrorButton")]  = rgbToHex(accent4Visible);
        theme[QStringLiteral("bottombar")]       = bb;
    }
    {
        QJsonObject mr;
        mr[QStringLiteral("previous")]    = rgbToHex(navColorVisible);
        mr[QStringLiteral("play")]        = rgbToHex(ensureIconContrast(adjustBrightness(accent, 1.1), base));
        mr[QStringLiteral("pause")]       = rgbToHex(ensureIconContrast(adjustBrightness(accent, 1.1), base));
        mr[QStringLiteral("next")]        = rgbToHex(navColorVisible);
        mr[QStringLiteral("left")]        = rgbToHex(accent4Visible);
        mr[QStringLiteral("right")]       = rgbToHex(accent4Visible);
        mr[QStringLiteral("shuffle")]     = rgbToHex(accent3Visible);
        mr[QStringLiteral("toggleShade")] = rgbToHex(paused);
        theme[QStringLiteral("mediaroom")] = mr;
    }
    {
        QJsonObject mm;
        mm[QStringLiteral("mediaContainer")] = rgbToHex(adjustBrightness(accent2Visible, 0.6));
        theme[QStringLiteral("mainmenu")]    = mm;
    }
    {
        QJsonObject obd;
        obd[QStringLiteral("boxBackground")] = rgbToHex(baseAlt);
        obd[QStringLiteral("barColor")]      = rgbToHex(ensureIconContrast(accent4, baseAlt));
        obd[QStringLiteral("labelColor")]    = rgbToHex(getSecondaryTextColor(baseAlt));
        obd[QStringLiteral("valueColor")]    = rgbToHex(getReadableTextColor(baseAlt));
        theme[QStringLiteral("obd")]         = obd;
    }

    return QString::fromUtf8(QJsonDocument(theme).toJson(QJsonDocument::Compact));
}

#endif // Q_OS_MOBILE
