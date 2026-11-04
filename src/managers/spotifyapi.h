#ifndef SPOTIFYAPI_H
#define SPOTIFYAPI_H

// The pure half of SpotifyManager: PKCE, the sign-in URL and reading the Web
// API's JSON into the rows QML shows. No network here (tst_spotifyapi).
//
// Spotify's February 2026 changes for Development Mode apps renamed the
// playlist routes and fields (/playlists/{id}/tracks -> /items, "tracks" ->
// "items", a row's "track" -> "item"); the readers take both shapes.

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace SpotifyApi {

// What the sign-in asks for: the library (read only) and the player
QStringList scopes();

// PKCE (RFC 7636): a random verifier and its S256 challenge
QString makeVerifier();
QString challengeFor(const QString &verifier);
QString makeState();

QUrl authorizeUrl(const QString &clientId, const QString &redirectUri, const QString &challenge,
                  const QString &state);

// A page's "items" rows as tracks: {uri, title, artist, album, durationMs,
// imageUrl}. Takes saved-track rows ({track}), playlist rows ({item} or
// {track}) and plain tracks (an album's). Local files and episodes without a
// uri are skipped. `albumImage` is used when a row has no album art (an
// album's own tracks carry none).
QVariantList readTracks(const QJsonObject &page, const QString &albumImage = QString());

// A playlist object as a row: {uri, id, name, owner, imageUrl, trackCount,
// kind: "playlist", tracksListed}. tracksListed: Spotify lists the songs of
// playlists the user owns or collaborates on; others can only be played.
QVariantMap readPlaylist(const QJsonObject &playlist, const QString &userId);
// A saved-album row ({album}) or an album: {uri, id, name, owner (artist),
// imageUrl, trackCount, kind: "album", tracksListed: true}
QVariantMap readAlbum(const QJsonObject &row);

// The image closest to `want` px wide (Spotify lists 640 / 300 / 64)
QString pickImage(const QJsonArray &images, int want = 300);
QString joinArtists(const QJsonArray &artists);

// GET /me/player
struct PlayerState
{
    bool valid = false;        // false: nothing to report (204, or no item)
    bool playing = false;
    int positionMs = 0;
    int durationMs = 0;
    QString trackUri, title, artist, album, imageUrl;
    QString contextUri;
    QString deviceId, deviceName;
    // The device's volume (-1: not reported) and whether Spotify may set it
    // (phones that keep their volume to themselves say false)
    int volumePercent = -1;
    bool supportsVolume = true;
    bool shuffle = false;
    QString repeat = QStringLiteral("off");   // "off" | "context" | "track"
};
PlayerState readPlayer(const QJsonObject &player);

// GET /me/player/devices: [{id, name, type, active}]
QVariantList readDevices(const QJsonObject &reply);
// The device to wake when nothing is active: the active one if any, then a
// phone (the one in the car, not a speaker at home), then the first ("": none)
QString pickDevice(const QVariantList &devices);
// The PUT /me/player/play body: a collection from row `index` (-1: from the
// top). `tracks` are its listed rows, empty when they aren't known
QJsonObject playBody(const QString &uri, int index, const QVariantList &tracks);

// "spotify:playlist:abc" -> "abc"
QString idOf(const QString &uri);
// The Liked Songs context ("spotify:user:<id>:collection")
QString likedUri(const QString &userId);
bool isLiked(const QString &uri);

} // namespace SpotifyApi

#endif // SPOTIFYAPI_H
