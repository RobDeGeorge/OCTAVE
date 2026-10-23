// folderbrowser.h
//
// Folder browsing for a frontend with no native folder picker (a web or
// remote UI choosing the music folder): one directory's sub-folders and
// audio files, by name. Confined to `roots` (the home folder, the Music
// location, removable-media mounts, the current music folder; see
// MediaManager::browse_music_folders): a path whose canonical form (every
// symlink resolved) is not inside a root is refused, so neither ".." nor
// a link leads out. Hidden entries (a leading '.') are never listed,
// unreadable directories are an error, and a listing stops at kMaxEntries.
// Names only: no sizes, dates or contents.
//
// Pure, for tst_folderbrowser.

#ifndef FOLDERBROWSER_H
#define FOLDERBROWSER_H

#include <QStringList>
#include <QVariantMap>

namespace FolderBrowser {

constexpr int kMaxEntries = 2000;

// `path` empty: the roots, as {roots: [{name, path}]}. Otherwise
// {path (canonical), parent ("" at a root), dirs: [name], audioFiles:
// [name], truncated} or {error}. `audioExtensions` like ".mp3".
QVariantMap list(const QString &path, const QStringList &roots, const QStringList &audioExtensions);

// `path`'s canonical form if it exists and lies inside one of `roots`
// (canonical too), else ""
QString confine(const QString &path, const QStringList &roots);

} // namespace FolderBrowser

#endif // FOLDERBROWSER_H
