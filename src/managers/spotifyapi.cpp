#include "spotifyapi.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QRandomGenerator>

#include <cmath>
#include <limits>

namespace SpotifyApi {

QStringList scopes()
{
    return {QStringLiteral("user-library-read"),           QStringLiteral("playlist-read-private"),
            QStringLiteral("playlist-read-collaborative"), QStringLiteral("user-read-playback-state"),
            QStringLiteral("user-modify-playback-state"),  QStringLiteral("user-read-currently-playing")};
}

namespace {
QString randomText(int length)
{
    static const char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QString out;
    out.reserve(length);
    auto *rng = QRandomGenerator::system();
    for (int i = 0; i < length; ++i)
        out += QLatin1Char(kChars[rng->bounded(int(sizeof(kChars) - 1))]);
    return out;
}
} // namespace

QString makeVerifier()
{
    return randomText(64);
}

QString challengeFor(const QString &verifier)
{
    const QByteArray hash = QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString makeState()
{
    return randomText(24);
}

QUrl authorizeUrl(const QString &clientId, const QString &redirectUri, const QString &challenge,
                  const QString &state)
{
    // Encoded by hand: QUrlQuery leaves ':' '/' in the redirect URI as is
    auto pair = [](const char *key, const QString &value) {
        return QString::fromLatin1(key) + QLatin1Char('=') + QString::fromLatin1(QUrl::toPercentEncoding(value));
    };
    const QStringList parts = {pair("client_id", clientId),
                               pair("response_type", QStringLiteral("code")),
                               pair("redirect_uri", redirectUri),
                               pair("code_challenge_method", QStringLiteral("S256")),
                               pair("code_challenge", challenge),
                               pair("state", state),
                               pair("scope", scopes().join(QLatin1Char(' ')))};
    QUrl url(QStringLiteral("https://accounts.spotify.com/authorize"));
    url.setQuery(parts.join(QLatin1Char('&')), QUrl::StrictMode);
    return url;
}

QString pickImage(const QJsonArray &images, int want)
{
    QString best;
    int bestDiff = std::numeric_limits<int>::max();
    for (const QJsonValue &v : images) {
        const QJsonObject img = v.toObject();
        const QString url = img.value(QStringLiteral("url")).toString();
        if (url.isEmpty())
            continue;
        // Unknown size (user-uploaded playlist covers): take it if nothing else
        const int w = img.value(QStringLiteral("width")).toInt(0);
        const int diff = w > 0 ? std::abs(w - want) : std::numeric_limits<int>::max() - 1;
        if (diff < bestDiff) {
            bestDiff = diff;
            best = url;
        }
    }
    return best;
}

QString joinArtists(const QJsonArray &artists)
{
    QStringList names;
    for (const QJsonValue &a : artists) {
        const QString n = a.toObject().value(QStringLiteral("name")).toString();
        if (!n.isEmpty())
            names << n;
    }
    return names.join(QStringLiteral(", "));
}

QVariantList readTracks(const QJsonObject &page, const QString &albumImage)
{
    QVariantList out;
    for (const QJsonValue &v : page.value(QStringLiteral("items")).toArray()) {
        const QJsonObject row = v.toObject();
        // Saved tracks / old playlist rows: {track}; 2026 playlist rows:
        // {item}; an album's tracks: the track itself. "track" is a bool
        // on some 2026 rows (is this item a track), so check the type.
        QJsonObject t;
        if (row.value(QStringLiteral("item")).isObject())
            t = row.value(QStringLiteral("item")).toObject();
        else if (row.value(QStringLiteral("track")).isObject())
            t = row.value(QStringLiteral("track")).toObject();
        else
            t = row;
        const QString uri = t.value(QStringLiteral("uri")).toString();
        if (!uri.startsWith(QLatin1String("spotify:track:")))
            continue;   // local files, episodes, removed tracks
        const QJsonObject album = t.value(QStringLiteral("album")).toObject();
        QString image = pickImage(album.value(QStringLiteral("images")).toArray(), 64);
        if (image.isEmpty())
            image = albumImage;
        QVariantMap m;
        m[QStringLiteral("uri")] = uri;
        m[QStringLiteral("title")] = t.value(QStringLiteral("name")).toString();
        m[QStringLiteral("artist")] = joinArtists(t.value(QStringLiteral("artists")).toArray());
        m[QStringLiteral("album")] = album.value(QStringLiteral("name")).toString();
        m[QStringLiteral("durationMs")] = t.value(QStringLiteral("duration_ms")).toInt();
        m[QStringLiteral("imageUrl")] = image;
        out << m;
    }
    return out;
}

QVariantMap readPlaylist(const QJsonObject &p, const QString &userId)
{
    const QJsonObject owner = p.value(QStringLiteral("owner")).toObject();
    const QString ownerId = owner.value(QStringLiteral("id")).toString();
    // 2026: "items" ({href, total}); before: "tracks"
    QJsonObject counts = p.value(QStringLiteral("items")).toObject();
    if (counts.isEmpty())
        counts = p.value(QStringLiteral("tracks")).toObject();
    QVariantMap m;
    m[QStringLiteral("uri")] = p.value(QStringLiteral("uri")).toString();
    m[QStringLiteral("id")] = p.value(QStringLiteral("id")).toString();
    m[QStringLiteral("name")] = p.value(QStringLiteral("name")).toString();
    m[QStringLiteral("owner")] = owner.value(QStringLiteral("display_name")).toString();
    m[QStringLiteral("imageUrl")] = pickImage(p.value(QStringLiteral("images")).toArray());
    m[QStringLiteral("trackCount")] = counts.value(QStringLiteral("total")).toInt();
    m[QStringLiteral("kind")] = QStringLiteral("playlist");
    m[QStringLiteral("tracksListed")] =
        (!userId.isEmpty() && ownerId == userId) || p.value(QStringLiteral("collaborative")).toBool();
    return m;
}

QVariantMap readAlbum(const QJsonObject &row)
{
    const QJsonObject a = row.value(QStringLiteral("album")).isObject() ? row.value(QStringLiteral("album")).toObject()
                                                                         : row;
    QVariantMap m;
    m[QStringLiteral("uri")] = a.value(QStringLiteral("uri")).toString();
    m[QStringLiteral("id")] = a.value(QStringLiteral("id")).toString();
    m[QStringLiteral("name")] = a.value(QStringLiteral("name")).toString();
    m[QStringLiteral("owner")] = joinArtists(a.value(QStringLiteral("artists")).toArray());
    m[QStringLiteral("imageUrl")] = pickImage(a.value(QStringLiteral("images")).toArray());
    m[QStringLiteral("trackCount")] = a.value(QStringLiteral("total_tracks")).toInt();
    m[QStringLiteral("kind")] = QStringLiteral("album");
    m[QStringLiteral("tracksListed")] = true;
    return m;
}

PlayerState readPlayer(const QJsonObject &player)
{
    PlayerState s;
    const QJsonObject item = player.value(QStringLiteral("item")).toObject();
    if (item.isEmpty())
        return s;
    s.valid = true;
    s.playing = player.value(QStringLiteral("is_playing")).toBool();
    s.positionMs = player.value(QStringLiteral("progress_ms")).toInt();
    s.durationMs = item.value(QStringLiteral("duration_ms")).toInt();
    s.trackUri = item.value(QStringLiteral("uri")).toString();
    s.title = item.value(QStringLiteral("name")).toString();
    const QJsonObject album = item.value(QStringLiteral("album")).toObject();
    if (!album.isEmpty()) {
        s.artist = joinArtists(item.value(QStringLiteral("artists")).toArray());
        s.album = album.value(QStringLiteral("name")).toString();
        s.imageUrl = pickImage(album.value(QStringLiteral("images")).toArray(), 640);
    } else {
        // A podcast episode: the show stands in for artist and album
        const QJsonObject show = item.value(QStringLiteral("show")).toObject();
        s.artist = show.value(QStringLiteral("name")).toString();
        s.album = s.artist;
        s.imageUrl = pickImage(item.value(QStringLiteral("images")).toArray(), 640);
    }
    s.contextUri = player.value(QStringLiteral("context")).toObject().value(QStringLiteral("uri")).toString();
    const QJsonObject device = player.value(QStringLiteral("device")).toObject();
    s.deviceId = device.value(QStringLiteral("id")).toString();
    s.deviceName = device.value(QStringLiteral("name")).toString();
    s.volumePercent = device.value(QStringLiteral("volume_percent")).toInt(-1);
    s.supportsVolume = device.value(QStringLiteral("supports_volume")).toBool(true);
    s.shuffle = player.value(QStringLiteral("shuffle_state")).toBool();
    const QString repeat = player.value(QStringLiteral("repeat_state")).toString();
    if (!repeat.isEmpty())
        s.repeat = repeat;
    return s;
}

QVariantList readDevices(const QJsonObject &reply)
{
    QVariantList out;
    for (const QJsonValue &v : reply.value(QStringLiteral("devices")).toArray()) {
        const QJsonObject d = v.toObject();
        if (d.value(QStringLiteral("id")).toString().isEmpty() || d.value(QStringLiteral("is_restricted")).toBool())
            continue;   // a restricted device takes no commands
        QVariantMap m;
        m[QStringLiteral("id")] = d.value(QStringLiteral("id")).toString();
        m[QStringLiteral("name")] = d.value(QStringLiteral("name")).toString();
        m[QStringLiteral("type")] = d.value(QStringLiteral("type")).toString();
        m[QStringLiteral("active")] = d.value(QStringLiteral("is_active")).toBool();
        out << m;
    }
    return out;
}

QString idOf(const QString &uri)
{
    return uri.section(QLatin1Char(':'), -1);
}

QString likedUri(const QString &userId)
{
    return QStringLiteral("spotify:user:%1:collection").arg(userId);
}

bool isLiked(const QString &uri)
{
    return uri.startsWith(QLatin1String("spotify:user:")) && uri.endsWith(QLatin1String(":collection"));
}

} // namespace SpotifyApi
