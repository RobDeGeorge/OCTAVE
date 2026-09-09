#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QScreen>
#include <QObject>
#include <QDir>
#include <QWindow>
#include <QTimer>
#include <QtQml/qqml.h>

#ifdef Q_OS_ANDROID
#include <QtCore/private/qandroidextras_p.h>
#include <QFuture>
#include <QtConcurrent>
#include "platform/androidmediabridge.h"
#endif

// Phase 1 managers
#include "managers/settingsmanager.h"
#include "managers/clock.h"
#include "managers/networkmanager.h"
#include "managers/volumecontroller.h"

// Phase 2 managers
#include "managers/mediamanager.h"
#include "managers/audioanalyzer.h"

// Phase 3 managers
#include "managers/spotifymanager.h"   // desktop-only; empty on Q_OS_MOBILE
#include "managers/downloadmanager.h"

// Phase 4 managers
#include "managers/obdmanager.h"
#include "managers/esp32volumemanager.h"  // desktop-only; empty on Q_OS_MOBILE
#include "managers/berryimumanager.h"     // desktop-only; empty on Q_OS_MOBILE
#include "managers/gesturemanager.h"      // desktop-only; empty on Q_OS_MOBILE

// Phase 5 managers (desktop only — all guarded by Q_OS_MOBILE in their headers)
#include "managers/phonemirrormanager.h"
#if !defined(Q_OS_WIN) && !defined(Q_OS_MOBILE)
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "managers/androidautomanager.h"
#include "items/scrcpycapture.h"
#include "items/embeddedscrcpyitem.h"
#include "items/embeddeddhuitem.h"

// Phase 6 — dashboards feature (see TODO/dashboards-roadmap.md)
#include "managers/dashboardmanager.h"

// All managers are now real C++ implementations -- no stubs needed.

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("OCTAVE");
    app.setApplicationName("OCTAVE");

    // Force Qt Quick Controls to Basic style (matches Python version —
    // required for full Slider customization)
    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;

    // ---- Auto-detect screen scale (mirrors main.py logic) ----
    // Scale off the SHORTER axis so the factor is the same whether the
    // display reports landscape or portrait dimensions. On Android the
    // primary-screen availableSize() is the device's natural (portrait)
    // orientation even when the app is locked landscape — using height()
    // alone produced a ~3× autoScale there.
    qreal autoScale = 1.0;
    if (QScreen *screen = app.primaryScreen()) {
        QSize available = screen->availableSize();
        const int shortEdge = qMin(available.width(), available.height());
        autoScale = shortEdge / 720.0;
        autoScale = qBound(0.4, autoScale, 3.0);
    }

    QQmlContext *ctx = engine.rootContext();

    // ---- Scalar context properties ----
    ctx->setContextProperty("screenAutoScale", autoScale);
#ifdef Q_OS_ANDROID
    ctx->setContextProperty("isAndroid", true);
#else
    ctx->setContextProperty("isAndroid", false);
#endif

    // ==================================================================
    // Phase 1: Real manager objects
    // ==================================================================

    // Settings Manager — central settings store, everything depends on this
    SettingsManager settingsManager;
    ctx->setContextProperty("settingsManager", &settingsManager);

    // Clock — time display, depends on settings for format
    Clock clock(&settingsManager);
    ctx->setContextProperty("clock", &clock);

    // Network Manager — WiFi status, update checking
    NetworkManager networkManager;
    ctx->setContextProperty("networkManager", &networkManager);

    // Volume Controller — quadratic curve + dispatch to all audio outputs.
    // Other manager pointers (media, spotify, etc.) will be wired in when
    // those managers are ported in Phase 2/3.
    VolumeController volumeController(&settingsManager);
    ctx->setContextProperty("volumeController", &volumeController);

    // Apply saved startup volume (media wiring below, after MediaManager creation)

    // ==================================================================
    // Phase 2: Media Playback
    // ==================================================================

    // Media Manager — local music playback, library scanning, playlists
    MediaManager mediaManager;
    mediaManager.setSettingsManager(&settingsManager);
    ctx->setContextProperty("mediaManager", &mediaManager);

