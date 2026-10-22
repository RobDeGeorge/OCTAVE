#include <QGuiApplication>
#include <cstring>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSslSocket>
#include "util/assetstore.h"
#include <QQuickStyle>
#include <QScreen>
#include <QObject>
#include <QDir>
#include <QFile>
#include <QWindow>
#include <QTimer>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtQml/qqml.h>
#ifdef OCTAVE_HAVE_WEBENGINE
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#endif
#ifdef OCTAVE_HAVE_WEBVIEW
#include <QtWebView/QtWebView>
#endif
#ifdef OCTAVE_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif

#ifdef Q_OS_ANDROID
#include <QtCore/private/qandroidextras_p.h>
#include <QFuture>
#include <QtConcurrent>
#include "platform/androidmediabridge.h"
#endif

// Phase 1 managers
#include "managers/settingsmanager.h"
#include "managers/diagnosticsmanager.h"
#include "util/logger.h"
#include "util/uiwatchdog.h"
#include "managers/clock.h"
#include "managers/networkmanager.h"
#include "managers/volumecontroller.h"
#include "managers/shiftlightmanager.h"

// Phase 2 managers
#include "managers/mediamanager.h"
#include "managers/audioanalyzer.h"

// Phase 3 managers
#include "managers/downloadmanager.h"

// Phase 4 managers
#include "managers/obdmanager.h"
#include "managers/obdadapterfinder.h"
#include "managers/triprecorder.h"
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
#include "items/embeddeddhuitem.h"

// Phase 6 — dashboards feature (see TODO/dashboards-roadmap.md)
#include "managers/dashboardmanager.h"

#ifdef OCTAVE_DEV_TOOLS
// --dev / --profile: simulator, command server, perf monitor (src/dev/)
#include "dev/devruntime.h"
#endif

// All managers are now real C++ implementations -- no stubs needed.

int main(int argc, char *argv[])
{
    // Persistent logging + crash handlers first, so anything the managers
    // say during construction (and any crash) reaches the log files.
    bool debugLogging = false;
    // --smoke: load Main.qml, run 5 s (startup work, page pre-building), then
    // exit 0. A packaged build that cannot start (missing DLL, no frontend/)
    // exits non-zero or never starts, so CI can launch the real artifact.
    bool smokeTest = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0)
            debugLogging = true;
        else if (std::strcmp(argv[i], "--smoke") == 0)
            smokeTest = true;
    }
    OctaveLog::install(debugLogging);

#ifdef OCTAVE_HAVE_WEBENGINE
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Chromium's GPU initialization can crash in Mesa on Wayland when GBM
    // falls back to Vulkan. The offline wiki needs no GPU acceleration;
    // disable it only for WebEngine, leaving Qt Quick's renderer unchanged.
    QByteArray chromiumFlags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (!chromiumFlags.split(' ').contains("--disable-gpu")) {
        chromiumFlags.append(" --disable-gpu");
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags.trimmed());
    }
#endif
    QtWebEngineQuick::initialize();
#endif
#ifdef OCTAVE_HAVE_WEBVIEW
    QtWebView::initialize();   // Android: must precede QGuiApplication
#endif
    QGuiApplication app(argc, argv);
    app.setOrganizationName("OCTAVE");
    app.setApplicationName("OCTAVE");

    // Force Qt Quick Controls to Basic style (required for full Slider
    // customization)
    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;

    // ---- Auto-detect screen scale ----
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
    // OpenSSL is bundled from android/openssl (QT_ANDROID_EXTRA_LIBS). If this
    // says no TLS backend, every Qt-side HTTPS request (self-update, album search)
    // fails on the device — v0.9.2 shipped that way without noticing.
    qInfo() << "Android TLS:" << (QSslSocket::supportsSsl() ? "OpenSSL available," : "NO TLS BACKEND,")
            << QSslSocket::sslLibraryVersionString();
#else
    ctx->setContextProperty("isAndroid", false);
