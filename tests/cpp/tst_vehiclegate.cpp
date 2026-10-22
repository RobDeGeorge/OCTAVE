// Vehicle support gate, QML side: the real OBDParameterModel singleton and
// DashboardRenderer against a fake `obdManager` with the gate API
// (vehicleSupportedParameters / vehicleScanComplete / setParameterDemand).
// Checks what the renderer shows before a scan, after one, and after the
// vehicle disconnects.

#include <QJSValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <memory>

#include "managers/parametercatalog.h"
#include "managers/parameterstore.h"

class FakeObd : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList vehicleSupportedParameters MEMBER m_supported NOTIFY vehicleSupportedParametersChanged)
    Q_PROPERTY(bool vehicleScanComplete MEMBER m_complete NOTIFY vehicleSupportedParametersChanged)

public:
    Q_INVOKABLE void setParameterDemand(const QString &consumer, const QStringList &ids) { demand[consumer] = ids; }

    void scan(const QStringList &names, bool complete = true)
    {
        m_supported = names;
        m_complete = complete;
        emit vehicleSupportedParametersChanged();
    }

    QHash<QString, QStringList> demand;

signals:
    void vehicleSupportedParametersChanged();

private:
    QStringList m_supported;
    bool m_complete = false;
};

namespace {
const QString kFrontend = QStringLiteral(OCTAVE_SOURCE_DIR "/frontend");

const char *kQml = R"qml(
import QtQuick
import "%1" as App
import "%1/dashboards" as Dash

Item {
    id: root
    width: 1200; height: 600
    property var model: App.OBDParameterModel
    Dash.DashboardRenderer {
        objectName: "renderer"
        anchors.fill: parent
        spec: ({ gridColumns: 12, gridRows: 6, cells: [
            { type: "DigitalReadout", paramId: "RPM",              col: 0, row: 0, colSpan: 6, rowSpan: 3 },
            { type: "DigitalReadout", paramId: "BOOST_PRESSURE_A", col: 6, row: 0, colSpan: 6, rowSpan: 3 },
            { type: "DigitalReadout", paramId: "PITCH",            col: 0, row: 3, colSpan: 6, rowSpan: 3 }
        ] })
    }
    function supported(id) { return App.OBDParameterModel.isSupported(id) }
    // Visual-tree lookup: Repeater delegates aren't QObject children
    function _find(item, name) {
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; i++) {
            var hit = _find(item.children[i], name)
            if (hit) return hit
        }
        return null
    }
    function placeholderVisible(i) {
        var it = _find(root, "dashUnsupported_" + i)
        return it ? it.visible : null
    }
    function vehicleIds() { return App.OBDParameterModel.vehicleParameters.map(function(p) { return p.id }) }
    function holder(id) { return App.OBDParameterModel.valueHolder(id) }
    function push(id, value) { paramStore.ingest(id, value) }   // as obdManager.parameterSampled does
    function setDemo(on) { App.OBDParameterModel.simulationActive = on }
}
)qml";
} // namespace

class TstVehicleGate : public QObject
{
    Q_OBJECT

private slots:
    void gateFollowsTheScan()
    {
        QQmlEngine engine;
        engine.addImportPath(kFrontend);
        FakeObd obd;
        engine.rootContext()->setContextProperty("obdManager", &obd);
        ParameterCatalog catalog;   // as main.cpp registers them
        engine.rootContext()->setContextProperty("parameterCatalog", &catalog);
        ParameterStore store;
        engine.rootContext()->setContextProperty("paramStore", &store);
        QQmlComponent comp(&engine);
        comp.setData(QString::fromLatin1(kQml).arg(QUrl::fromLocalFile(kFrontend).toString()).toUtf8(), QUrl());
        std::unique_ptr<QObject> root(comp.create());
        QVERIFY2(root, qPrintable(comp.errorString()));
        QCoreApplication::processEvents();
        m_root = root.get();

        // No vehicle: nothing gated, Parameter Cards list empty
        for (int i = 0; i < 3; ++i)
            QCOMPARE(placeholder(i), false);
        QCOMPARE(call("vehicleIds").toStringList(), QStringList());
        QCOMPARE(call("supported", "PITCH").toBool(), true);    // sensors never gated
        QCOMPARE(call("supported", "RPM").toBool(), false);     // unknown until scanned

        // Scanned vehicle supports RPM + SPEED only
        obd.scan({"RPM", "SPEED"});
        QCoreApplication::processEvents();
        QCOMPARE(placeholder(0), false);   // RPM supported
        QCOMPARE(placeholder(1), true);    // BOOST not reported: placeholder
        QCOMPARE(placeholder(2), false);   // PITCH is a sensor
        QCOMPARE(call("vehicleIds").toStringList(), (QStringList{"SPEED", "RPM"}));   // allParameters order, OBD only

        // OBDManager publishes an alias with its target (withAliases):
        // supported for dashboards, but no second Parameter Card
        obd.scan({"RPM", "SPEED", "TIMING_ADVANCE", "IGNITION_TIMING"});
        QCoreApplication::processEvents();
        QCOMPARE(call("supported", "IGNITION_TIMING").toBool(), true);
        QCOMPARE(call("vehicleIds").toStringList(), (QStringList{"SPEED", "RPM", "TIMING_ADVANCE"}));

        // Disconnect: gate lifts, list empties again
        obd.scan({}, false);
        QCoreApplication::processEvents();
        for (int i = 0; i < 3; ++i)
            QCOMPARE(placeholder(i), false);
        QCOMPARE(call("vehicleIds").toStringList(), QStringList());
        m_root = nullptr;
    }

