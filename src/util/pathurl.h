// pathurl.h
//
// Turning what a file or folder picker hands back into something to open.
// FolderDialog gives a file:// URL on desktop (Qt handles Windows drive
// letters) and a content:// tree URI on Android (Storage Access Framework).
// OCTAVE reads the music folder with QDir under All Files Access, so a tree
// URI is mapped back to its /storage path; a FileDialog's content:// file
// URI, by contrast, goes to QFile as it is (Qt's Android file engine opens it).
// Header-only: SettingsManager uses it and is linked into many test targets.

#ifndef PATHURL_H
#define PATHURL_H

#include <QString>
#include <QUrl>

namespace PathUrl {

// A picked folder as a filesystem path: file: -> local path; an Android
// external-storage tree -> /storage/emulated/0/<dir> (primary) or
// /storage/<volume>/<dir> (an SD card); "" for anything else
inline QString folderPath(const QUrl &url)
{
    if (url.isLocalFile()) {
        QString path = url.toLocalFile();
        // file:///C:/x is "C:/x" on Windows; elsewhere Qt keeps the slash
        if (path.size() > 2 && path.at(0) == QLatin1Char('/') && path.at(2) == QLatin1Char(':') && path.at(1).isLetter())
            path.remove(0, 1);
        return path;
    }
    if (url.scheme() != QLatin1String("content")
        || url.host() != QLatin1String("com.android.externalstorage.documents"))
        return {};
    // .../tree/<volume>%3A<dir>[/document/...]: the tree id is one encoded
    // segment ("primary%3AMusic%2FRock"), so split before decoding
    const QString encoded = url.path(QUrl::FullyEncoded);
    const int at = encoded.indexOf(QLatin1String("/tree/"));
    if (at < 0)
        return {};
    QString treeId = encoded.mid(at + 6);
    treeId = QUrl::fromPercentEncoding(treeId.left(treeId.indexOf(QLatin1Char('/'))).toUtf8());
    const int colon = treeId.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return {};
    const QString volume = treeId.left(colon);
    const QString dir = treeId.mid(colon + 1);
    const QString root = volume == QLatin1String("primary") ? QStringLiteral("/storage/emulated/0")
                                                             : QStringLiteral("/storage/") + volume;
    return dir.isEmpty() ? root : root + QLatin1Char('/') + dir;
}

// A picked file to hand to QFile: file: -> local path, anything else
// (content://, a plain path) unchanged
inline QString openablePath(const QString &fileOrUrl)
{
    const QUrl url(fileOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : fileOrUrl;
}

} // namespace PathUrl

#endif // PATHURL_H
