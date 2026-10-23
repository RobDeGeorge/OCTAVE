#include <QGuiApplication>
#include <cstdio>
#include <cstring>
#include <memory>
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
#include <QTemporaryDir>
#include <QTimer>
#include <QPointer>
#include <QUrl>
#include <QBuffer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QImage>
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
#include "service/apidump.h"
#include "service/serviceoptions.h"
#include "service/streamhub.h"
#ifndef Q_OS_MOBILE
#include "phone_mirror/scrcpyclient.h"
#endif

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
#ifdef OCTAVE_WASM
#include "platform/wasm/webdemo.h"   // the browser demo's simulated car
#endif

namespace {

// frontend/: next to the binary (packages), or one level up (build/)
QDir findFrontendDir()
{
#ifdef Q_OS_ANDROID
    // On Android, frontend/ is bundled as assets (see android/assets/frontend
    // symlink). Qt supports the "assets:" URL scheme for anything under assets/.
    return QDir(QStringLiteral("assets:/frontend"));
#else
    const QString appDir = QGuiApplication::applicationDirPath();
    QDir frontendDir(appDir);
    if (!frontendDir.cd("frontend")) {
        frontendDir = QDir(appDir);
        frontendDir.cdUp();  // build/ -> project root
        if (!frontendDir.cd("frontend")) {
            qWarning("Could not locate frontend/ directory");
        }
    }
    return frontendDir;
#endif
}

// The command server + the service protocol (src/service/), when asked
// for: --service, the serviceEnabled setting, --headless, or --profile with
// the dev tools; loopback only unless --service-bind. Not started yet: the
// dev runtime adds its actions first. The bridge outlives the server (the
// server's ops call into it), so members are in that order.
struct Service {
    std::unique_ptr<OctaveService::ServiceBridge> bridge;
    std::unique_ptr<OctaveService::CommandServer> server;
};

Service makeService(const QStringList &arguments, AppCore &core, bool headless)
{
#ifdef OCTAVE_DEV_TOOLS
    constexpr bool devToolsBuild = true;
#else
    constexpr bool devToolsBuild = false;
#endif
#ifdef QT_DEBUG
    constexpr bool debugBuild = true;
#else
    constexpr bool debugBuild = false;
#endif
    const OctaveService::ServiceOptions options = OctaveService::parseServiceOptions(
        arguments, core.settingsManager.get_setting(QStringLiteral("serviceEnabled")).toBool(), devToolsBuild,
        core.settingsManager.get_setting(QStringLiteral("serviceWebRoot")).toString());
    if (!options.error.isEmpty())
        qWarning("Service not started: %s", qPrintable(options.error));
    Service service;
    if (!options.enabled && options.error.isEmpty())
        qInfo("Service off (--service, --headless or the serviceEnabled setting turns it on)");
    if (options.enabled) {
        service.server = std::make_unique<OctaveService::CommandServer>(options.port);
        service.server->setBindAddress(options.bind);
        // The reference browser client (sdk/web), same-origin with the socket
        QFile page(QStringLiteral(":/web-client/index.html"));
        if (page.open(QIODevice::ReadOnly))
            service.server->setPage(page.readAll());
        // Or a frontend's own files (--web-root / serviceWebRoot)
        if (!options.webRoot.isEmpty()) {
            if (const QString problem = OctaveService::webRootProblem(options.webRoot); problem.isEmpty()) {
                service.server->setWebRoot(options.webRoot);
                qInfo("Service: serving %s at GET /", qPrintable(options.webRoot));
            } else {
                qWarning("Service: web root not used (%s); serving the reference client", qPrintable(problem));
            }
        }
        service.bridge = std::make_unique<OctaveService::ServiceBridge>(
            &core.registry, OctaveService::AppInfo{QStringLiteral(OCTAVE_VERSION), debugBuild, headless});
        // The current cover, so a client can show it: only nowPlaying's
        // own art file, never a path from the request
        // /art?song=<library filename> and /art?queue=<phone queue id>: a
        // library row's or a queue item's cover, resolved only through
        // the manager's own lookup and only from its own art folder
        QPointer<NowPlaying> nowPlaying = &core.nowPlaying;
        QPointer<MediaManager> media = &core.mediaManager;
        QPointer<PhoneMirrorManager> phone = &core.phoneMirrorManager;
        service.server->addRoute("/art", [nowPlaying, media, phone](const QByteArray &target) {
            const QString song = OctaveService::StaticFiles::queryValue(target, QStringLiteral("song"));
            const QString queue = OctaveService::StaticFiles::queryValue(target, QStringLiteral("queue"));
            const QString file = OctaveService::CommandServer::onGui([=]() -> QVariant {
                if (!song.isNull())
                    return media ? media->libraryArtFile(song) : QString();
                if (!queue.isNull()) {
                    bool ok = false;
                    const qint64 id = queue.toLongLong(&ok);
                    return ok && phone ? phone->queueArtFile(id) : QString();
                }
                const QUrl url(nowPlaying ? nowPlaying->artUrl() : QString());
                return url.isLocalFile() ? url.toLocalFile() : QString();
            }, 15000).toString();
            return OctaveService::StaticFiles::serveImage(file);
        });
        // GET /export/<kind>: what the app itself last exported (to the
        // head unit's Downloads), as an attachment; never a requested path
        QPointer<TripRecorder> trips = &core.tripRecorder;
        QPointer<DiagnosticsManager> diagnostics = &core.diagnosticsManager;
        QPointer<DashboardManager> dashboards = &core.dashboardManager;
        auto lastExport = [](QPointer<QObject> o) {
            return OctaveService::CommandServer::onGui([o]() {
                return QVariant(o ? o->property("lastExportPath").toString() : QString());
            }, 5000).toString();
        };
        service.server->addRoute("/export/trip", [trips, lastExport](const QByteArray &target) {
            QString csv = lastExport(trips.data());
            if (OctaveService::StaticFiles::queryValue(target, QStringLiteral("format")) == QLatin1String("json")
                && csv.endsWith(QLatin1String(".csv")))
                csv = csv.left(csv.size() - 4) + QStringLiteral(".json");   // its sibling, written with it
            return OctaveService::StaticFiles::serveDownload(csv);
        });
        service.server->addRoute("/export/dashboard", [dashboards, lastExport](const QByteArray &) {
            return OctaveService::StaticFiles::serveDownload(lastExport(dashboards.data()));
        });
        service.server->addRoute("/export/logs", [diagnostics, lastExport](const QByteArray &target) {
            return OctaveService::StaticFiles::serveFromFolder(
                lastExport(diagnostics.data()), OctaveService::StaticFiles::queryValue(target, QStringLiteral("file")));
        });
        // Media streams (src/service/streamhub.h): the phone's H.264 as it
        // arrives and Android Auto's frames as JPEG, to `stream` clients
#ifndef Q_OS_MOBILE
        {
            using OctaveService::StreamHub;
            QPointer<PhoneMirrorManager> phoneForKeys = &core.phoneMirrorManager;
            StreamHub::instance().declare(QStringLiteral("phoneMirrorManager.video"), [phoneForKeys]() {
                QMetaObject::invokeMethod(phoneForKeys, [phoneForKeys] {
                    if (phoneForKeys)
                        phoneForKeys->requestVideoKeyframe();
                }, Qt::QueuedConnection);
            });
            ScrcpyClient::setVideoTap({
                [] { return StreamHub::instance().wanted(QStringLiteral("phoneMirrorManager.video")); },
                [](const QByteArray &annexB, qint64 ptsUs, bool key, bool config) {
                    StreamHub::instance().publishH264(QStringLiteral("phoneMirrorManager.video"), annexB, ptsUs, key, config);
                }});
            StreamHub::instance().declare(QStringLiteral("androidAutoManager.frames"));
            DhuFrameProvider::setFrameTap([](const QImage &image) {
                // Encoded only while someone watches, at most 15 frames a second
                static QElapsedTimer last;
                if (image.isNull() || !StreamHub::instance().wanted(QStringLiteral("androidAutoManager.frames"))
                    || (last.isValid() && last.elapsed() < 66))
                    return;
                last.start();
                QByteArray jpeg;
                QBuffer buffer(&jpeg);
                buffer.open(QIODevice::WriteOnly);
                image.save(&buffer, "JPEG", 75);
                StreamHub::instance().publishJpeg(QStringLiteral("androidAutoManager.frames"), jpeg,
                                                  QDateTime::currentMSecsSinceEpoch() * 1000);
            });
        }
#endif
        // A client that goes without saying so leaves no demand behind
        service.bridge->releaseOnClose(QStringLiteral("obdManager"), QStringLiteral("setParameterDemand"), QStringList());
        service.bridge->releaseOnClose(QStringLiteral("obdManager"), QStringLiteral("setBackgroundDemand"), QStringList());
        service.bridge->releaseOnClose(QStringLiteral("berryIMU"), QStringLiteral("setDemand"), false);
        service.bridge->releaseOnClose(QStringLiteral("audioAnalyzer"), QStringLiteral("setDemand"), false);
        service.bridge->install(*service.server);
    }
    return service;
}

// SIGTERM/SIGINT (launcher scripts, systemd, Ctrl-C) must run the
// aboutToQuit cleanup, otherwise child processes and adb forwards are
// orphaned. Self-pipe trick: the handler only writes a byte; the notifier
// turns it into app.quit() on the event loop.
void quitOnSignals(QCoreApplication &app)
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_MOBILE)
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
#else
    Q_UNUSED(app);