#ifdef Q_OS_ANDROID
    // Initialize the youtubedl-android bridge off the GUI thread — on first
    // launch it extracts Python 3.9 + yt-dlp + ffmpeg to the app's private data
    // dir (~10–20s). Subsequent launches are instant.
    QtConcurrent::run([] {
        if (OctaveAndroid::initMediaBridge()) {
            qInfo() << "[OctaveMediaBridge] yt-dlp + ffmpeg ready";
        } else {
            qWarning() << "[OctaveMediaBridge] init failed — downloads + waveform disabled";
        }
    });

    // Request runtime permissions needed for the media library and (eventual)
    // OBD Bluetooth transport. Declared in the manifest isn't enough on
    // Android 12+/13+; user must grant at runtime. Re-scan once granted.
    const QStringList androidPermissions = {
        QStringLiteral("android.permission.READ_MEDIA_AUDIO"),     // Android 13+
        QStringLiteral("android.permission.READ_EXTERNAL_STORAGE"),// pre-13 fallback
        QStringLiteral("android.permission.BLUETOOTH_CONNECT"),    // OBD Phase 3
        QStringLiteral("android.permission.BLUETOOTH_SCAN"),       // OBD Phase 3
    };
    for (const QString &perm : androidPermissions) {
        auto future = QtAndroidPrivate::requestPermission(perm);
        future.then([&mediaManager, perm](QtAndroidPrivate::PermissionResult result) {
            if (result == QtAndroidPrivate::Authorized
                && perm == QStringLiteral("android.permission.READ_MEDIA_AUDIO")) {
                // Got audio permission — kick off a library scan
                QMetaObject::invokeMethod(&mediaManager, "scan_library",
                                          Qt::QueuedConnection, Q_ARG(bool, true));
            }
        });
    }
#endif

    // Audio Analyzer — waveform visualization via FFT
    AudioAnalyzer audioAnalyzer;
    ctx->setContextProperty("audioAnalyzer", &audioAnalyzer);

    // Volume wiring moved below after all managers are created

    // Auto-rescan library when download completes (wired when downloadManager is ported)

    // ==================================================================
    // Phase 3: Online Features
    // ==================================================================

    // Spotify Manager — Spotify Web API integration.
    // On mobile: header swaps in a no-op stub so QML bindings stay valid.
    SpotifyManager spotifyManager;
    spotifyManager.setSettingsManager(&settingsManager);
    ctx->setContextProperty("spotifyManager", &spotifyManager);

    // ==================================================================
    // Phase 4: Vehicle & Hardware
    // ==================================================================

    // OBD Manager — vehicle diagnostics via ELM327
    OBDManager obdManager(&settingsManager);
    ctx->setContextProperty("obdManager", &obdManager);

    // Hardware managers (USB-serial / I2C on desktop, stubbed on mobile via header).
    ESP32VolumeManager esp32VolumeManager;
    esp32VolumeManager.connect_settings_manager(&settingsManager);
    ctx->setContextProperty("esp32VolumeManager", &esp32VolumeManager);

    BerryIMUManager berryIMU;
    berryIMU.connect_settings_manager(&settingsManager);
    ctx->setContextProperty("berryIMU", &berryIMU);

    GestureManager gestureSensor;
    gestureSensor.connect_settings_manager(&settingsManager);
    ctx->setContextProperty("gestureSensor", &gestureSensor);

    // ==================================================================
    // Phase 5: Desktop Integration (phone mirror, Android Auto)
    //          Stubbed out on mobile via Q_OS_MOBILE guards in the headers.
    // ==================================================================

    AndroidAutoManager androidAutoManager;
    ctx->setContextProperty("androidAutoManager", &androidAutoManager);
    engine.addImageProvider(QStringLiteral("dhuframe"),
                            androidAutoManager.frameProvider());

    PhoneMirrorManager phoneMirrorManager;
    ctx->setContextProperty("phoneMirrorManager", &phoneMirrorManager);

    ScrcpyCapture scrcpyCapture;
    scrcpyCapture.setPhoneMirrorManager(&phoneMirrorManager);
    ctx->setContextProperty("scrcpyCapture", &scrcpyCapture);
    engine.addImageProvider(QStringLiteral("scrcpyframe"),
                            scrcpyCapture.frameProvider());