#endif

    // ==================================================================
    // Phase 1: Real manager objects
    // ==================================================================

    // Settings Manager — central settings store, everything depends on this
    SettingsManager settingsManager;
    ctx->setContextProperty("settingsManager", &settingsManager);

    // A hung event loop gets logged (octave.ui_watchdog) instead of vanishing
    UiWatchdog uiWatchdog;
    uiWatchdog.start();

    // Diagnostics — read / export the logs from the head unit (Settings > About)
    DiagnosticsManager diagnosticsManager;
    ctx->setContextProperty("diagnosticsManager", &diagnosticsManager);

    // Clock — time display, depends on settings for format
    Clock clock(&settingsManager);
    ctx->setContextProperty("clock", &clock);

    // Network Manager — WiFi status, update checking
    NetworkManager networkManager;
    ctx->setContextProperty("networkManager", &networkManager);

    // Volume Controller — quadratic curve + dispatch to all audio outputs
    // (wired to the managers further down, once they exist).
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
#ifdef OCTAVE_ENABLE_DOWNLOADS
    // Initialize the youtubedl-android bridge off the GUI thread — on first
    // launch it extracts Python 3.9 + yt-dlp + ffmpeg to the app's private data
    // dir (~10–20s). Subsequent launches are instant.
    QtConcurrent::run([] {
        if (OctaveAndroid::initDownloadBridge()) {
            qInfo() << "[OctaveDownloadBridge] yt-dlp + ffmpeg ready";
        } else {
            qWarning() << "[OctaveDownloadBridge] init failed — downloads disabled";
        }
    });
#endif

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

    // ==================================================================
    // Phase 3: Online Features
    // ==================================================================

    // ==================================================================
    // Phase 4: Vehicle & Hardware
    // ==================================================================

    // OBD Manager — vehicle diagnostics via ELM327
    OBDManager obdManager(&settingsManager);
    ctx->setContextProperty("obdManager", &obdManager);

    // Shift light — which RPM flag is active and its flash phase, fed by OBD
    ShiftLightManager shiftLightManager(&settingsManager);
    QObject::connect(&obdManager, &OBDManager::rpmChanged,
                     &shiftLightManager, &ShiftLightManager::setRpm);
    ctx->setContextProperty("shiftLightManager", &shiftLightManager);

    // Bluetooth OBD adapter discovery + pairing (the "Connect" sheet)
    OBDAdapterFinder obdAdapterFinder(&obdManager, &settingsManager);
    ctx->setContextProperty("obdAdapterFinder", &obdAdapterFinder);

    // Trip Recorder — the black box: every OBD reading of every drive into
    // <config>/history/blackbox.sqlite, one CSV + JSON per trip
    TripRecorder tripRecorder(&settingsManager);
    tripRecorder.attach(&obdManager);
    tripRecorder.start();
    ctx->setContextProperty("tripRecorder", &tripRecorder);

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