#endif
}

// --dump-api DIR: the published API (docs/api/, src/service/apidump.h)
// from the managers' meta-objects, then exit. The managers are really
// built, so main() has pointed the config at a throwaway directory first.
// The event loop never runs, so the managers' deferred starts (the OBD
// connection, the ESP32 and IMU probes) never fire; only TripRecorder's
// thread and database come up, in that directory, and shutdown() closes them.
int dumpApi(QGuiApplication &app, const QString &dir)
{
    Units units;
    AppCore core(findFrontendDir(), &units);
    QString error;
    const bool ok = OctaveService::writeApiDocs(core.registry, QDir(dir), &error);
    core.shutdown();
    if (!ok) {
        qCritical("--dump-api: %s", qPrintable(error));
        return 1;
    }
    qInfo("API written to %s", qPrintable(QDir(dir).absolutePath()));
    Q_UNUSED(app);
    return 0;
}

#ifndef Q_OS_MOBILE
// --headless: every manager and the service, no QML engine and no window
// (TODO/open-backend.md, contract step 2). Another frontend, a script or
// the SDK drives it over the service protocol; video (phone mirror,
// Android Auto) has nowhere to go and is not decoded for anyone.
int runHeadless(QGuiApplication &app, bool smokeTest)
{
#ifndef OCTAVE_DEV_TOOLS
    if (app.arguments().contains(QStringLiteral("--dev")) || app.arguments().contains(QStringLiteral("--profile")))
        qWarning("--dev / --profile need a build with -DOCTAVE_DEV_TOOLS=ON; ignored with --headless");
#endif

    // A stalled event loop still gets logged (octave.ui_watchdog)
    UiWatchdog uiWatchdog;
    uiWatchdog.start();
    Units units;
    AppCore core(findFrontendDir(), &units);
    // The QML engine owns this provider in a normal run; here nobody does.
    // Declared after `core`, so it goes first: safe, because core.shutdown()
    // has run AndroidAutoManager::cleanup() by then and DhuCapture's
    // destructor never touches the provider
    std::unique_ptr<QQuickImageProvider> dhuFrames(core.androidAutoManager.frameProvider());

    Service service = makeService(app.arguments(), core, true);
#ifdef OCTAVE_DEV_TOOLS
    // --headless --dev: the simulated car and its sim_* actions, no UI
    const QDir frontendDir = findFrontendDir();
    OctaveDev::Runtime devRuntime(app.arguments(), {nullptr, &core.settingsManager, &core.mediaManager, &core.volumeController,
                                                    &core.nowPlaying, &core.obdManager, &core.berryIMU,
                                                    QDir(frontendDir.absoluteFilePath(QStringLiteral("..")))});
    devRuntime.startHeadless(service.server.get());
#endif
    if (!service.server || !service.server->start()) {
        qCritical("--headless: the service did not start, nothing could reach the app; exiting");
        core.shutdown();
        return 1;
    }
    qInfo("Headless: %d objects served on port %u", int(core.registry.names().size()), service.server->port());
    if (smokeTest) {
        qInfo("Smoke test: headless core up, exiting in 5 s");
        QTimer::singleShot(5000, &app, [&app]() { app.exit(0); });
    }

    quitOnSignals(app);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&]() {
        service.server->stop();   // no client reaches a manager while it cleans up
        uiWatchdog.stop();
        core.shutdown();
        OctaveLog::flush();
    });
    return app.exec();
}
#endif

