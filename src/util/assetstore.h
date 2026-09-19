#ifndef ASSETSTORE_H
#define ASSETSTORE_H

#include <QObject>
#include <QString>
#include <QUrl>

// Hands QML a plain file:// URL for a file under frontend/assets/.
//
// Qt's own loaders (Image, Texture, FontLoader) read "assets:/" URLs on
// Android through Qt's asset file engine, but third-party loaders do not:
// Quick3D's RuntimeLoader passes the path to Assimp, which fopen()s it and
// fails with "IO Error: File not found" for anything inside the APK. So on
// Android, localUrl() copies the file out of the APK into the app's data
// directory once (re-copied when its size changes, i.e. after an update) and
// returns that path. On desktop it just returns the file inside frontend/.
// The Python backend mirrors this in backend/asset_store.py (desktop only,
// so it never has to copy).
class AssetStore : public QObject
{
    Q_OBJECT
public:
    // sourceDir: the frontend/assets directory — a filesystem path on desktop,
    // "assets:/frontend/assets" on Android.
    explicit AssetStore(const QString &sourceDir, QObject *parent = nullptr);

    Q_INVOKABLE QUrl localUrl(const QString &relativePath);

private:
    QString m_sourceDir;
    QString m_cacheDir;   // Android only
};

#endif // ASSETSTORE_H