#ifndef Q_OS_MOBILE
    // Phone audio drives the visualizer's live mode
    QObject::connect(&phoneMirrorManager, &PhoneMirrorManager::audioPlayed,
                     &audioAnalyzer, &AudioAnalyzer::feed_live_pcm);
    // Phone mirror settings from saved config (desktop-only accessors)
    phoneMirrorManager.setDisplaySize(settingsManager.get_scrcpy_display_size());
    // Picture quality: virtual display density and per-link video bit rate
    phoneMirrorManager.setDisplayDpi(settingsManager.get_scrcpy_display_dpi());
    phoneMirrorManager.setBitRateUsb(settingsManager.get_scrcpy_bit_rate_usb());
    phoneMirrorManager.setBitRateWifi(settingsManager.get_scrcpy_bit_rate_wifi());
    QObject::connect(&settingsManager, &SettingsManager::scrcpyDisplayDpiChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setDisplayDpi);
    QObject::connect(&settingsManager, &SettingsManager::scrcpyBitRateUsbChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setBitRateUsb);
    QObject::connect(&settingsManager, &SettingsManager::scrcpyBitRateWifiChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setBitRateWifi);
    // Adaptive quality: the per-link rates above become the ceiling
    phoneMirrorManager.setBitRateAuto(settingsManager.get_scrcpy_bit_rate_auto());
    QObject::connect(&settingsManager, &SettingsManager::scrcpyBitRateAutoChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setBitRateAuto);
    // A USB phone is switched to adb over TCP so it can be reached over Wi-Fi later
    phoneMirrorManager.setAutoWifi(settingsManager.get_scrcpy_auto_wifi());
    QObject::connect(&settingsManager, &SettingsManager::scrcpyAutoWifiChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setAutoWifi);
    phoneMirrorManager.setAudioGain(float(settingsManager.get_scrcpy_audio_gain()));
    QObject::connect(&settingsManager, &SettingsManager::scrcpyAudioGainChanged,
                     &phoneMirrorManager, [&phoneMirrorManager](double g) { phoneMirrorManager.setAudioGain(float(g)); });
    // Duck local media while the phone produces sound (nav prompts, video, calls)
    phoneMirrorManager.setAudioDuckEnabled(settingsManager.get_scrcpy_audio_duck_enabled());
    phoneMirrorManager.setAudioDuckLevel(float(settingsManager.get_scrcpy_audio_duck_level()));
    QObject::connect(&settingsManager, &SettingsManager::scrcpyAudioDuckEnabledChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setAudioDuckEnabled);
    QObject::connect(&settingsManager, &SettingsManager::scrcpyAudioDuckLevelChanged,
                     &phoneMirrorManager, [&phoneMirrorManager](double l) { phoneMirrorManager.setAudioDuckLevel(float(l)); });
    QObject::connect(&phoneMirrorManager, &PhoneMirrorManager::duckingChanged,
                     &mediaManager, &MediaManager::setDucking);
    // OCTAVE's mute silences phone audio too (volume arrives via VolumeController)
    phoneMirrorManager.setMuted(mediaManager.is_muted());
    QObject::connect(&mediaManager, &MediaManager::muteChanged, &phoneMirrorManager, &PhoneMirrorManager::setMuted);
    // While mirroring the phone is the media source: when it takes OCTAVE's
    // media controls local playback pauses; starting a local track hands
    // the controls back (and pauses the phone).
    QObject::connect(&phoneMirrorManager, &PhoneMirrorManager::mediaActiveChanged,
                     &mediaManager, [&](bool active) {
        if (active && mediaManager.is_playing())
            mediaManager.pause();
    });
    QObject::connect(&mediaManager, &MediaManager::playStateChanged, &phoneMirrorManager, [&](bool playing) {
        if (playing)
            phoneMirrorManager.releaseMedia(true);
    });
    // Keep the phone's own panel dark while mirroring; a locked phone is woken automatically
    phoneMirrorManager.setPhoneScreenOff(settingsManager.get_scrcpy_phone_screen_off());
    QObject::connect(&settingsManager, &SettingsManager::scrcpyPhoneScreenOffChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setPhoneScreenOff);
    // Phone last connected over Wi-Fi: reconnected at startup, and kept
    // current when it is paired, moves to a new port, or is forgotten
    phoneMirrorManager.setWirelessAddress(settingsManager.get_scrcpy_wireless_address());
    QObject::connect(&phoneMirrorManager, &PhoneMirrorManager::wirelessAddressChanged,
                     &settingsManager, &SettingsManager::save_scrcpy_wireless_address);
#endif

    // Register custom QML types for video embedding (stub types on mobile)
    qmlRegisterType<EmbeddedDhuItem>("OCTAVE.AndroidAuto", 1, 0, "EmbeddedDhuItem");

    // Download Manager — music search & download via yt-dlp
    DownloadManager downloadManager;
    downloadManager.connect_settings_manager(&settingsManager);
    ctx->setContextProperty("downloadManager", &downloadManager);

    // Auto-rescan library when a download completes
    QObject::connect(&downloadManager, &DownloadManager::downloadComplete,
                     [&mediaManager](const QString &, const QString &) {
        mediaManager.scan_library(false);
    });

    // Wire volume controller -> all audio outputs. Media and phone mirror
    // take the curved linear value, the ESP32 LED ring the raw percent (the
    // ESP32 manager rate-limits itself).
    QObject::connect(&volumeController, &VolumeController::volumeApplied,
                     [&mediaManager, &phoneMirrorManager, &esp32VolumeManager](int percent, float linear) {
        mediaManager.setVolume(linear);
        phoneMirrorManager.setVolume(linear);
#ifndef Q_OS_MOBILE
        esp32VolumeManager.send_volume_update(percent);
#else
        Q_UNUSED(esp32VolumeManager);
#endif
    });
    volumeController.applyVolume(settingsManager.currentVolume());

    // ---- QML import path: frontend/ ----
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
    // file:// URLs for frontend/assets files that non-Qt loaders (Quick3D's
    // Assimp importer) must open themselves — extracted from the APK on Android.
    AssetStore assetStore(frontendDir.absoluteFilePath(QStringLiteral("assets")));
    ctx->setContextProperty("assetStore", &assetStore);

    // Prefer editable documentation alongside the checkout; packaged desktop
    // builds fall back to the offline copy embedded in the executable.
