#ifndef DOWNLOADUTIL_H
#define DOWNLOADUTIL_H

// The parts of DownloadManager that don't need a process, the network or
// TagLib: parsing yt-dlp search output, finding songs already in the library,
// and the power-cut-safe staging of downloads. Split out so tests/cpp can
// exercise them directly.

#include <QByteArray>
#include <QJsonArray>
#include <QMap>
#include <QString>
#include <QStringList>

namespace DownloadUtil {

// yt-dlp --dump-json / --flat-playlist output (one JSON object per line) to
// the result objects the QML download page binds to (name, artist, artists,
// album_name, duration, year, cover_url, url, song_id, ...). Lines that aren't
// JSON objects with an id are skipped; 60px Google CDN thumbnails are upgraded
// to 800px.
QJsonArray parseSearchOutput(const QByteArray &rawOutput);

// Audio files under `roots` (recursively; hidden dirs such as the staging
// dir are skipped): lower-case base name -> folder name ("Unsorted" for a
// file directly in its root).
QMap<QString, QString> scanDownloadedFiles(const QStringList &roots, const QStringList &audioExtensions);

#ifndef Q_OS_MOBILE
// Hidden dir under the download path that songs are written into until done
inline const QString kStagingDirName = QStringLiteral(".octave-partial");

// <dlPath>/.octave-partial/<songId, sanitized>
QString stagingDirFor(const QString &dlPath, const QString &songId);
// Deletes a song's staging dir; refuses anything outside a staging root
void removeStagingDir(const QString &dir);
// fsync the staged file, then rename it over finalPath (atomic on POSIX)
bool publishStagedFile(const QString &staged, const QString &finalPath, QString *error);
#endif

} // namespace DownloadUtil

#endif // DOWNLOADUTIL_H
