// The vehicle the app shows (src/managers/vehiclemanager.h): a fresh install
// has none, the built-in ones are always listed and can't be removed, an
// install from before the choice keeps the Jeep, a .glb is copied in whole
// or not at all, a hand-made folder is listed only when its manifest stays
// inside it, and a choice that is gone is no vehicle.

#include "managers/vehiclemanager.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

namespace {
void write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

// Enough of a .glb for the import: the magic and some bytes after it
QByteArray glb(int size = 64)
{
    return QByteArray("glTF") + QByteArray(size - 4, '\x01');
}

const QStringList kBuiltIns{QStringLiteral("jeep-tj"), QStringLiteral("bmw-e46-330i-zhp")};

QStringList ids(const VehicleManager &m)
{
    QStringList out;
    for (const QVariant &v : m.vehicles())
        out << v.toMap().value("id").toString();
    return out;
}
} // namespace

class TstVehicleManager : public QObject
{
    Q_OBJECT
    QTemporaryDir m_tmp;
    QString m_root;
    int m_case = 0;

private slots:
    void init()
    {
        m_root = m_tmp.filePath(QStringLiteral("vehicles%1").arg(++m_case));
    }

    void aFreshInstallHasNoVehicleAndListsTheBuiltIns()
    {
        VehicleManager m;
        QVERIFY(m.activeId().isEmpty());   // even before a root is set
        m.setRoot(m_root);
        QCOMPARE(ids(m), kBuiltIns);
        QVERIFY(m.activeId().isEmpty());
        QVERIFY(m.active().value("id").toString().isEmpty());
        QVERIFY(!m.active().value("rigged").toBool());

        QSignalSpy activeSpy(&m, &VehicleManager::activeChanged);
        QVERIFY(m.select(VehicleManager::kBuiltInId));
        QCOMPARE(activeSpy.count(), 1);
        QVariantMap a = m.active();
        QVERIFY(a.value("builtIn").toBool());
        QVERIFY(a.value("rigged").toBool());
        QVERIFY(a.value("asset").toString().isEmpty());            // the rig has no single model file
        QCOMPARE(a.value("pictureAsset").toString(), QStringLiteral("jeep_tj/preview.png"));

        QVERIFY(m.select("bmw-e46-330i-zhp"));
        a = m.active();
        QVERIFY(a.value("builtIn").toBool());
        QVERIFY(!a.value("rigged").toBool());
        QCOMPARE(a.value("asset").toString(), QStringLiteral("vehicles/bmw-e46-330i-zhp/model.glb"));
        QVERIFY(a.value("modelUrl").toString().isEmpty());

        for (const QString &id : kBuiltIns) {
            QVERIFY(!m.remove(id));
            QVERIFY(!m.lastError().isEmpty());
            QVERIFY(m.picturePath(id).isEmpty());
        }
        // Back to none
        QVERIFY(m.select(""));
        QVERIFY(m.activeId().isEmpty());
        QVERIFY(!QFileInfo::exists(m_root + "/ACTIVE"));
    }

    void anInstallFromBeforeTheChoiceKeepsTheJeep()
    {
        VehicleManager m;
        m.setRoot(m_root, true);
        QCOMPARE(m.activeId(), VehicleManager::kBuiltInId);
        // Only the first time: choosing none afterwards stays none
        QVERIFY(m.select(""));
        VehicleManager again;
        again.setRoot(m_root, true);
        QVERIFY(again.activeId().isEmpty());
    }

    void aGlbIsCopiedInAndBecomesActive()
    {
        const QString src = m_tmp.filePath("My Civic (final).glb");
        write(src, glb());
        VehicleManager m;
        m.setRoot(m_root);
        QSignalSpy activeSpy(&m, &VehicleManager::activeChanged);

        const QString id = m.importModel(QUrl::fromLocalFile(src).toString());
        QCOMPARE(id, QStringLiteral("my-civic-final"));
        QVERIFY(m.lastError().isEmpty());
        QCOMPARE(activeSpy.count(), 1);
        QCOMPARE(m.activeId(), id);
        const QVariantMap a = m.active();
        QCOMPARE(a.value("name").toString(), QStringLiteral("My Civic (final)"));
        QVERIFY(!a.value("rigged").toBool());
        QCOMPARE(QUrl(a.value("modelUrl").toString()).toLocalFile(), m_root + "/my-civic-final/model.glb");
        QCOMPARE(QFileInfo(m_root + "/my-civic-final/model.glb").size(), 64);
        QVERIFY(!QFileInfo::exists(m_root + "/.incoming"));

        // The same file again is a second vehicle, and a plain path works too
        QCOMPARE(m.importModel(src, QStringLiteral("  Track   car ")), QStringLiteral("track-car"));
        QCOMPARE(m.importModel(src), QStringLiteral("my-civic-final-2"));
        QCOMPARE(ids(m), (kBuiltIns + QStringList{"my-civic-final", "my-civic-final-2", "track-car"}));

        // The choice outlives the manager
        VehicleManager again;
        again.setRoot(m_root);
        QCOMPARE(again.activeId(), QStringLiteral("my-civic-final-2"));
    }

