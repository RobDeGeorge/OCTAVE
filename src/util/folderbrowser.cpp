#include "folderbrowser.h"

#include <QDir>
#include <QFileInfo>

namespace FolderBrowser {

namespace {
QStringList canonicalRoots(const QStringList &roots)
{
    QStringList out;
    for (const QString &r : roots) {
        const QString c = QFileInfo(r).canonicalFilePath();
        if (!c.isEmpty() && QFileInfo(c).isDir() && !out.contains(c))
            out.append(c);
    }
    return out;
}

bool within(const QString &root, const QString &path)
{
    return path == root || path.startsWith(root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/'));
}
} // namespace

QString confine(const QString &path, const QStringList &roots)
{
    if (path.isEmpty())
        return {};
    const QString c = QFileInfo(path).canonicalFilePath();
    if (c.isEmpty() || !QFileInfo(c).isDir())
        return {};
    for (const QString &root : canonicalRoots(roots))
        if (within(root, c))
            return c;
    return {};
}

QVariantMap list(const QString &path, const QStringList &roots, const QStringList &audioExtensions)
{
    const QStringList rootList = canonicalRoots(roots);
    if (path.isEmpty()) {
        QVariantList out;
        for (const QString &r : rootList) {
            const QString name = QFileInfo(r).fileName();
            out.append(QVariantMap{{QStringLiteral("name"), name.isEmpty() ? r : name}, {QStringLiteral("path"), r}});
        }
        return {{QStringLiteral("roots"), out}};
    }

    const QString dir = confine(path, rootList);
    if (dir.isEmpty())
        return {{QStringLiteral("error"), QStringLiteral("not a folder this app may browse")}};
    QDir d(dir);
    if (!d.isReadable())
        return {{QStringLiteral("error"), QStringLiteral("folder not readable")}};

    QStringList dirs, audio;
    bool truncated = false;
    // NoDotAndDotDot without Hidden: no ".", "..", or dotfiles
    const QFileInfoList entries = d.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
                                                  QDir::Name | QDir::IgnoreCase | QDir::DirsFirst);
    for (const QFileInfo &e : entries) {
        if (e.fileName().startsWith(QLatin1Char('.')))
            continue;
        if (dirs.size() + audio.size() >= kMaxEntries) {
            truncated = true;
            break;
        }
        if (e.isDir()) {
            // A link to a folder is listed only if it stays inside a root
            if (e.isSymLink() && confine(e.absoluteFilePath(), rootList).isEmpty())
                continue;
            dirs.append(e.fileName());
        } else {
            const QString name = e.fileName();
            for (const QString &ext : audioExtensions) {
                if (name.endsWith(ext, Qt::CaseInsensitive)) {
                    audio.append(name);
                    break;
                }
            }
        }
    }

    // Up while that is still inside a root (a root inside another, such
    // as Music in home, has one)
    QString parent;
    const QString up = QFileInfo(dir).absolutePath();
    if (up != dir && !confine(up, rootList).isEmpty())
        parent = up;
    return {{QStringLiteral("path"), dir},
            {QStringLiteral("parent"), parent},
            {QStringLiteral("dirs"), dirs},
            {QStringLiteral("audioFiles"), audio},
            {QStringLiteral("truncated"), truncated}};
}

} // namespace FolderBrowser
