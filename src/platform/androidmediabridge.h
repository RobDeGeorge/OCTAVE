#ifndef ANDROIDMEDIABRIDGE_H
#define ANDROIDMEDIABRIDGE_H

// Thin C++ wrappers around org.octave.app.OctaveMediaBridge (Java side,
// android/src/org/octave/app/OctaveMediaBridge.java: metadata, album art,
// the app's Music dir).
//
// Everything here is header-only and inlined for simplicity — the JNI calls
// use QJniObject and don't need a separate compilation unit. Only compiled
// on Android (guarded by caller).

#ifdef Q_OS_ANDROID

#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#include <QString>
#include <QDebug>

namespace OctaveAndroid {

// Returns newline-delimited "title\n<t>\nartist\n<a>\nalbum\n<al>\nduration\n<secs>".
// Empty string if MediaMetadataRetriever failed.
inline QString readMetadata(const QString &filePath)
{
    QJniObject jpath = QJniObject::fromString(filePath);
    QJniObject result = QJniObject::callStaticObjectMethod(
        "org/octave/app/OctaveMediaBridge",
        "readMetadata",
        "(Ljava/lang/String;)Ljava/lang/String;",
        jpath.object<jstring>());
    return result.toString();
}

// Returns absolute path to the app's writable Music dir under its external
// files area (no permissions required, scoped-storage-safe).
inline QString getAppMusicDir()
{
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");
    if (!activity.isValid()) return {};
    QJniObject path = QJniObject::callStaticObjectMethod(
        "org/octave/app/OctaveMediaBridge",
        "getAppMusicDir",
        "(Landroid/content/Context;)Ljava/lang/String;",
        activity.object<jobject>());
    return path.toString();
}

// Writes embedded album art bytes to outPath. Returns true on success.
inline bool extractAlbumArt(const QString &filePath, const QString &outPath)
{
    QJniObject jpath = QJniObject::fromString(filePath);
    QJniObject jout  = QJniObject::fromString(outPath);
    return QJniObject::callStaticMethod<jboolean>(
        "org/octave/app/OctaveMediaBridge",
        "extractAlbumArt",
        "(Ljava/lang/String;Ljava/lang/String;)Z",
        jpath.object<jstring>(),
        jout.object<jstring>());
}

} // namespace OctaveAndroid

#endif // Q_OS_ANDROID
#endif // ANDROIDMEDIABRIDGE_H
