// staticfiles.h
//
// --web-root DIR / the serviceWebRoot setting: a directory of static files
// served at GET / on the service port in place of the embedded reference
// client (sdk/web), so a third-party web frontend is same-origin with the
// socket and passes the Origin rule (websocket.h).
//
// What it refuses, assuming a hostile page or local client picks the path:
// a ".." segment (also percent-encoded), a backslash or NUL, any segment
// starting with "." (.git, .env), a path that resolves outside the root
// through a symlink (canonical path checked, the root's own included), and
// files over kMaxFile. Unknown extensions are served as
// application/octet-stream with nosniff, never guessed. A path with no
// extension that names nothing gets index.html (single-page apps' client
// routes); a missing file with an extension is a 404.
//
// Pure (no sockets), so tst_staticfiles covers it.

#ifndef OCTAVE_SERVICE_STATICFILES_H
#define OCTAVE_SERVICE_STATICFILES_H

#include <QByteArray>
#include <QString>

namespace OctaveService::StaticFiles {

constexpr qint64 kMaxFile = 64 * 1024 * 1024;

struct Reply {
    int status = 404;
    QByteArray reason = "Not Found";
    QByteArray contentType = "text/plain";
    QByteArray body = "not found\n";
};

// The Content-Type for a file name, by extension (lower case)
QByteArray contentTypeFor(const QString &fileName);

// The reply for request target `target` (path plus any ?query / #fragment)
// under `root`. `root` must be an existing directory.
Reply serve(const QString &root, const QByteArray &target);

// The path part of a request target (no ?query / #fragment)
QByteArray pathOf(const QByteArray &target);

// The image type of `bytes` by its signature (JPEG, PNG, GIF, WebP,
// BMP), or empty when it is none of them
QByteArray imageTypeOf(const QByteArray &bytes);

constexpr qint64 kMaxImage = 16 * 1024 * 1024;

// The value of `key` in a request target's query (percent-decoded), or
// a null string when absent
QString queryValue(const QByteArray &target, const QString &key);

// `localPath` if its canonical form lies inside `dir` (canonical too),
// else "": a file a manager names must still be in that manager's folder
QString insideDir(const QString &localPath, const QString &dir);

// GET /art: the image file at `localPath` (the app's own current cover,
// never a path a client chose), typed by its bytes. 404 for no path, a
// missing or oversize file, or one that is not an image.
Reply serveImage(const QString &localPath);

} // namespace OctaveService::StaticFiles

#endif // OCTAVE_SERVICE_STATICFILES_H
