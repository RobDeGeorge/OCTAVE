#include "assetstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcAssetStore, "octave.assetstore")

AssetStore::AssetStore(const QString &sourceDir, QObject *parent)
    : QObject(parent)
    , m_sourceDir(sourceDir)
{
#ifdef Q_OS_ANDROID
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QStringLiteral("/asset-cache");
#endif
}

QUrl AssetStore::localUrl(const QString &relativePath)
{
    if (relativePath.isEmpty() || relativePath.contains(QStringLiteral("..")))
        return QUrl();

    const QString source = m_sourceDir + QLatin1Char('/') + relativePath;
#ifndef Q_OS_ANDROID
    return QUrl::fromLocalFile(QDir::cleanPath(source));
#else
    const QString target = m_cacheDir + QLatin1Char('/') + relativePath;
    const QFileInfo src(source);
    if (!src.exists()) {
        qCWarning(lcAssetStore) << "No such asset in the APK:" << relativePath;
        return QUrl();
    }
    const QFileInfo dst(target);
    if (!dst.exists() || dst.size() != src.size()) {
        QDir().mkpath(dst.absolutePath());
        if (dst.exists())
            QFile::remove(target);
        if (!QFile::copy(source, target)) {
            qCWarning(lcAssetStore) << "Failed to extract" << relativePath << "to" << target;
            return QUrl();
        }
        // QFile::copy keeps the asset's read-only permission bits; make the
        // copy replaceable by the next update.
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        qCInfo(lcAssetStore) << "Extracted" << relativePath << "(" << src.size() << "bytes )";
    }
    return QUrl::fromLocalFile(target);
#endif
}
