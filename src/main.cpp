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

// Every manager, built and wired (src/appcore)
#include "appcore.h"
#include "items/embeddeddhuitem.h"
#include "util/logger.h"
#include "util/qmlmodules.h"
#include "util/uiwatchdog.h"
#include "util/units.h"
#include "service/commandserver.h"
#include "service/servicebridge.h"
#include "service/serviceoptions.h"

#if !defined(Q_OS_WIN) && !defined(Q_OS_MOBILE)
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef OCTAVE_DEV_TOOLS
// --dev / --profile: simulator, UI / perf actions, perf monitor (src/dev/)
#include "dev/devruntime.h"
#endif

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

    // Outlives the engine: bindings that format times still run while it
    // tears down (declared after it, it went first and they read null)
    Units units;
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

    // ---- frontend/: QML, fonts, dashboard presets ----
#ifdef Q_OS_ANDROID
    // On Android, frontend/ is bundled as assets (see android/assets/frontend
    // symlink). Qt supports the "assets:" URL scheme for anything under assets/.
    QDir frontendDir(QStringLiteral("assets:/frontend"));
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
#endif

    // A hung event loop gets logged (octave.ui_watchdog) instead of vanishing
    UiWatchdog uiWatchdog;
    uiWatchdog.start();

    // Every manager, built and wired (src/appcore). Its registry is what the
    // QML UI and the service protocol (src/service/) both see.
    AppCore core(frontendDir, &units);
    core.exposeTo(ctx);
    SettingsManager &settingsManager = core.settingsManager;
    PhoneMirrorManager &phoneMirrorManager = core.phoneMirrorManager;

    engine.addImageProvider(QStringLiteral("dhuframe"), core.androidAutoManager.frameProvider());
    // Register custom QML types for video embedding (stub types on mobile)
    qmlRegisterType<EmbeddedDhuItem>("OCTAVE.AndroidAuto", 1, 0, "EmbeddedDhuItem");
    // OBDManager.ConnectionState values for QML (OBDManager.Connecting, ...)
    qmlRegisterUncreatableType<OBDManager>("OCTAVE.OBD", 1, 0, "OBDManager",
                                           QStringLiteral("use the obdManager context property"));

    // ---- QML import path: frontend/ ----
#ifdef Q_OS_ANDROID
    engine.addImportPath(QStringLiteral("assets:/frontend"));
#else
    engine.addImportPath(frontendDir.absolutePath());
#endif

    // file:// URLs for frontend/assets files that non-Qt loaders (Quick3D's
    // Assimp importer) must open themselves — extracted from the APK on Android.
    AssetStore assetStore(frontendDir.absoluteFilePath(QStringLiteral("assets")));
    ctx->setContextProperty("assetStore", &assetStore);

    // Optional QML modules, checked without loading them: CarMenu's 3D view
    // needs QtQuick3D (SensorHome greys its card without it); the music
    // device objects also need AssetUtils and Timeline (MusicDevicePreference
    // still load-tests the scene before offering them: assimp can be
    // missing). A mobile package bundles whatever the app links
    // (androiddeployqt), so there the build decides; a desktop install may
    // lack a module the build found.
    bool hasQuick3D = QmlModules::isInstalled(engine, QStringLiteral("QtQuick3D"));
    bool hasQuick3DAssetUtils = QmlModules::isInstalled(engine, QStringLiteral("QtQuick3D.AssetUtils"))
                                && QmlModules::isInstalled(engine, QStringLiteral("QtQuick.Timeline"));
#ifdef Q_OS_MOBILE
#ifdef OCTAVE_LINKED_QUICK3D
    hasQuick3D = true;
#endif
#ifdef OCTAVE_LINKED_QUICK3D_ASSETUTILS
    hasQuick3DAssetUtils = true;
#endif
#endif
    ctx->setContextProperty("hasQuick3D", hasQuick3D);
    ctx->setContextProperty("hasQuick3DAssetUtils", hasQuick3DAssetUtils);

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

    // ---- Developer tooling (inactive unless --dev / --profile) ----
#ifdef OCTAVE_DEV_TOOLS
    OctaveDev::Runtime devRuntime(app.arguments(), {&engine, &settingsManager, &core.mediaManager, &core.volumeController, &core.nowPlaying, &core.obdManager, &core.berryIMU,
                                                    QDir(frontendDir.absoluteFilePath(QStringLiteral("..")))});
    devRuntime.beforeLoad();
#else
    if (app.arguments().contains(QStringLiteral("--dev")) || app.arguments().contains(QStringLiteral("--profile")))
        qWarning("--dev / --profile need a build with -DOCTAVE_DEV_TOOLS=ON (the default for Debug); ignoring "
                 "(--service still starts the service)");
#endif

    // ---- Service: the command server + the service protocol (src/service/) ----
    // Off unless asked for (--service, the serviceEnabled setting, or
    // --profile with the dev tools); loopback only unless --service-bind.
#ifdef OCTAVE_DEV_TOOLS
    constexpr bool devToolsBuild = true;
#else
    constexpr bool devToolsBuild = false;
#endif
    const OctaveService::ServiceOptions serviceOptions = OctaveService::parseServiceOptions(
        app.arguments(), settingsManager.get_setting(QStringLiteral("serviceEnabled")).toBool(), devToolsBuild);
    if (!serviceOptions.error.isEmpty())
        qWarning("Service not started: %s", qPrintable(serviceOptions.error));
    // The bridge outlives the server: the server's ops call into it
    std::unique_ptr<OctaveService::ServiceBridge> serviceBridge;
    std::unique_ptr<OctaveService::CommandServer> commandServer;
    if (serviceOptions.enabled) {
        commandServer = std::make_unique<OctaveService::CommandServer>(serviceOptions.port);
        commandServer->setBindAddress(serviceOptions.bind);
#ifdef QT_DEBUG
        constexpr bool debugBuild = true;
#else
        constexpr bool debugBuild = false;
#endif
        serviceBridge = std::make_unique<OctaveService::ServiceBridge>(
            &core.registry, OctaveService::AppInfo{QStringLiteral(OCTAVE_VERSION), debugBuild, false});
        serviceBridge->install(*commandServer);
    }

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
    devRuntime.afterLoad(commandServer.get());   // its UI / perf / sim actions, with --profile
#endif
    if (commandServer)
        commandServer->start();
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

    // Cleanup on quit
    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&]() {
#ifdef OCTAVE_DEV_TOOLS
        devRuntime.shutdown();   // the perf log and a UI recording go first
#endif
        if (commandServer)
            commandServer->stop();   // no client reaches a manager while it cleans up
        uiWatchdog.stop();
        core.shutdown();   // hardware, players, the open trip, then the settings flush
        OctaveLog::flush();                   // last: everything the cleanup above logged reaches disk
    });

    return app.exec();
}

#include "main.moc"
