#ifndef ANDROIDMEDIABRIDGE_H
#define ANDROIDMEDIABRIDGE_H

// Thin C++ wrappers around org.octave.app.OctaveMediaBridge (Java side,
// android/src/org/octave/app/OctaveMediaBridge.java: metadata, album art,
// storage helpers) and, when OCTAVE_ENABLE_DOWNLOADS is on, around
// org.octave.app.OctaveDownloadBridge (android/src-downloads/: yt-dlp, ffmpeg,
// ytmusicapi). The Java download class only exists in APKs built with
// downloads on, so its wrappers must stay behind the same flag.
//
// Everything here is header-only and inlined for simplicity — the JNI calls
// use QJniObject and don't need a separate compilation unit. Only compiled
// on Android (guarded by caller).

#ifdef Q_OS_ANDROID

#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
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

// Returns absolute path to a writable downloads dir under the app's external
// files area (no permissions required, scoped-storage-safe).
inline QString getDownloadsDir()
{
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");
    if (!activity.isValid()) return {};
    QJniObject path = QJniObject::callStaticObjectMethod(
        "org/octave/app/OctaveMediaBridge",
        "getDownloadsDir",
        "(Landroid/content/Context;)Ljava/lang/String;",
        activity.object<jobject>());
    return path.toString();
}

// True when the app holds Android 11+ All-Files-Access (MANAGE_EXTERNAL_STORAGE).
// Gate for writing into user-picked folders like /storage/emulated/0/Music.
inline bool hasAllFilesAccess()
{
    return QJniObject::callStaticMethod<jboolean>(
        "org/octave/app/OctaveMediaBridge",
        "hasAllFilesAccess",
        "()Z");
}

// Opens the system Settings page for toggling All-Files-Access on this app.
// Returns true if the Intent was dispatched. User grants the permission by
// flipping the toggle in the opened screen — the app must re-check
// hasAllFilesAccess() on resume.
inline bool requestAllFilesAccess()
{
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");
    if (!activity.isValid()) return false;
    return QJniObject::callStaticMethod<jboolean>(
        "org/octave/app/OctaveMediaBridge",
        "requestAllFilesAccess",
        "(Landroid/app/Activity;)Z",
        activity.object<jobject>());
}

