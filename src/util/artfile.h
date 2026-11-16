// artfile.h
//
// The album art a remote source sends (the mirrored phone, Android Auto) as
// a file QML can show. Every cover gets a file name no earlier cover of this
// run had: QML's Image caches decoded pictures by URL and never looks at the
// file again, so a name that comes back (the two alternating files this
// replaced) showed the cover it held two tracks ago.
// Header-only: the managers that use it are linked into several test targets.

#ifndef ARTFILE_H
#define ARTFILE_H

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

namespace ArtFile {

// Writes data to <dir>/<stem>_<++seq><suffix> and returns the path ("" if it
// could not be written). Keeps the cover before it, which a cross-fade may
// still be loading, and removes every older <stem>_* file, leftovers of an
// earlier run included.
inline QString write(const QString &dir, const QString &stem, const QString &suffix, const QByteArray &data, quint32 &seq)
{
    QDir().mkpath(dir);
    const QString previous = QStringLiteral("%1_%2%3").arg(stem).arg(seq).arg(suffix);
    ++seq;
    const QString name = QStringLiteral("%1_%2%3").arg(stem).arg(seq).arg(suffix);
    const QString path = dir + QLatin1Char('/') + name;
    QFile f(path);
    const bool ok = f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    f.close();
    const QStringList old = QDir(dir).entryList({stem + QStringLiteral("_*")}, QDir::Files);
    for (const QString &entry : old) {
        if (entry != name && entry != previous)
            QFile::remove(dir + QLatin1Char('/') + entry);
    }
    return ok ? path : QString();
}

} // namespace ArtFile

#endif // ARTFILE_H
