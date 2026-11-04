// SpotifyApi: the pure half of SpotifyManager. PKCE against RFC 7636's
// example, the sign-in URL, and reading the Web API's JSON in both the
// pre-2026 shape and the February 2026 one (playlist "tracks" -> "items",
// row "track" -> "item").

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>
#include <QUrlQuery>

#include "managers/spotifyapi.h"

namespace {
QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

const char *kTrack = R"({"uri":"spotify:track:t1","name":"Song","duration_ms":200000,
    "artists":[{"name":"A"},{"name":"B"}],
    "album":{"name":"Album","images":[{"url":"big","width":640},{"url":"mid","width":300},{"url":"small","width":64}]}})";
} // namespace

class TstSpotifyApi : public QObject
{
    Q_OBJECT

private slots:
    void pkceMatchesTheRfcExample()
    {
        // RFC 7636, appendix B
        QCOMPARE(SpotifyApi::challengeFor(QStringLiteral("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk")),
                 QStringLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
    }

    void verifierIsLongRandomAndUnreserved()
    {
        const QString a = SpotifyApi::makeVerifier();
        const QString b = SpotifyApi::makeVerifier();
        QVERIFY(a.size() >= 43 && a.size() <= 128);
        QVERIFY(a != b);
        static const QRegularExpression unreserved(QStringLiteral("^[A-Za-z0-9._~-]+$"));
        QVERIFY(unreserved.match(a).hasMatch());
    }

    void authorizeUrlCarriesEverything()
    {
        const QUrl url = SpotifyApi::authorizeUrl(QStringLiteral("cid"), QStringLiteral("http://127.0.0.1:43821/callback"),
                                                  QStringLiteral("chal"), QStringLiteral("st"));
        QCOMPARE(url.host(), QStringLiteral("accounts.spotify.com"));
        QCOMPARE(url.path(), QStringLiteral("/authorize"));
        const QUrlQuery q(url);
        QCOMPARE(q.queryItemValue(QStringLiteral("client_id")), QStringLiteral("cid"));
        QCOMPARE(q.queryItemValue(QStringLiteral("response_type")), QStringLiteral("code"));
        QCOMPARE(q.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded),
                 QStringLiteral("http://127.0.0.1:43821/callback"));
        QCOMPARE(q.queryItemValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
        QCOMPARE(q.queryItemValue(QStringLiteral("code_challenge")), QStringLiteral("chal"));
        QCOMPARE(q.queryItemValue(QStringLiteral("state")), QStringLiteral("st"));
        const QStringList scopes = q.queryItemValue(QStringLiteral("scope"), QUrl::FullyDecoded).split(QLatin1Char(' '));
        QVERIFY(scopes.contains(QStringLiteral("user-modify-playback-state")));
        QVERIFY(scopes.contains(QStringLiteral("user-library-read")));
        // The redirect is encoded, not left raw in the query
        QVERIFY(url.toString(QUrl::FullyEncoded).contains(QStringLiteral("redirect_uri=http%3A%2F%2F127.0.0.1")));
    }

    void readsSavedTracksPlaylistRowsOfBothShapesAndAlbumTracks()
    {
        const QJsonObject track = json(kTrack);
        QJsonArray rows;
        rows << QJsonObject{{"track", track}};                                   // saved / old playlist row
        rows << QJsonObject{{"item", track}, {"track", true}};                   // 2026 playlist row
        rows << track;                                                           // an album's own track
        rows << QJsonObject{{"track", QJsonObject{{"uri", "spotify:local:x"}}}}; // local file: skipped
        rows << QJsonObject{{"item", QJsonValue()}};                             // removed: skipped
        const QVariantList out = SpotifyApi::readTracks(QJsonObject{{"items", rows}});
        QCOMPARE(out.size(), 3);
        for (const QVariant &v : out) {
            const QVariantMap m = v.toMap();
            QCOMPARE(m.value("uri").toString(), QStringLiteral("spotify:track:t1"));
            QCOMPARE(m.value("title").toString(), QStringLiteral("Song"));
            QCOMPARE(m.value("artist").toString(), QStringLiteral("A, B"));
            QCOMPARE(m.value("album").toString(), QStringLiteral("Album"));
            QCOMPARE(m.value("durationMs").toInt(), 200000);
            QCOMPARE(m.value("imageUrl").toString(), QStringLiteral("small"));
        }
    }

    void albumTracksTakeTheAlbumsCover()
    {
        const QJsonObject t = json(R"({"uri":"spotify:track:t2","name":"X","duration_ms":1,"artists":[]})");
        const QVariantList out = SpotifyApi::readTracks(QJsonObject{{"items", QJsonArray{t}}}, QStringLiteral("cover"));
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.first().toMap().value("imageUrl").toString(), QStringLiteral("cover"));
    }

