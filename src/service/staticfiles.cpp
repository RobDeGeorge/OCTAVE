#include "staticfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <QUrl>

namespace OctaveService::StaticFiles {

namespace {

Reply reply(int status, const QByteArray &reason, const QByteArray &body)
{
    return Reply{status, reason, "text/plain", body};
}

// `path` (canonical) is `root` (canonical) or inside it
bool inside(const QString &root, const QString &path)
{
    return path == root || path.startsWith(root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/'));
}

Reply file(const QString &root, const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return reply(404, "Not Found", "not found\n");
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || !inside(root, canonical))
        return reply(403, "Forbidden", "outside the web root\n");
    if (!info.isFile())
        return reply(404, "Not Found", "not found\n");
    if (info.size() > kMaxFile)
        return reply(413, "Content Too Large", "file too large\n");
    QFile f(canonical);
    if (!f.open(QIODevice::ReadOnly))
        return reply(403, "Forbidden", "unreadable\n");
    return Reply{200, "OK", contentTypeFor(canonical), f.readAll()};
}

} // namespace

QByteArray contentTypeFor(const QString &fileName)
{
    static const QHash<QString, QByteArray> types = {
        {QStringLiteral("html"), "text/html; charset=utf-8"},
        {QStringLiteral("htm"), "text/html; charset=utf-8"},
        {QStringLiteral("js"), "text/javascript; charset=utf-8"},
        {QStringLiteral("mjs"), "text/javascript; charset=utf-8"},
        {QStringLiteral("css"), "text/css; charset=utf-8"},
        {QStringLiteral("json"), "application/json"},
        {QStringLiteral("map"), "application/json"},
        {QStringLiteral("webmanifest"), "application/manifest+json"},
        {QStringLiteral("txt"), "text/plain; charset=utf-8"},
        {QStringLiteral("svg"), "image/svg+xml"},
        {QStringLiteral("png"), "image/png"},
        {QStringLiteral("jpg"), "image/jpeg"},
        {QStringLiteral("jpeg"), "image/jpeg"},
        {QStringLiteral("gif"), "image/gif"},
        {QStringLiteral("webp"), "image/webp"},
        {QStringLiteral("avif"), "image/avif"},
        {QStringLiteral("ico"), "image/x-icon"},
        {QStringLiteral("woff"), "font/woff"},
        {QStringLiteral("woff2"), "font/woff2"},
        {QStringLiteral("ttf"), "font/ttf"},
        {QStringLiteral("otf"), "font/otf"},
        {QStringLiteral("wasm"), "application/wasm"},
    };
    return types.value(QFileInfo(fileName).suffix().toLower(), "application/octet-stream");
}

QByteArray pathOf(const QByteArray &target)
{
    QByteArray path = target;
    const int q = path.indexOf('?');
    if (q >= 0)
        path.truncate(q);
    const int h = path.indexOf('#');
    if (h >= 0)
        path.truncate(h);
    return path;
}

QByteArray imageTypeOf(const QByteArray &bytes)
{
    if (bytes.startsWith("\xFF\xD8\xFF"))
        return "image/jpeg";
    if (bytes.startsWith("\x89PNG\r\n\x1A\n"))
        return "image/png";
    if (bytes.startsWith("GIF87a") || bytes.startsWith("GIF89a"))
        return "image/gif";
    if (bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP")
        return "image/webp";
    if (bytes.startsWith("BM") && bytes.size() > 14)
        return "image/bmp";
    return {};
}

QString queryValue(const QByteArray &target, const QString &key)
{
    const int q = target.indexOf('?');
    if (q < 0)
        return {};
    QByteArray query = target.mid(q + 1);
    const int h = query.indexOf('#');
    if (h >= 0)
        query.truncate(h);
    for (const QByteArray &pair : query.split('&')) {
        const int eq = pair.indexOf('=');
        const QByteArray k = eq < 0 ? pair : pair.left(eq);
        if (QUrl::fromPercentEncoding(k) != key)
            continue;
        QByteArray v = eq < 0 ? QByteArray() : pair.mid(eq + 1);
        v.replace('+', ' ');
        return QUrl::fromPercentEncoding(v);
    }
    return {};
}

QString insideDir(const QString &localPath, const QString &dir)
{
    const QString file = QFileInfo(localPath).canonicalFilePath();
    const QString root = QFileInfo(dir).canonicalFilePath();
    if (file.isEmpty() || root.isEmpty())
        return {};
    return file.startsWith(root + QLatin1Char('/')) ? file : QString();
}

Reply serveImage(const QString &localPath)
{
    const QFileInfo info(localPath);
    if (localPath.isEmpty() || !info.isFile() || info.size() > kMaxImage)
        return reply(404, "Not Found", "no art\n");
    QFile f(localPath);
    if (!f.open(QIODevice::ReadOnly))
        return reply(404, "Not Found", "no art\n");
    const QByteArray bytes = f.readAll();
    const QByteArray type = imageTypeOf(bytes);
    if (type.isEmpty())
        return reply(404, "Not Found", "no art\n");
    return Reply{200, "OK", type, bytes};
}

Reply serve(const QString &root, const QByteArray &target)
{
    const QString rootCanonical = QFileInfo(root).canonicalFilePath();
    if (rootCanonical.isEmpty() || !QFileInfo(rootCanonical).isDir())
        return reply(404, "Not Found", "not found\n");

    const QByteArray rawPath = pathOf(target);
    if (!rawPath.startsWith('/'))
        return reply(400, "Bad Request", "bad path\n");
    const QString path = QUrl::fromPercentEncoding(rawPath);
    if (path.contains(QLatin1Char('\\')) || path.contains(QChar(0)))
        return reply(400, "Bad Request", "bad path\n");

    QStringList segments;
    for (const QString &s : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (s == QLatin1String("."))
            continue;
        if (s == QLatin1String(".."))
            return reply(403, "Forbidden", "no parent paths\n");
        if (s.startsWith(QLatin1Char('.')))
            return reply(404, "Not Found", "not found\n");   // .git, .env: not served
        segments.append(s);
    }

    const QString joined = rootCanonical + QLatin1Char('/') + segments.join(QLatin1Char('/'));
    const QFileInfo info(joined);
    if (info.isDir())
        return file(rootCanonical, QDir(joined).filePath(QStringLiteral("index.html")));
    if (info.exists() || info.isSymLink())
        return file(rootCanonical, joined);
    // A client route (/settings, /media/queue): the app's page, which routes it
    if (segments.isEmpty() || !segments.last().contains(QLatin1Char('.')))
        return file(rootCanonical, rootCanonical + QStringLiteral("/index.html"));
    return reply(404, "Not Found", "not found\n");
}

} // namespace OctaveService::StaticFiles
