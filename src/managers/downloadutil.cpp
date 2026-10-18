#include "downloadutil.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#if !defined(Q_OS_MOBILE) && !defined(Q_OS_WIN)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace DownloadUtil {

QJsonArray parseSearchOutput(const QByteArray &rawOutput)
{
    QJsonArray results;
    const QList<QByteArray> lines = rawOutput.split('\n');

    for (const QByteArray &line : lines) {
        QByteArray trimmedLine = line.trimmed();
        if (trimmedLine.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(trimmedLine, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        QJsonObject obj = doc.object();

        QString videoId = obj.value(QStringLiteral("id")).toString();
        if (videoId.isEmpty()) {
            continue;
        }

        QString title = obj.value(QStringLiteral("title")).toString();

        QString channel = obj.value(QStringLiteral("channel")).toString();
        if (channel.isEmpty()) {
            channel = obj.value(QStringLiteral("uploader")).toString();
        }
        // Try additional artist fields returned by YouTube Music extractor.
        if (channel.isEmpty()) {
            channel = obj.value(QStringLiteral("artist")).toString();
        }
        if (channel.isEmpty()) {
            channel = obj.value(QStringLiteral("creator")).toString();
        }

        // Duration in seconds (yt-dlp gives numeric or null)
        int duration = 0;
        QJsonValue durVal = obj.value(QStringLiteral("duration"));
        if (durVal.isDouble()) {
            duration = static_cast<int>(durVal.toDouble());
        }

        // Thumbnail URL — prefer the last (highest resolution) thumbnail
        QString coverUrl;
        QJsonArray thumbnails = obj.value(QStringLiteral("thumbnails")).toArray();
        if (!thumbnails.isEmpty()) {
            coverUrl = thumbnails.last().toObject().value(QStringLiteral("url")).toString();
        }
        if (coverUrl.isEmpty()) {
            coverUrl = obj.value(QStringLiteral("thumbnail")).toString();
        }
        // Request high-res from Google's CDN if applicable
        if (coverUrl.contains(QStringLiteral("lh3.googleusercontent.com"))) {
            static const QRegularExpression resCropRe(QStringLiteral("=w\\d+-h\\d+.*$"));
            coverUrl.replace(resCropRe, QStringLiteral("=w800-h800-l90-rj"));
        }

        // Album name — yt-dlp flat-playlist may include album in metadata
        QString albumName = obj.value(QStringLiteral("album")).toString();

        // Year — extract from upload_date (YYYYMMDD) or release_year
        int year = 0;
        QJsonValue releaseYear = obj.value(QStringLiteral("release_year"));
        if (releaseYear.isDouble()) {
            year = static_cast<int>(releaseYear.toDouble());
        }
        if (year == 0) {
            QString uploadDate = obj.value(QStringLiteral("upload_date")).toString();
            if (uploadDate.length() >= 4) {
                bool ok = false;
                int y = uploadDate.left(4).toInt(&ok);
                if (ok) {
                    year = y;
                }
            }
        }

        // Build the result object matching Python's format
        QJsonObject result;
        result.insert(QStringLiteral("name"), title);
        result.insert(QStringLiteral("artist"), channel);
        result.insert(QStringLiteral("artists"), QJsonArray({channel}));
        result.insert(QStringLiteral("album_name"), albumName);
        result.insert(QStringLiteral("duration"), duration);
        result.insert(QStringLiteral("year"), year);
        result.insert(QStringLiteral("cover_url"), coverUrl);
        result.insert(QStringLiteral("url"),
                       QStringLiteral("https://music.youtube.com/watch?v=") + videoId);
        result.insert(QStringLiteral("song_id"), videoId);
        result.insert(QStringLiteral("explicit"), false);
        result.insert(QStringLiteral("track_number"), 0);
        result.insert(QStringLiteral("disc_number"), 0);

        results.append(result);
    }

    return results;
}

QMap<QString, QString> scanDownloadedFiles(const QStringList &roots, const QStringList &audioExtensions)
{
    // QDirIterator's default filter leaves hidden entries out, so the
    // .octave-partial staging dir (and anything a power cut left in it)
    // never counts as downloaded
    QMap<QString, QString> names;
    for (const QString &root : roots) {
        QDir rootDir(root);
        if (!rootDir.exists()) continue;

        QDirIterator it(root, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (!fi.isFile()) continue;

            const QString suffix = QStringLiteral(".") + fi.suffix().toLower();
            if (!audioExtensions.contains(suffix)) continue;

            QString folder = fi.dir().dirName();
            // Root-level files belong to "Unsorted"
            if (QDir::cleanPath(fi.dir().absolutePath()) == QDir::cleanPath(root)) {
                folder = QStringLiteral("Unsorted");
            }
            names.insert(fi.completeBaseName().toLower(), folder);
        }
    }
    return names;
}

#ifndef Q_OS_MOBILE
// ═══════════════════════════════════════════════════════════════════
// Download staging (desktop)
//
// yt-dlp and ffmpeg write each song into <downloadPath>/.octave-partial/<id>/
// and the finished, tagged file is renamed into the library in one step.
// The head unit loses power with the ignition; writing straight to the
// library left truncated songs that yt-dlp later reported as "already
// downloaded". The staging dir is hidden, so neither backend's library scan
// descends into it.
// ═══════════════════════════════════════════════════════════════════

QString stagingDirFor(const QString &dlPath, const QString &songId)
{
    QString tag = songId;
    tag.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    if (tag.isEmpty())
        tag = QStringLiteral("song");
    return QDir(dlPath).filePath(kStagingDirName + QLatin1Char('/') + tag);
}

// Delete a song's staging dir. Refuses any path outside a staging root, so
// an empty or unexpected path can never wipe a real folder.
void removeStagingDir(const QString &dir)
{
    if (dir.isEmpty() || !QDir::cleanPath(dir).contains(QLatin1Char('/') + kStagingDirName))
        return;
    QDir(dir).removeRecursively();
}

// fsync the staged file, then rename it over finalPath (atomic on POSIX)
bool publishStagedFile(const QString &staged, const QString &finalPath, QString *error)
{
#ifndef Q_OS_WIN
    const QByteArray from = QFile::encodeName(staged);
    const QByteArray to = QFile::encodeName(finalPath);
    int fd = ::open(from.constData(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
    if (::rename(from.constData(), to.constData()) != 0) {
        *error = qt_error_string(errno);
        return false;
    }
    // Persist the rename itself (the directory entry)
    const QByteArray dir = QFile::encodeName(QFileInfo(finalPath).absolutePath());
    fd = ::open(dir.constData(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
    return true;
#else
    // Qt has no atomic replace for an existing file; Windows isn't the
    // power-cut target, so remove-then-rename is good enough here.
    QFile::remove(finalPath);
    QFile file(staged);
    if (!file.rename(finalPath)) {
        *error = file.errorString();
        return false;
    }
    return true;
#endif
}
#endif // Q_OS_MOBILE

} // namespace DownloadUtil