#ifndef Q_OS_MOBILE
    // Phone mirror settings from saved config (desktop-only accessors)
    QString savedScrcpyPath = settingsManager.get_scrcpy_path();
    if (!savedScrcpyPath.isEmpty())
        phoneMirrorManager.setScrcpyPath(savedScrcpyPath);
    phoneMirrorManager.setAudioEnabled(settingsManager.get_scrcpy_audio_enabled());
    phoneMirrorManager.setVideoDevice(settingsManager.get_scrcpy_video_device());
    phoneMirrorManager.setDisplaySize(settingsManager.get_scrcpy_display_size());
    phoneMirrorManager.setNativeMode(settingsManager.get_phone_mirror_native());
#endif

    // Register custom QML types for video embedding (stub types on mobile)
    qmlRegisterType<EmbeddedDhuItem>("OCTAVE.AndroidAuto", 1, 0, "EmbeddedDhuItem");
    qmlRegisterType<EmbeddedScrcpyItem>("OCTAVE.PhoneMirror", 1, 0, "EmbeddedScrcpyItem");
    qmlRegisterType<ScrcpyCaptureItem>("OCTAVE.PhoneMirror", 1, 0, "ScrcpyCaptureItem");

    // Download Manager — music search & download via yt-dlp
    DownloadManager downloadManager;
    downloadManager.connect_settings_manager(&settingsManager);
    ctx->setContextProperty("downloadManager", &downloadManager);

    // Auto-rescan library when a download completes
    QObject::connect(&downloadManager, &DownloadManager::downloadComplete,
                     [&mediaManager](const QString &, const QString &) {
        mediaManager.scan_library(false);
    });

    // Wire volume controller -> all audio outputs.
    // Spotify volume is debounced (300ms) to avoid 429 rate limiting on desktop.
    // On mobile, SpotifyManager is a stub, so `is_connected()` always returns false
    // and the debounce harmlessly fires into the stub's no-op set_volume.
    static QTimer spotifyVolumeDebounce;
    spotifyVolumeDebounce.setSingleShot(true);
    spotifyVolumeDebounce.setInterval(300);
    static int pendingSpotifyVolume = 0;

    QObject::connect(&spotifyVolumeDebounce, &QTimer::timeout,
                     [&spotifyManager]() {
        if (spotifyManager.is_connected())
            spotifyManager.set_volume(pendingSpotifyVolume);
    });

    QObject::connect(&volumeController, &VolumeController::volumeApplied,
                     [&mediaManager](int percent, float linear) {
        mediaManager.setVolume(linear);
        pendingSpotifyVolume = percent;
        spotifyVolumeDebounce.start();
    });
    volumeController.applyVolume(settingsManager.currentVolume());

    // ---- QML import path: share frontend/ with Python version ----
#ifdef Q_OS_ANDROID
    // On Android, frontend/ is bundled as assets (see android/assets/frontend
    // symlink). Qt supports the "assets:" URL scheme for anything under assets/.
    QDir frontendDir(QStringLiteral("assets:/frontend"));
    engine.addImportPath(QStringLiteral("assets:/frontend"));
#else
    QString appDir = QGuiApplication::applicationDirPath();
    // In development, the binary is in build/ — frontend/ is one level up.
    QDir frontendDir(appDir);
    if (!frontendDir.cd("frontend")) {
        frontendDir = QDir(appDir);
        frontendDir.cdUp();  // build/ -> project root
        if (!frontendDir.cd("frontend")) {
            qWarning("Could not locate frontend/ directory");
        }
    }

    engine.addImportPath(frontendDir.absolutePath());
#endif

    // ==================================================================
    // Phase 6: Dashboards (data-driven dashboards + user-authored presets)
    // See TODO/dashboards-roadmap.md. Must be registered before engine.load
    // so QML sees the context property at first binding evaluation.
    // ==================================================================
    DashboardManager dashboardManager;
    dashboardManager.setPresetsDir(frontendDir.absoluteFilePath("dashboards/presets"));
    dashboardManager.setUserDir(SettingsManager::getAppDataDir()
                                + QStringLiteral("/dashboards"));
    ctx->setContextProperty("dashboardManager", &dashboardManager);

    // ---- Load Main.qml ----