    void valueHoldersChangeOnlyForTheirOwnPid()
    {
        // Parameter Cards bind each card to OBDParameterModel.valueHolder(id),
        // so a fold must touch only the holders whose value was in it
        QQmlEngine engine;
        engine.addImportPath(kFrontend);
        FakeObd obd;
        engine.rootContext()->setContextProperty("obdManager", &obd);
        ParameterCatalog catalog;   // as main.cpp registers them
        engine.rootContext()->setContextProperty("parameterCatalog", &catalog);
        ParameterStore store;
        engine.rootContext()->setContextProperty("paramStore", &store);
        QQmlComponent comp(&engine);
        comp.setData(QString::fromLatin1(kQml).arg(QUrl::fromLocalFile(kFrontend).toString()).toUtf8(), QUrl());
        std::unique_ptr<QObject> root(comp.create());
        QVERIFY2(root, qPrintable(comp.errorString()));
        m_root = root.get();

        QObject *rpm = call("holder", "RPM").value<QObject *>();
        QObject *speed = call("holder", "SPEED").value<QObject *>();
        QVERIFY(rpm && speed);
        QVERIFY(call("holder", "RPM").value<QObject *>() == rpm);   // one per PID
        QSignalSpy rpmChanged(rpm, SIGNAL(valueChanged()));
        QSignalSpy speedChanged(speed, SIGNAL(valueChanged()));

        QMetaObject::invokeMethod(m_root, "push", Q_ARG(QVariant, "RPM"), Q_ARG(QVariant, 2500));
        QTRY_COMPARE(rpm->property("value").toInt(), 2500);   // after the ~33 ms fold
        QCOMPARE(rpmChanged.count(), 1);
        QCOMPARE(speedChanged.count(), 0);
        QVERIFY(!speed->property("value").isValid());          // never reported: "--"

        // Demo mode replaces every value at once: holders follow; on exit the
        // fake values go and the real readings come back
        QMetaObject::invokeMethod(m_root, "setDemo", Q_ARG(QVariant, true));
        QVERIFY(speed->property("value").isValid());
        QMetaObject::invokeMethod(m_root, "push", Q_ARG(QVariant, "RPM"), Q_ARG(QVariant, 3100));
        QTest::qWait(3 * ParameterStore::kFlushMs);
        QVERIFY(rpm->property("value").toInt() != 3100);   // demo owns the values meanwhile
        QMetaObject::invokeMethod(m_root, "setDemo", Q_ARG(QVariant, false));
        QVERIFY(!speed->property("value").isValid());
        QCOMPARE(rpm->property("value").toInt(), 3100);
        m_root = nullptr;
    }

    void newerSchemaShowsAMessageAndPollsNothing()
    {
        // A dashboard written by a newer OCTAVE, copied into the folder by hand:
        // no half-rendered cells, no PIDs demanded, one message instead
        QQmlEngine engine;
        engine.addImportPath(kFrontend);
        FakeObd obd;
        engine.rootContext()->setContextProperty("obdManager", &obd);
        ParameterCatalog catalog;   // as main.cpp registers them
        engine.rootContext()->setContextProperty("parameterCatalog", &catalog);
        ParameterStore store;
        engine.rootContext()->setContextProperty("paramStore", &store);
        QQmlComponent comp(&engine);
        comp.setData(QString::fromLatin1(kQml).arg(QUrl::fromLocalFile(kFrontend).toString()).toUtf8(), QUrl());
        std::unique_ptr<QObject> root(comp.create());
        QVERIFY2(root, qPrintable(comp.errorString()));
        QObject *renderer = root->findChild<QObject *>("renderer");
        QVERIFY(renderer);
        QCOMPARE(renderer->property("paramIds").toStringList().size(), 3);

        QVariantMap spec{{"schema", 2}, {"label", "Future"}, {"gridColumns", 12}, {"gridRows", 6},
                         {"cells", QVariantList{QVariantMap{{"type", "DigitalReadout"}, {"paramId", "RPM"}}}}};
        renderer->setProperty("spec", spec);
        QCoreApplication::processEvents();
        QVERIFY(renderer->property("tooNew").toBool());
        QCOMPARE(renderer->property("paramIds").toStringList(), QStringList());
        QObject *message = root->findChild<QObject *>("dashTooNew");
        QVERIFY(message);
        QVERIFY(message->property("visible").toBool());
        QVERIFY(message->property("text").toString().contains("Future"));

        spec["schema"] = 1;   // the current schema renders as usual
        renderer->setProperty("spec", spec);
        QCoreApplication::processEvents();
        QVERIFY(!renderer->property("tooNew").toBool());
        QVERIFY(!message->property("visible").toBool());
        QCOMPARE(renderer->property("paramIds").toStringList(), QStringList{"RPM"});
    }

private:
    QVariant call(const char *method, const QVariant &arg = QVariant())
    {
        QVariant ret;
        const bool ok = arg.isValid()
            ? QMetaObject::invokeMethod(m_root, method, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, arg))
            : QMetaObject::invokeMethod(m_root, method, Q_RETURN_ARG(QVariant, ret));
        if (!ok)
            qWarning() << "no QML function" << method;
        if (ret.metaType() == QMetaType::fromType<QJSValue>())
            ret = ret.value<QJSValue>().toVariant();
        return ret;
    }

    // A placeholder's visibility; fails the test if it doesn't exist
    bool placeholder(int index)
    {
        const QVariant v = call("placeholderVisible", index);
        if (!v.isValid() || v.isNull())
            QTest::qFail(qPrintable(QStringLiteral("placeholder %1 missing").arg(index)), __FILE__, __LINE__);
        return v.toBool();
    }

    QObject *m_root = nullptr;
};

QTEST_MAIN(TstVehicleGate)
#include "tst_vehiclegate.moc"