// Every flag the app reads, for --help (and the unknown-flag warning).
// Keep in step with the parsing below, serviceoptions.cpp and devruntime.
struct Flag {
    const char *name;
    const char *arg;    // "" = none
    const char *help;
};
constexpr Flag kFlags[] = {
    {"--help", "", "print this and exit (also -h)"},
    {"--version", "", "print the version and exit"},
    {"--service", "[PORT]", "serve the service protocol on 127.0.0.1:PORT (9876); also --service=PORT"},
    {"--service-bind", "ADDRESS", "listen on ADDRESS instead of 127.0.0.1 (no authentication: read the docs first)"},
    {"--command-port", "PORT", "the service port, older spelling"},
    {"--web-root", "DIR", "serve DIR (a built web frontend with index.html) at GET / instead of the reference client"},
    {"--headless", "", "every manager and the service, no QML engine and no window"},
    {"--dump-api", "DIR", "write the published API (docs/api) to DIR and exit"},
    {"--smoke", "", "start, run 5 s, exit 0 (CI)"},
    {"--debug", "", "debug logging (octave-cpp-debug.log)"},
#ifdef OCTAVE_DEV_TOOLS
    {"--dev", "", "simulated car (OBD and IMU) and the dev overlay; with --headless, no UI"},
    {"--profile", "", "perf monitor, and the service with the UI / perf / sim actions"},
#endif
};

