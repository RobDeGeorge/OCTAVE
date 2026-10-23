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
    // What every gauge, home card and Parameter Card binds: liveValue(id).
    // The counters are plain JS (not properties) so counting is not a loop.
    readonly property var _evals: ({ rpm: 0, speed: 0 })
    readonly property var rpmLive: { _evals.rpm++; return App.OBDParameterModel.liveValue("RPM") }
    readonly property var speedLive: { _evals.speed++; return App.OBDParameterModel.liveValue("SPEED") }
    function evals(which) { return _evals[which] }
    function push(id, value) { paramStore.ingest(id, value) }   // as obdManager.parameterSampled does
    function setDemo(on) { App.OBDParameterModel.simulationActive = on }
    readonly property var coolantLive: App.OBDParameterModel.liveValue("COOLANT_TEMP")
    function unitOf(id) { return App.OBDParameterModel.getParamInfo(id).unit }
    function nativeUnit(id) { return App.OBDParameterModel.nativeUnit(id) }
    function unitProps(type) { return App.WidgetCatalog.paramUnitProps(type) }
    function fromDisplay(id, v) { return App.OBDParameterModel.fromDisplay(id, v) }
    function rawValue(id) { return App.OBDParameterModel.rawValue(id) }
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

    void liveValuesChangeOnlyForTheirOwnPid()
    {
        // Every reader binds OBDParameterModel.liveValue(id), which reads
        // paramStore[id]: a value arriving for one PID re-evaluates only that
        // PID's readers (the whole-map paramValues re-evaluated them all)
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

        QVERIFY(!m_root->property("rpmLive").isValid());    // never reported: "--"
        QVERIFY(!m_root->property("speedLive").isValid());
        const int speed0 = call("evals", "speed").toInt();

        QMetaObject::invokeMethod(m_root, "push", Q_ARG(QVariant, "RPM"), Q_ARG(QVariant, 2500));
        QTRY_COMPARE(m_root->property("rpmLive").toInt(), 2500);   // after the ~33 ms fold
        const int rpm1 = call("evals", "rpm").toInt();
        for (int i = 0; i < 4; ++i) {
            QMetaObject::invokeMethod(m_root, "push", Q_ARG(QVariant, "PITCH"), Q_ARG(QVariant, i));
            QTest::qWait(ParameterStore::kFlushMs + 10);
        }
        QCOMPARE(call("evals", "rpm").toInt(), rpm1);       // other PIDs: RPM readers untouched
        QCOMPARE(call("evals", "speed").toInt(), speed0);
        QVERIFY(!m_root->property("speedLive").isValid());

        // Demo mode serves its sweep instead; on exit the fake values go and
        // the real readings are back
        QMetaObject::invokeMethod(m_root, "setDemo", Q_ARG(QVariant, true));
        QVERIFY(m_root->property("speedLive").isValid());
        QMetaObject::invokeMethod(m_root, "push", Q_ARG(QVariant, "RPM"), Q_ARG(QVariant, 3100));
        QTest::qWait(3 * ParameterStore::kFlushMs);
        QVERIFY(m_root->property("rpmLive").toInt() != 3100);   // demo owns the values meanwhile
        QMetaObject::invokeMethod(m_root, "setDemo", Q_ARG(QVariant, false));
        QVERIFY(!m_root->property("speedLive").isValid());
        QCOMPARE(m_root->property("rpmLive").toInt(), 3100);
        m_root = nullptr;
    }

    void gaugesFollowTheUnitSetting()
    {
        // Gauges read display units; a unit change re-evaluates every
        // reader at once, with no new sample (a parked car, the editor demo)
        QQmlEngine engine;
        engine.addImportPath(kFrontend);
        FakeObd obd;
        engine.rootContext()->setContextProperty("obdManager", &obd);
        ParameterCatalog catalog;
        engine.rootContext()->setContextProperty("parameterCatalog", &catalog);
        ParameterStore store;
        engine.rootContext()->setContextProperty("paramStore", &store);
        QQmlComponent comp(&engine);
        comp.setData(QString::fromLatin1(kQml).arg(QUrl::fromLocalFile(kFrontend).toString()).toUtf8(), QUrl());
        std::unique_ptr<QObject> root(comp.create());
        QVERIFY2(root, qPrintable(comp.errorString()));
        m_root = root.get();

        QMetaObject::invokeMethod(m_root, "push", Q_ARG(QVariant, "COOLANT_TEMP"), Q_ARG(QVariant, 100));
        QTRY_COMPARE(m_root->property("coolantLive").toDouble(), 100.0);
        QCOMPARE(call("unitOf", "COOLANT_TEMP").toString(), QStringLiteral("°C"));

        catalog.setDisplayUnits("F", "mph", "m");
        QCOMPARE(m_root->property("coolantLive").toDouble(), 212.0);
        QCOMPARE(call("unitOf", "COOLANT_TEMP").toString(), QStringLiteral("°F"));
        QCOMPARE(call("nativeUnit", "COOLANT_TEMP").toString(), QStringLiteral("°C"));   // Trip History's
        QCOMPARE(call("rawValue", "COOLANT_TEMP").toDouble(), 100.0);   // the native read stays put
        QVariant stored;
        QVERIFY(QMetaObject::invokeMethod(m_root, "fromDisplay", Q_RETURN_ARG(QVariant, stored),
                                          Q_ARG(QVariant, "COOLANT_TEMP"), Q_ARG(QVariant, 221)));
        QCOMPARE(stored.toDouble(), 105.0);   // what the editor stores for a typed 221 °F

        // Range props are converted for these widgets (sport.json's 105 °C)
        QVERIFY(call("unitProps", "BarGauge").toStringList().contains("warnAbove"));
        QVERIFY(call("unitProps", "CircularGauge").toStringList().contains("redlineStart"));
        QVERIFY(call("unitProps", "WarningLight").toStringList().contains("triggerBelow"));
        QVERIFY(!call("unitProps", "CircularGauge").toStringList().contains("startAngle"));
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
