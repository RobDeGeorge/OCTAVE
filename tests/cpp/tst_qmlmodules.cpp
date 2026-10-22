// QmlModules::isInstalled answers "is this optional QML module there" from
// the engine's import paths without loading it (main.cpp's hasQuick3D /
// hasQuick3DAssetUtils, which replaced SensorHome compiling CarMenu.qml).

#include <QDir>
#include <QFile>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTest>

#include "util/qmlmodules.h"

class TstQmlModules : public QObject
{
    Q_OBJECT

private slots:
    void findsModulesByTheirQmldir()
    {
        QQmlEngine engine;
        QVERIFY(QmlModules::isInstalled(engine, QStringLiteral("QtQuick")));   // ships with Qt
        QVERIFY(!QmlModules::isInstalled(engine, QStringLiteral("Not.A.Module")));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("Fake/Module")));
        QFile f(dir.path() + QStringLiteral("/Fake/Module/qmldir"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("module Fake.Module\n");
        f.close();
        QVERIFY(!QmlModules::isInstalled(engine, QStringLiteral("Fake.Module")));
        engine.addImportPath(dir.path());
        QVERIFY(QmlModules::isInstalled(engine, QStringLiteral("Fake.Module")));
        QVERIFY(!QmlModules::isInstalled(engine, QStringLiteral("Fake")));   // the dir alone isn't a module
    }
};

QTEST_MAIN(TstQmlModules)
#include "tst_qmlmodules.moc"