// --help / --version, or "" to start: decided before any manager, config
// read or log file exists
QString earlyExitText(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0)
            return QStringLiteral("OCTAVE %1\n").arg(QStringLiteral(OCTAVE_VERSION));
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            QString text = QStringLiteral("OCTAVE %1\nUsage: octave [options]\n\n").arg(QStringLiteral(OCTAVE_VERSION));
            for (const Flag &f : kFlags) {
                const QString left = QStringLiteral("  %1 %2").arg(QLatin1String(f.name), QLatin1String(f.arg)).trimmed();
                text += QStringLiteral("  %1  %2\n").arg(left.leftJustified(24), QLatin1String(f.help));
            }
            return text + QStringLiteral("\nDocumentation: wiki/development.html, wiki/writing-a-frontend.html\n");
        }
    }
    return {};
}

// A "--flag" this build doesn't know (a typo, or a flag from another
// build): said on stderr before anything starts
void warnUnknownFlags(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (!a.startsWith(QLatin1String("--")) || a.startsWith(QLatin1String("--qmljsdebugger")))
            continue;
        const QString name = a.section(QLatin1Char('='), 0, 0);
        bool known = false;
        for (const Flag &f : kFlags)
            known = known || name == QLatin1String(f.name);
#ifndef OCTAVE_DEV_TOOLS
        known = known || name == QLatin1String("--dev") || name == QLatin1String("--profile");   // warned about later
#endif
        if (!known)
            std::fprintf(stderr, "octave: unknown option %s (see octave --help)\n", qPrintable(name));
    }
}

} // namespace