#ifdef OCTAVE_HAVE_WEBENGINE
    ctx->setContextProperty("wikiViewerAvailable", true);
    const QString localWiki = frontendDir.absoluteFilePath("../wiki/index.html");
    ctx->setContextProperty("wikiHomeUrl", QFile::exists(localWiki)
        ? QUrl::fromLocalFile(QDir::cleanPath(localWiki))
        : QUrl(QStringLiteral("qrc:/wiki/index.html")));
#elif defined(OCTAVE_HAVE_WEBVIEW)
    // Android: the native WebView reads the wiki copied into the APK's assets
    // (android/assets/wiki). Only asset URLs work here — no qrc:/, no assets:/.
    ctx->setContextProperty("wikiViewerAvailable", true);
    ctx->setContextProperty("wikiHomeUrl", QUrl(QStringLiteral("file:///android_asset/wiki/index.html")));
#else
    ctx->setContextProperty("wikiViewerAvailable", false);
    ctx->setContextProperty("wikiHomeUrl", QUrl());
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

    // ---- Developer tooling (inactive unless --dev / --profile) ----
#ifdef OCTAVE_DEV_TOOLS
    OctaveDev::Runtime devRuntime(app.arguments(), {&engine, &settingsManager, &mediaManager, &obdManager, &berryIMU,
                                                    QDir(frontendDir.absoluteFilePath(QStringLiteral("..")))});
    devRuntime.beforeLoad();
#else
    if (app.arguments().contains(QStringLiteral("--dev")) || app.arguments().contains(QStringLiteral("--profile")))
        qWarning("--dev / --profile need a build with -DOCTAVE_DEV_TOOLS=ON (the default for Debug); ignoring");
#endif

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
#ifdef OCTAVE_DEV_TOOLS
    devRuntime.afterLoad();
