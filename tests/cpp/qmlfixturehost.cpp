// Runs one QML fixture from tests/fixtures/ in its own process, with the C++
// managers it needs, the way the Python tests ran each fixture in a fresh
// interpreter. tst_qmlfixtures launches it once per fixture and checks the
// exit code and output. Not a test by itself.
//
//   qmlfixturehost <fixture.qml> <scratch dir> [wiki-available: true|false]
//
// Exit codes: the fixture's own (0 pass, 1 check failed, 2 timed out),
// 3 a component failed to compile, 4 the fixture didn't load, 5 a setting
// didn't survive the disk round trip.

#include <QColor>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPointF>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTest>
#include <QUrl>
#include <cstdio>

#ifdef OCTAVE_HAVE_WEBENGINE
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#endif

#include "managers/settingsmanager.h"
#include "util/assetstore.h"

namespace {
const QString kRepo = QStringLiteral(OCTAVE_SOURCE_DIR);

// What the wiki fixture calls back into (testProbe)
class Probe : public QObject
{
    Q_OBJECT

public:
    explicit Probe(QQmlApplicationEngine *engine) : m_engine(engine) {}

    Q_INVOKABLE void passTest()
    {
        // Printed here, not with console.log: Qt may route that to the journal
        std::printf("WIKI_SMOKE_PASS\n");
        std::fflush(stdout);
    }
    Q_INVOKABLE QObject *find(const QString &name) const
    {
        const auto roots = m_engine->rootObjects();
        return roots.isEmpty() ? nullptr : roots.first()->findChild<QObject *>(name);
    }
    Q_INVOKABLE void click(double x, double y)
    {
        const auto roots = m_engine->rootObjects();
        if (auto *w = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first()))
            QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, QPointF(x, y).toPoint());
    }

private:
    QQmlApplicationEngine *m_engine;
};
} // namespace

int main(int argc, char *argv[])
{
    // Every message to stderr, whatever logging rules the machine has:
    // the test reads console.log markers and QML errors from the output
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &msg) {
        std::fprintf(stderr, "%s\n", qPrintable(msg));
        std::fflush(stderr);
    });

#ifdef OCTAVE_HAVE_WEBENGINE
    QtWebEngineQuick::initialize();
#endif
    QGuiApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: qmlfixturehost <fixture.qml> <scratch dir> [true|false]\n");
        return 64;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QDir scratch(QString::fromLocal8Bit(argv[2]));
    const bool wiki = QFileInfo(fixture).fileName().startsWith(QLatin1String("Wiki"));

    QQmlApplicationEngine engine;
    engine.addImportPath(kRepo + QStringLiteral("/frontend"));
    QQmlContext *ctx = engine.rootContext();
    Probe probe(&engine);
    AssetStore assetStore(kRepo + QStringLiteral("/frontend/assets"));
    ctx->setContextProperty(QStringLiteral("assetStore"), &assetStore);
    std::unique_ptr<SettingsManager> settings;

    if (wiki) {
        ctx->setContextProperty(QStringLiteral("testProbe"), &probe);
        ctx->setContextProperty(QStringLiteral("width"), 1100);
        ctx->setContextProperty(QStringLiteral("height"), 760);
        ctx->setContextProperty(QStringLiteral("settingsManager"), QVariant::fromValue<QObject *>(nullptr));
        ctx->setContextProperty(QStringLiteral("networkManager"), QVariant::fromValue<QObject *>(nullptr));
        ctx->setContextProperty(QStringLiteral("wikiViewerAvailable"), argc > 3 && qstrcmp(argv[3], "true") == 0);
        ctx->setContextProperty(QStringLiteral("wikiHomeUrl"),
                                QUrl::fromLocalFile(kRepo + QStringLiteral("/wiki/index.html")));
    } else {
        // Music device fixtures: two flat-colour covers and a real settings store
        for (const auto &[name, color] : {std::pair{"cover_a", "#ec6354"}, std::pair{"cover_b", "#43b6cf"}}) {
            QImage image(64, 64, QImage::Format_RGB32);
            image.fill(QColor(QString::fromLatin1(color)));
            const QString path = scratch.filePath(QString::fromLatin1(name) + QStringLiteral(".png"));
            image.save(path);
            ctx->setContextProperty(QString::fromLatin1(name), QUrl::fromLocalFile(path));
        }
        settings = std::make_unique<SettingsManager>();
        settings->save_setting(QStringLiteral("musicDeviceModel"), QStringLiteral("Album art"));
        ctx->setContextProperty(QStringLiteral("settingsManager"), settings.get());
        ctx->setContextProperty(QStringLiteral("testSettings"), settings.get());
        // Catch import / type errors in the integration surfaces too, not
        // just the isolated scene
        for (const char *file : {"MediaRoom.qml", "NowPlayingStudio.qml", "settings/MediaSettingsPage.qml"}) {
            QQmlComponent component(&engine, QUrl::fromLocalFile(kRepo + QStringLiteral("/frontend/") + QLatin1String(file)));
            if (component.isError()) {
                std::fprintf(stderr, "%s\n", qPrintable(component.errorString()));
                return 3;
            }
        }
    }

    QObject::connect(&engine, &QQmlApplicationEngine::exit, &app, &QCoreApplication::exit);
    engine.load(QUrl::fromLocalFile(fixture));
    if (engine.rootObjects().isEmpty())
        return 4;
    const int code = app.exec();

    if (settings) {
        // The final selection must survive a disk round trip
        settings->save_setting(QStringLiteral("musicDeviceModel"), QStringLiteral("Record player"));
        settings->flushPendingSave();
        SettingsManager restored;
        if (restored.get_setting_with_default(QStringLiteral("musicDeviceModel"), QStringLiteral("Album art")).toString()
            != QLatin1String("Record player"))
            return 5;
    }
    return code;
}

#include "qmlfixturehost.moc"
