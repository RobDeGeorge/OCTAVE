// ParameterStore holds the latest value of every OBD PID and IMU reading, one
// notifying key per id. It replaces the 84 per-signal QML handlers (fed from
// OBDManager::parameterSampled and the BerryIMU signals in main.cpp) and the
// QML map that was copied whole on every update. Pins: writes are coalesced
// (the IMU alone sends ~440 values a second), a binding on one id does not
// re-evaluate when another id changes, an alias follows its target, and
// unknown ids read undefined (the "--" readouts rely on it).

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>

#include <memory>

#include "managers/parametercatalog.h"
#include "managers/parameterstore.h"

namespace {
const char *kQml = R"qml(
import QtQuick

QtObject {
    readonly property var _count: ({ n: 0 })   // not a property: no binding loop
    property real rpm: { _count.n++; return paramStore.RPM === undefined ? -1 : paramStore.RPM }
    function rpmEvaluations() { return _count.n }
    readonly property bool hasBoost: paramStore.BOOST_PRESSURE_A !== undefined
    function write() { paramStore.RPM = 1 }
    function byName(id) { return paramStore[id] }
}
)qml";
} // namespace

int evaluations(QObject *root)
{
    QVariant n;
    QMetaObject::invokeMethod(root, "rpmEvaluations", Q_RETURN_ARG(QVariant, n));
    return n.toInt();
}

class TstParameterStore : public QObject
{
    Q_OBJECT

private slots:
    void writesAreCoalesced()
    {
        ParameterStore store;
        QSignalSpy changed(&store, &ParameterStore::changed);
        store.ingest("RPM", 800);
        store.ingest("RPM", 900);
        store.ingest("SPEED", 12);
        QVERIFY(store.get("RPM").isNull());   // not before the fold
        QTRY_COMPARE(changed.count(), 1);
        QStringList ids = changed.first().first().toStringList();
        ids.sort();
        QCOMPARE(ids, (QStringList{"RPM", "SPEED"}));
        QCOMPARE(store.get("RPM").toDouble(), 900.0);   // the last one wins
        QCOMPARE(store.get("SPEED").toDouble(), 12.0);

        // Idle: nothing more
        QTest::qWait(3 * ParameterStore::kFlushMs);
        QCOMPARE(changed.count(), 1);
    }

    void aliasFollowsItsTarget()
    {
        ParameterStore store;
        store.ingest("TIMING_ADVANCE", 14.5f);
        QTRY_COMPARE(store.get("IGNITION_TIMING").toDouble(), 14.5);
        QCOMPARE(store.get("TIMING_ADVANCE").toDouble(), 14.5);
    }

    void snapshotHasEveryValue()
    {
        ParameterStore store;
        store.ingest("RPM", 1000);
        store.ingest("PITCH", -3.5f);
        QTRY_VERIFY(!store.get("PITCH").isNull());
        const QVariantMap snap = store.snapshot();
        QCOMPARE(snap.size(), 2);
        QCOMPARE(snap.value("PITCH").toDouble(), -3.5);
    }

    void qmlBindingsArePerKey()
    {
        QQmlEngine engine;
        ParameterStore store;
        engine.rootContext()->setContextProperty("paramStore", &store);
        QQmlComponent comp(&engine);
        comp.setData(kQml, QUrl());
        std::unique_ptr<QObject> root(comp.create());
        QVERIFY2(root, qPrintable(comp.errorString()));

        QCOMPARE(root->property("rpm").toDouble(), -1.0);   // no reading yet: undefined
        QCOMPARE(root->property("hasBoost").toBool(), false);
        // Reading a key from QML creates it, empty: snapshot() lists values only
        QVERIFY(store.snapshot().isEmpty());
        QVERIFY(store.get("RPM").isNull());
        const int before = evaluations(root.get());

        store.ingest("RPM", 2500);
        QTRY_COMPARE(root->property("rpm").toDouble(), 2500.0);
        const int afterRpm = evaluations(root.get());
        QVERIFY(afterRpm > before);

        // Other ids change: the RPM binding stays put (the whole-map
        // paramValues re-evaluated every reader on every fold)
        for (int i = 0; i < 5; ++i) {
            store.ingest("SPEED", 10 + i);
            store.ingest("PITCH", i);
            QTest::qWait(ParameterStore::kFlushMs + 10);
        }
        QCOMPARE(evaluations(root.get()), afterRpm);

        store.ingest("BOOST_PRESSURE_A", 150);
        QTRY_COMPARE(root->property("hasBoost").toBool(), true);

        QVariant v;
        QVERIFY(QMetaObject::invokeMethod(root.get(), "byName", Q_RETURN_ARG(QVariant, v),
                                          Q_ARG(QVariant, QStringLiteral("SPEED"))));
        QCOMPARE(v.toDouble(), 14.0);
    }

    void qmlCannotWrite()
    {
        QQmlEngine engine;
        ParameterStore store;
        engine.rootContext()->setContextProperty("paramStore", &store);
        QQmlComponent comp(&engine);
        comp.setData(kQml, QUrl());
        std::unique_ptr<QObject> root(comp.create());
        QVERIFY(root);
        store.ingest("RPM", 3000);
        QTRY_COMPARE(store.get("RPM").toDouble(), 3000.0);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "write"));
        QCOMPARE(store.get("RPM").toDouble(), 3000.0);
    }
};

QTEST_MAIN(TstParameterStore)
#include "tst_parameterstore.moc"