int main(int argc, char *argv[])
{
    if (const QString text = earlyExitText(argc, argv); !text.isEmpty()) {
        std::fputs(text.toLocal8Bit().constData(), stdout);
        return 0;
    }
    warnUnknownFlags(argc, argv);

    // Persistent logging + crash handlers first, so anything the managers
    // say during construction (and any crash) reaches the log files.
    bool debugLogging = false;
    // --smoke: load Main.qml, run 5 s (startup work, page pre-building), then
    // exit 0. A packaged build that cannot start (missing DLL, no frontend/)
    // exits non-zero or never starts, so CI can launch the real artifact.
    bool smokeTest = false;
    // --headless: the managers and the service, no UI (runHeadless)
    bool headless = false;
    // --dump-api DIR: write the published API and exit (dumpApi)
    QString dumpApiDir;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0)
            debugLogging = true;
        else if (std::strcmp(argv[i], "--smoke") == 0)
            smokeTest = true;
        else if (std::strcmp(argv[i], "--headless") == 0)
            headless = true;
        else if (std::strcmp(argv[i], "--dump-api") == 0 && i + 1 < argc)
            dumpApiDir = QString::fromLocal8Bit(argv[++i]);
    }

    // --dump-api builds every manager: keep its settings, trip database and
    // logs out of the real config (set before the logger picks its folder)
    std::unique_ptr<QTemporaryDir> throwawayConfig;
    if (!dumpApiDir.isEmpty()) {
        dumpApiDir = QDir(dumpApiDir).absolutePath();
        throwawayConfig = std::make_unique<QTemporaryDir>();
        const QByteArray home = throwawayConfig->path().toLocal8Bit();
        for (const char *var : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "APPDATA", "LOCALAPPDATA"})
            qputenv(var, home);
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    OctaveLog::install(debugLogging);

    if (!dumpApiDir.isEmpty()) {
        QGuiApplication app(argc, argv);
        app.setOrganizationName("OCTAVE");
        app.setApplicationName("OCTAVE");
        return dumpApi(app, dumpApiDir);
    }

#ifndef Q_OS_MOBILE
    if (headless) {
        // No window: the offscreen platform unless the caller picked one.
        // QGuiApplication still, for the fonts and images the managers use.
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
        QGuiApplication app(argc, argv);
        app.setOrganizationName("OCTAVE");
        app.setApplicationName("OCTAVE");
        return runHeadless(app, smokeTest);
    }
#else
    if (headless)
        qWarning("--headless is for desktop and the Pi; ignored");
#endif

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
    const QDir frontendDir = findFrontendDir();

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

#ifdef OCTAVE_WASM
    // ---- Browser demo: the simulated car on the real OBDManager ----
    WebDemo webDemo(&core.obdManager, &core.berryIMU);
#endif

    // ---- Service (makeService: off unless asked for) ----
    Service service = makeService(app.arguments(), core, false);

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
    devRuntime.afterLoad(service.server.get());   // its UI / perf / sim actions, with --profile
#endif
    if (service.server)
        service.server->start();
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

    quitOnSignals(app);

    // Cleanup on quit
    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&]() {
#ifdef OCTAVE_DEV_TOOLS
        devRuntime.shutdown();   // the perf log and a UI recording go first
#endif
        if (service.server)
            service.server->stop();   // no client reaches a manager while it cleans up
        uiWatchdog.stop();
        core.shutdown();   // hardware, players, the open trip, then the settings flush
        OctaveLog::flush();                   // last: everything the cleanup above logged reaches disk
    });

    return app.exec();
}

#include "main.moc"