#endif
    if (smokeTest) {
        qInfo("Smoke test: Main.qml loaded, exiting in 5 s");
        // CI greps this line: release builds must ship the phone-mirror decoder + adb
        qInfo("Smoke test: phone mirror native: %d", int(phoneMirrorManager.nativeAvailable()));
#ifdef OCTAVE_HAVE_FFMPEG
        // ... and CI greps this one: the decoder release builds link must be LGPL
        // (packaging/ffmpeg/build-h264-decoder.sh, vcpkg's ffmpeg on Windows)
        qInfo("Smoke test: libavcodec %s license: %s", av_version_info(), avcodec_license());
#endif
        QTimer::singleShot(5000, &app, [&app]() { app.exit(0); });
    }

    // Quit the app when the last window is closed (Mod+Q, X button, etc.)
    app.setQuitOnLastWindowClosed(true);
    QObject::connect(&app, &QGuiApplication::lastWindowClosed,
                     &app, &QGuiApplication::quit);

    // Wire gesture sensor actions -> media control (stub never emits on mobile).
    QObject::connect(&gestureSensor, &GestureManager::actionTriggered,
                     [&mediaManager, &volumeController, &settingsManager, &phoneMirrorManager](const QString &action) {
        // Track actions drive the phone while it has the media controls
        const bool phone = phoneMirrorManager.mediaActive();
        if (action == "next_track") phone ? phoneMirrorManager.mediaNext() : mediaManager.next_track();
        else if (action == "previous_track") phone ? phoneMirrorManager.mediaPrevious() : mediaManager.previous_track();
        else if (action == "play_pause") phone ? phoneMirrorManager.mediaPlayPause() : mediaManager.toggle_play();
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

#ifndef Q_OS_MOBILE
    // ESP32 LED ring colour sync.
    // "static" mode pushes the user's picked colour; "theme" mode follows the
    // album-art accent. Cyan (0,255,255) is the fallback when a colour is
    // missing or unparsable.
    auto sendEsp32Rgb = [&esp32VolumeManager](const QString &hex, bool fallbackOnError) {
        const QColor c(hex.trimmed());
        if (c.isValid())
            esp32VolumeManager.send_theme_color(c.red(), c.green(), c.blue());
        else if (fallbackOnError)
            esp32VolumeManager.send_theme_color(0, 255, 255);
    };
    auto accentFromAlbumArt = [](const QString &colorsJson) -> QString {
        const QJsonDocument doc = QJsonDocument::fromJson(colorsJson.toUtf8());
        if (!doc.isObject())
            return QString();
        return doc.object().value(QStringLiteral("accent")).toString(QStringLiteral("#00FFFF"));
    };
    auto sendCurrentLedColor = [&]() {
        if (settingsManager.esp32LedColorMode() == QLatin1String("static")) {
            sendEsp32Rgb(settingsManager.esp32LedStaticColor(), true);
            return;
        }
        const QString accent = accentFromAlbumArt(settingsManager.albumArtColors());
        if (accent.isEmpty())
            esp32VolumeManager.send_theme_color(0, 255, 255);
        else
            sendEsp32Rgb(accent, true);
    };
    QObject::connect(&settingsManager, &SettingsManager::albumArtColorsChanged,
                     &esp32VolumeManager, [&, accentFromAlbumArt, sendEsp32Rgb](const QString &colorsJson) {
        if (settingsManager.esp32LedColorMode() != QLatin1String("theme") || colorsJson.isEmpty())
            return;
        const QString accent = accentFromAlbumArt(colorsJson);
        if (!accent.isEmpty())
            sendEsp32Rgb(accent, false);
    });
    QObject::connect(&settingsManager, &SettingsManager::esp32LedStaticColorChanged,
                     &esp32VolumeManager, [&, sendEsp32Rgb](const QString &color) {
        if (settingsManager.esp32LedColorMode() == QLatin1String("static"))
            sendEsp32Rgb(color, false);
    });
    QObject::connect(&settingsManager, &SettingsManager::esp32LedColorModeChanged,
                     &esp32VolumeManager, [sendCurrentLedColor](const QString &) {
        sendCurrentLedColor();
    });
    // Push the full state (volume / mute / colour) once the knob is up; the
    // 500 ms delay gives the firmware time to finish its own boot after the
    // serial port opens.
    QObject::connect(&esp32VolumeManager, &ESP32VolumeManager::connectionStatusChanged,
                     &esp32VolumeManager, [&, sendCurrentLedColor](const QString &status) {
        if (status != QLatin1String("Connected"))
            return;
        QTimer::singleShot(500, &esp32VolumeManager, [&, sendCurrentLedColor]() {
            esp32VolumeManager.send_volume_update(settingsManager.currentVolume());
            esp32VolumeManager.send_mute_state(mediaManager.is_muted());
            sendCurrentLedColor();
        });
    });
#endif

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
#ifdef OCTAVE_DEV_TOOLS
        devRuntime.shutdown();   // command clients and the perf log go first
#endif
        uiWatchdog.stop();
        androidAutoManager.cleanup();
        phoneMirrorManager.cleanup();
        esp32VolumeManager.cleanup();
        berryIMU.cleanup();
        gestureSensor.cleanup();
        tripRecorder.shutdown();   // closes the trip and writes its files before the adapter goes
        obdManager.close();
        downloadManager.cleanup();
        networkManager.cleanup();
        mediaManager._save_playback_state();   // before the settings flush: saveSettings() is debounced now
        mediaManager.flush_metadata_store();
        settingsManager.flushPendingSave();   // coalesced settings writes land before exit
        OctaveLog::flush();                   // last: everything the cleanup above logged reaches disk
    });

    return app.exec();
}

#include "main.moc"