    void whatIsNotAGlbIsRefusedAndLeavesNothing()
    {
        VehicleManager m;
        m.setRoot(m_root);
        const QString notModel = m_tmp.filePath("notes.glb");
        write(notModel, "just text, whatever the name says");
        QVERIFY(m.importModel(notModel).isEmpty());
        QVERIFY(m.lastError().contains(".glb"));
        QVERIFY(m.importModel(m_tmp.filePath("missing.glb")).isEmpty());
        QCOMPARE(ids(m), kBuiltIns);
        QCOMPARE(QDir(m_root).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), QStringList());
        QVERIFY(m.activeId().isEmpty());

        // An error clears with the next success
        const QString good = m_tmp.filePath("good.glb");
        write(good, glb());
        QVERIFY(!m.importModel(good).isEmpty());
        QVERIFY(m.lastError().isEmpty());
    }

    void aHandMadeFolderMustKeepItsModelInside()
    {
        const auto manifest = [](const QString &model) {
            return QJsonDocument(QJsonObject{{"name", "Hand made"}, {"model", model}, {"yaw", 450}}).toJson();
        };
        write(m_root + "/good/vehicle.json", manifest("car.gltf"));
        write(m_root + "/good/car.gltf", "{}");
        write(m_root + "/escapes/vehicle.json", manifest("../good/car.gltf"));
        write(m_root + "/absolute/vehicle.json", manifest("/etc/passwd"));
        write(m_root + "/wrong-type/vehicle.json", manifest("car.qml"));
        write(m_root + "/wrong-type/car.qml", "x");
        write(m_root + "/no-model/vehicle.json", manifest("car.glb"));
        write(m_root + "/Bad Name/vehicle.json", manifest("car.glb"));
        write(m_root + "/Bad Name/car.glb", glb());
        write(m_root + "/jeep-tj/vehicle.json", manifest("car.glb"));   // a built-in id is taken
        write(m_root + "/jeep-tj/car.glb", glb());
        write(m_root + "/bmw-e46-330i-zhp/vehicle.json", manifest("car.glb"));
        write(m_root + "/bmw-e46-330i-zhp/car.glb", glb());
        write(m_root + "/no-manifest/car.glb", glb());

        VehicleManager m;
        m.setRoot(m_root);
        QCOMPARE(ids(m), (kBuiltIns + QStringList{"good"}));
        QVERIFY(m.select("good"));
        QCOMPARE(m.active().value("yaw").toDouble(), 90.0);   // 450 wrapped
        QVERIFY(!m.select("escapes"));
        QCOMPARE(m.activeId(), QStringLiteral("good"));
    }

    void orientationAndNameAreSavedAndAnnounced()
    {
        const QString src = m_tmp.filePath("van.glb");
        write(src, glb());
        VehicleManager m;
        m.setRoot(m_root);
        const QString id = m.importModel(src);
        QSignalSpy activeSpy(&m, &VehicleManager::activeChanged);

        QVERIFY(m.setOrientation(id, 270, -90));
        QCOMPARE(activeSpy.count(), 1);
        QCOMPARE(m.active().value("yaw").toDouble(), -90.0);
        QCOMPARE(m.active().value("tilt").toDouble(), -90.0);
        QVERIFY(m.rename(id, "Camper"));
        QVERIFY(!m.rename(id, "   "));
        QVERIFY(!m.setOrientation("nobody", 0, 0));

        VehicleManager again;
        again.setRoot(m_root);
        QCOMPARE(again.active().value("name").toString(), QStringLiteral("Camper"));
        QCOMPARE(again.active().value("yaw").toDouble(), -90.0);
    }

    void thePictureAppearsOnceSaved()
    {
        const QString src = m_tmp.filePath("bus.glb");
        write(src, glb());
        VehicleManager m;
        m.setRoot(m_root);
        const QString id = m.importModel(src);
        QVERIFY(m.active().value("pictureUrl").toString().isEmpty());
        const QString path = m.picturePath(id);
        QCOMPARE(path, m_root + "/bus/preview.png");
        QVERIFY(m.picturePath("nobody").isEmpty());

        write(path, "png");
        QSignalSpy activeSpy(&m, &VehicleManager::activeChanged);
        m.pictureSaved(id);
        QCOMPARE(activeSpy.count(), 1);
        const QUrl url(m.active().value("pictureUrl").toString());
        QCOMPARE(url.toLocalFile(), path);
        QVERIFY(url.hasQuery());
    }

    void removingTheActiveVehicleLeavesNone()
    {
        const QString src = m_tmp.filePath("kart.glb");
        write(src, glb());
        VehicleManager m;
        m.setRoot(m_root);
        const QString id = m.importModel(src);
        QSignalSpy activeSpy(&m, &VehicleManager::activeChanged);
        QVERIFY(m.remove(id));
        QCOMPARE(activeSpy.count(), 1);
        QVERIFY(m.activeId().isEmpty());
        QVERIFY(!QFileInfo::exists(m_root + "/kart"));
        QVERIFY(!m.remove(id));
        QVERIFY(!m.remove("../outside"));

        // A stale ACTIVE (its folder deleted by hand) is no vehicle
        write(m_root + "/ACTIVE", "gone");
        QVERIFY(m.activeId().isEmpty());
        // A model named like a built-in vehicle gets another id
        const QString clash = m_tmp.filePath("Jeep TJ.glb");
        write(clash, glb());
        QCOMPARE(m.importModel(clash), QStringLiteral("vehicle"));
    }
};

QTEST_GUILESS_MAIN(TstVehicleManager)
#include "tst_vehiclemanager.moc"