// Download a URL (HTTPS OK) to a local cache file via Android's SSL stack.
// Bypasses Qt's broken OpenSSL on Android. Returns local path on success, ""
// on failure. Result is a plain filesystem path — wrap with QUrl::fromLocalFile
// before handing to QML Image.source.
inline QString cacheHttpImage(const QString &url)
{
    if (url.isEmpty()) return {};
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");
    if (!activity.isValid()) return {};
    QJniObject jurl = QJniObject::fromString(url);
    QJniObject path = QJniObject::callStaticObjectMethod(
        "org/octave/app/OctaveMediaBridge",
        "cacheHttpImage",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
        activity.object<jobject>(),
        jurl.object<jstring>());
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

#ifdef OCTAVE_ENABLE_DOWNLOADS

// Lazy one-time init of the youtubedl-android runtime (extracts Python + yt-dlp
// + ffmpeg to the app's private data dir). Blocking on first call — caller
// should run off the GUI thread. Safe to call repeatedly.
inline bool initDownloadBridge()
{
    // Grab the Android Activity as our Context
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");

    if (!activity.isValid()) {
        qWarning() << "[OctaveDownloadBridge] QtNative.activity() returned null";
        return false;
    }

    jboolean ok = QJniObject::callStaticMethod<jboolean>(
        "org/octave/app/OctaveDownloadBridge",
        "init",
        "(Landroid/content/Context;)Z",
        activity.object<jobject>());

    if (!ok) {
        QJniObject err = QJniObject::callStaticObjectMethod(
            "org/octave/app/OctaveDownloadBridge",
            "lastInitError",
            "()Ljava/lang/String;");
        qWarning() << "[OctaveDownloadBridge] init failed:" << err.toString();
    }
    return ok;
}

// Build a jobjectArray of Java strings from a QStringList. Caller owns ref
// and must DeleteLocalRef (we rely on the scope exit to do it).
inline QJniObject stringListToJavaArray(const QStringList &args)
{
    QJniEnvironment env;
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray jarr = env->NewObjectArray(args.size(), stringClass, nullptr);
    for (int i = 0; i < args.size(); ++i) {
        QJniObject s = QJniObject::fromString(args.at(i));
        env->SetObjectArrayElement(jarr, i, s.object<jstring>());
    }
    return QJniObject::fromLocalRef(jarr);
}

// Run yt-dlp with the given args. Blocks until done. Returns combined stdout+
// stderr as QString. Check lastExitCode() for success/failure.
inline QString runYtDlp(const QStringList &args)
{
    QJniObject jargs = stringListToJavaArray(args);
    QJniObject result = QJniObject::callStaticObjectMethod(
        "org/octave/app/OctaveDownloadBridge",
        "runYtDlp",
        "([Ljava/lang/String;)Ljava/lang/String;",
        jargs.object<jobjectArray>());
    return result.toString();
}

// Returns the absolute path to the ffmpeg binary installed by youtubedl-android,
// or empty string if unavailable. C++ can run this binary directly via QProcess
// since it lives in the app's private noBackupFilesDir (launchable on Android).
inline QString getFfmpegBinaryPath()
{
    QJniObject path = QJniObject::callStaticObjectMethod(
        "org/octave/app/OctaveDownloadBridge",
        "getFfmpegBinaryPath",
        "()Ljava/lang/String;");
    return path.toString();
}

inline int lastExitCode()
{
    return QJniObject::callStaticMethod<jint>(
        "org/octave/app/OctaveDownloadBridge",
        "lastExitCode",
        "()I");
}

inline float pollProgress()
{
    return QJniObject::callStaticMethod<jfloat>(
        "org/octave/app/OctaveDownloadBridge",
        "pollProgress",
        "()F");
}

inline qint64 pollEta()
{
    return QJniObject::callStaticMethod<jlong>(
        "org/octave/app/OctaveDownloadBridge",
        "pollEta",
        "()J");
}

inline QString pollLine()
{
    QJniObject line = QJniObject::callStaticObjectMethod(
        "org/octave/app/OctaveDownloadBridge",
        "pollLine",
        "()Ljava/lang/String;");
    return line.toString();
}

// Runs the bundled ytmusic_search.py against the youtubedl-android Python
// runtime. Returns stdout (JSON-per-line) on success; empty string if
// ytmusicapi isn't installed yet or the script failed. Caller falls back to
// yt-dlp search when empty. Blocking — run off the GUI thread.
inline QString runYtMusicSearch(const QString &query, int limit)
{
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");
    if (!activity.isValid()) return {};
    QJniObject jq = QJniObject::fromString(query);
    QJniObject result = QJniObject::callStaticObjectMethod(
        "org/octave/app/OctaveDownloadBridge",
        "runYtMusicSearch",
        "(Landroid/content/Context;Ljava/lang/String;I)Ljava/lang/String;",
        activity.object<jobject>(),
        jq.object<jstring>(),
        static_cast<jint>(limit));
    return result.toString();
}

// Replaces an m4a/mp4 file's cover art atom with the JPEG/PNG at coverPath.
// Uses mutagen via the AAR's bundled Python — no re-encode, in-place patch.
// Blocking; call off the GUI thread.
inline bool runEmbedCover(const QString &audioPath, const QString &coverPath)
{
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");
    if (!activity.isValid()) return false;
    QJniObject jaudio = QJniObject::fromString(audioPath);
    QJniObject jcover = QJniObject::fromString(coverPath);
    return QJniObject::callStaticMethod<jboolean>(
        "org/octave/app/OctaveDownloadBridge",
        "runEmbedCover",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
        activity.object<jobject>(),
        jaudio.object<jstring>(),
        jcover.object<jstring>());
}

#endif // OCTAVE_ENABLE_DOWNLOADS

} // namespace OctaveAndroid

#endif // Q_OS_ANDROID
#endif // ANDROIDMEDIABRIDGE_H