#ifdef Q_OS_ANDROID
    // On Android, use the "assets:" URL scheme directly — QUrl::fromLocalFile
    // would resolve relative to /, which is wrong for asset-bundled files.
    QUrl qmlUrl(QStringLiteral("assets:/frontend/Main.qml"));
    engine.load(qmlUrl);
#else
    QString qmlFile = frontendDir.absoluteFilePath("Main.qml");
    QUrl qmlUrl = QUrl::fromLocalFile(qmlFile);
    engine.load(qmlUrl);
#endif

    if (engine.rootObjects().isEmpty()) {
        qCritical("Failed to load Main.qml from %s", qPrintable(qmlUrl.toString()));
        return -1;
    }

    // Quit the app when the last window is closed (Mod+Q, X button, etc.)
    app.setQuitOnLastWindowClosed(true);
    QObject::connect(&app, &QGuiApplication::lastWindowClosed,
                     &app, &QGuiApplication::quit);

    // Wire gesture sensor actions -> media control (stub never emits on mobile).
    QObject::connect(&gestureSensor, &GestureManager::actionTriggered,
                     [&mediaManager, &volumeController, &settingsManager](const QString &action) {
        if (action == "next_track") mediaManager.next_track();
        else if (action == "previous_track") mediaManager.previous_track();
        else if (action == "play_pause") mediaManager.toggle_play();
        else if (action == "mute_toggle") mediaManager.toggle_mute();
        else if (action == "volume_up") {
            int step = settingsManager.gestureVolumeStep();
            volumeController.applyVolume(settingsManager.currentVolume() + step);
        } else if (action == "volume_down") {
            int step = settingsManager.gestureVolumeStep();
            volumeController.applyVolume(settingsManager.currentVolume() - step);
        }
    });

    // Wire ESP32 volume knob -> volume controller (stub never emits on mobile).
    QObject::connect(&esp32VolumeManager, &ESP32VolumeManager::volumeChangeRequested,
                     [&volumeController, &settingsManager](float delta) {
        static float accumulator = 0.0f;
        accumulator += delta;
        if (std::abs(accumulator) >= 1.0f) {
            int change = static_cast<int>(accumulator);
            accumulator -= change;
            volumeController.applyVolume(settingsManager.currentVolume() + change);
        }
    });
    QObject::connect(&esp32VolumeManager, &ESP32VolumeManager::muteToggleRequested,
                     [&mediaManager, &esp32VolumeManager]() {
        mediaManager.toggle_mute();
        esp32VolumeManager.send_mute_state(mediaManager.is_muted());
    });

#if !defined(Q_OS_WIN) && !defined(Q_OS_MOBILE)
    // SIGTERM/SIGINT (launcher scripts, systemd, Ctrl-C) must run the
    // aboutToQuit cleanup below, otherwise child processes and adb forwards
    // are orphaned. Self-pipe trick: the handler only writes a byte; the
    // notifier turns it into app.quit() on the event loop.
    static int quitPipe[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, quitPipe) == 0) {
        auto onSignal = [](int) { const char b = 1; (void)::write(quitPipe[0], &b, 1); };
        std::signal(SIGTERM, onSignal);
        std::signal(SIGINT, onSignal);
        auto *quitNotifier = new QSocketNotifier(quitPipe[1], QSocketNotifier::Read, &app);
        QObject::connect(quitNotifier, &QSocketNotifier::activated, &app, [] {
            char b; (void)::read(quitPipe[1], &b, 1);
            qInfo() << "Signal received, shutting down";
            QCoreApplication::quit();
        });
    }
#endif

    // Cleanup on quit (stubs make all calls no-op on mobile)
    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&]() {
        androidAutoManager.cleanup();
        scrcpyCapture.stopCapture();   // kill the ffmpeg reader before scrcpy
        phoneMirrorManager.cleanup();
        esp32VolumeManager.cleanup();
        berryIMU.cleanup();
        gestureSensor.cleanup();
        spotifyManager.cleanup();
        obdManager.close();
        downloadManager.cleanup();
        networkManager.cleanup();
    });

    return app.exec();
}

#include "main.moc"