    void playlistRows()
    {
        const QVariantMap mine = SpotifyApi::readPlaylist(
            json(R"({"uri":"spotify:playlist:p1","id":"p1","name":"Road","owner":{"id":"me","display_name":"Me"},
                     "items":{"total":12},"images":[{"url":"cover","width":null}]})"),
            QStringLiteral("me"));
        QCOMPARE(mine.value("name").toString(), QStringLiteral("Road"));
        QCOMPARE(mine.value("trackCount").toInt(), 12);
        QCOMPARE(mine.value("imageUrl").toString(), QStringLiteral("cover"));
        QVERIFY(mine.value("tracksListed").toBool());

        // Followed, pre-2026 "tracks" count: playable, not listed
        const QVariantMap theirs = SpotifyApi::readPlaylist(
            json(R"({"uri":"spotify:playlist:p2","name":"Hits","owner":{"id":"spotify"},"tracks":{"total":50}})"),
            QStringLiteral("me"));
        QCOMPARE(theirs.value("trackCount").toInt(), 50);
        QVERIFY(!theirs.value("tracksListed").toBool());

        const QVariantMap shared = SpotifyApi::readPlaylist(
            json(R"({"uri":"spotify:playlist:p3","owner":{"id":"friend"},"collaborative":true})"), QStringLiteral("me"));
        QVERIFY(shared.value("tracksListed").toBool());
    }

    void albumRows()
    {
        const QVariantMap a = SpotifyApi::readAlbum(json(
            R"({"album":{"uri":"spotify:album:a1","name":"LP","artists":[{"name":"Band"}],"total_tracks":9,"images":[]}})"));
        QCOMPARE(a.value("uri").toString(), QStringLiteral("spotify:album:a1"));
        QCOMPARE(a.value("owner").toString(), QStringLiteral("Band"));
        QCOMPARE(a.value("trackCount").toInt(), 9);
        QCOMPARE(a.value("kind").toString(), QStringLiteral("album"));
    }

    void player()
    {
        QJsonObject p{{"is_playing", true},
                      {"progress_ms", 1234},
                      {"shuffle_state", true},
                      {"repeat_state", "context"},
                      {"context", QJsonObject{{"uri", "spotify:playlist:p1"}}},
                      {"device", QJsonObject{{"id", "d1"}, {"name", "Phone"}, {"volume_percent", 42}}},
                      {"item", json(kTrack)}};
        const SpotifyApi::PlayerState s = SpotifyApi::readPlayer(p);
        QVERIFY(s.valid);
        QVERIFY(s.playing);
        QCOMPARE(s.positionMs, 1234);
        QCOMPARE(s.durationMs, 200000);
        QCOMPARE(s.title, QStringLiteral("Song"));
        QCOMPARE(s.artist, QStringLiteral("A, B"));
        QCOMPARE(s.imageUrl, QStringLiteral("big"));
        QCOMPARE(s.contextUri, QStringLiteral("spotify:playlist:p1"));
        QCOMPARE(s.deviceName, QStringLiteral("Phone"));
        QVERIFY(s.shuffle);
        QCOMPARE(s.repeat, QStringLiteral("context"));
        QCOMPARE(s.volumePercent, 42);
        QVERIFY(s.supportsVolume);   // not said: assume it can

        // A phone that keeps its volume to itself, volume not reported
        const SpotifyApi::PlayerState locked = SpotifyApi::readPlayer(json(
            R"({"item":{"uri":"spotify:track:t","name":"x"},"device":{"name":"iPhone","supports_volume":false,"volume_percent":null}})"));
        QVERIFY(!locked.supportsVolume);
        QCOMPARE(locked.volumePercent, -1);

        // An ad or nothing loaded: no item
        QVERIFY(!SpotifyApi::readPlayer(QJsonObject{{"is_playing", true}}).valid);

        // A podcast episode: the show stands in for the artist
        const SpotifyApi::PlayerState ep = SpotifyApi::readPlayer(json(
            R"({"is_playing":false,"item":{"uri":"spotify:episode:e1","name":"Ep","duration_ms":5,
                "show":{"name":"Show"},"images":[{"url":"ep","width":640}]}})"));
        QVERIFY(ep.valid);
        QCOMPARE(ep.artist, QStringLiteral("Show"));
        QCOMPARE(ep.imageUrl, QStringLiteral("ep"));
    }

    void devicesSkipRestricted()
    {
        const QVariantList d = SpotifyApi::readDevices(json(
            R"({"devices":[{"id":"a","name":"Phone","type":"Smartphone","is_active":true},
                           {"id":"b","name":"TV","is_restricted":true},{"id":null,"name":"Ghost"}]})"));
        QCOMPARE(d.size(), 1);
        QCOMPARE(d.first().toMap().value("name").toString(), QStringLiteral("Phone"));
        QVERIFY(d.first().toMap().value("active").toBool());
    }

    void wakesTheActiveDeviceThenAPhone()
    {
        const auto dev = [](const char *id, const char *type, bool active = false) {
            return QVariantMap{{QStringLiteral("id"), QString::fromLatin1(id)},
                               {QStringLiteral("type"), QString::fromLatin1(type)},
                               {QStringLiteral("active"), active}};
        };
        QCOMPARE(SpotifyApi::pickDevice({}), QString());
        QCOMPARE(SpotifyApi::pickDevice({dev("hifi", "Speaker")}), QStringLiteral("hifi"));
        QCOMPARE(SpotifyApi::pickDevice({dev("hifi", "Speaker"), dev("phone", "Smartphone")}), QStringLiteral("phone"));
        QCOMPARE(SpotifyApi::pickDevice({dev("phone", "Smartphone"), dev("pc", "Computer", true)}), QStringLiteral("pc"));
    }

    void playBodyAimsAtTheTappedTrack()
    {
        const auto track = [](const char *id) {
            return QVariantMap{{QStringLiteral("uri"), QStringLiteral("spotify:track:") + QLatin1String(id)}};
        };
        const QVariantList rows{track("a"), track("b")};
        const QString list = QStringLiteral("spotify:playlist:1");

        // By uri: a playlist's local files have no row, so the row number is off
        QCOMPARE(SpotifyApi::playBody(list, 1, rows)
                     .value(QStringLiteral("offset")).toObject().value(QStringLiteral("uri")).toString(),
                 QStringLiteral("spotify:track:b"));
        // A list whose rows aren't known (a followed playlist)
        QVERIFY(!SpotifyApi::playBody(list, -1, {}).contains(QStringLiteral("offset")));
        QCOMPARE(SpotifyApi::playBody(list, 3, {})
                     .value(QStringLiteral("offset")).toObject().value(QStringLiteral("position")).toInt(), 3);
        // Liked Songs: the listed tracks from the tapped one
        const QJsonArray uris = SpotifyApi::playBody(SpotifyApi::likedUri(QStringLiteral("me")), 1, rows)
                                    .value(QStringLiteral("uris")).toArray();
        QCOMPARE(uris.size(), 1);
        QCOMPARE(uris.at(0).toString(), QStringLiteral("spotify:track:b"));
    }

    void uris()
    {
        QCOMPARE(SpotifyApi::idOf(QStringLiteral("spotify:playlist:abc")), QStringLiteral("abc"));
        QCOMPARE(SpotifyApi::likedUri(QStringLiteral("me")), QStringLiteral("spotify:user:me:collection"));
        QVERIFY(SpotifyApi::isLiked(SpotifyApi::likedUri(QStringLiteral("me"))));
        QVERIFY(!SpotifyApi::isLiked(QStringLiteral("spotify:playlist:abc")));
    }
};

QTEST_GUILESS_MAIN(TstSpotifyApi)
#include "tst_spotifyapi.moc"
