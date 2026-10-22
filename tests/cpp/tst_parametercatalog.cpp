// ParameterCatalog is the one table of display metadata (title, unit, range,
// kind, decimals) for every OBD PID and IMU reading. Two guards:
//   - its OBD ids are exactly the ids ELM327Protocol can decode, so a new PID
//     cannot ship without metadata or the other way round;
//   - it matches what the QML OBDParameterModel shows, field by field, so the
//     move from the QML table to this one changes nothing users see.

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSet>
#include <QTest>
#include <QUrl>

#include <memory>

#include "managers/elm327protocol.h"
#include "managers/parametercatalog.h"

namespace {
const QString kFrontend = QStringLiteral(OCTAVE_SOURCE_DIR "/frontend");

const char *kQml = R"qml(
import QtQuick
import "%1" as App

QtObject {
    property var model: App.OBDParameterModel
}
)qml";
} // namespace

class TstParameterCatalog : public QObject
{
    Q_OBJECT

private slots:
    void idsAreUniqueAndComplete()
    {
        const auto &all = ParameterCatalog::all();
        QCOMPARE(all.size(), 129);
        QSet<QString> seen;
        for (const auto &p : all) {
            QVERIFY2(!seen.contains(p.id), qPrintable(p.id));
            seen.insert(p.id);
            QVERIFY2(!p.title.isEmpty(), qPrintable(p.id));
            QVERIFY2(p.max > p.min, qPrintable(p.id));
            QVERIFY2(QStringList({"percentage", "temperature", "voltage", "pressure",
                                  "bidirectional", "numeric"}).contains(p.kind),
                     qPrintable(p.id + " " + p.kind));
            QVERIFY2(p.decimals == 0 || p.decimals == 1, qPrintable(p.id));
            if (!p.aliasOf.isEmpty())
                QVERIFY2(ParameterCatalog::find(p.aliasOf), qPrintable(p.id + " -> " + p.aliasOf));
        }
        QCOMPARE(ParameterCatalog::imuIds(),
                 (QStringList{"PITCH", "ROLL", "HEADING", "ALTITUDE", "ACCEL_MAG",
                              "LATERAL_G", "LONGITUDINAL_G", "BARO_TEMP"}));
        QVERIFY(!ParameterCatalog::find("NOPE"));
    }

    void obdIdsMatchTheDecoders()
    {
        QSet<QString> decoded;
        for (const PidEntry &e : ELM327Protocol::pidTable())
            decoded.insert(e.commandName);
        for (const auto &group : ELM327Protocol::extendedPidTable())
            for (const ExtendedSignal &s : group)
                decoded.insert(s.paramId);

        const QStringList catalog = ParameterCatalog::obdIds();
        const QSet<QString> catalogSet(catalog.cbegin(), catalog.cend());
        QStringList missingMeta, missingDecoder;
        for (const QString &id : decoded)
            if (!catalogSet.contains(id))
                missingMeta << id;
        for (const QString &id : catalogSet)
            if (!decoded.contains(id))
                missingDecoder << id;
        QVERIFY2(missingMeta.isEmpty(), qPrintable("decoded but no metadata: " + missingMeta.join(", ")));
        QVERIFY2(missingDecoder.isEmpty(), qPrintable("metadata but no decoder: " + missingDecoder.join(", ")));
    }

    void invokablesForQml()
    {
        ParameterCatalog catalog;
        QCOMPARE(catalog.parameters().size(), 129);
        const QVariantMap rpm = catalog.info("RPM");
        QCOMPARE(rpm.value("title").toString(), QStringLiteral("Engine RPM"));
        QCOMPARE(rpm.value("shortTitle").toString(), QStringLiteral("RPM"));
        QCOMPARE(rpm.value("decimals").toInt(), 0);
        QCOMPARE(rpm.value("source").toString(), QStringLiteral("obd"));
        QCOMPARE(catalog.info("PITCH").value("source").toString(), QStringLiteral("imu"));
        QCOMPARE(catalog.info("COOLANT_TEMP").value("unit").toString(), QString::fromUtf8("°C"));
        QVERIFY(catalog.info("NOPE").isEmpty());
        QVERIFY(catalog.contains("BARO_TEMP"));
        QVERIFY(!catalog.contains("NOPE"));
    }

    void matchesWhatQmlShows()
    {
        QQmlEngine engine;
        engine.addImportPath(kFrontend);
        ParameterCatalog catalog;
        engine.rootContext()->setContextProperty("parameterCatalog", &catalog);
        QQmlComponent comp(&engine);
        comp.setData(QString::fromLatin1(kQml).arg(QUrl::fromLocalFile(kFrontend).toString()).toUtf8(), QUrl());
        std::unique_ptr<QObject> root(comp.create());
        QVERIFY2(root, qPrintable(comp.errorString()));
        QObject *model = root->property("model").value<QObject *>();
        QVERIFY(model);

        const QVariantList qml = model->property("allParameters").toList();
        const auto &cpp = ParameterCatalog::all();
        QCOMPARE(qml.size(), cpp.size());
        const QVariantMap shortTitles = model->property("shortTitles").toMap();
        for (int i = 0; i < cpp.size(); ++i) {
            const QVariantMap q = qml.at(i).toMap();
            const auto &p = cpp.at(i);
            const QByteArray where = p.id.toUtf8();
            QCOMPARE(q.value("id").toString(), p.id);
            QVERIFY2(q.value("title").toString() == p.title, where.constData());
            QVERIFY2(q.value("unit").toString() == p.unit, where.constData());
            QVERIFY2(q.value("min").toDouble() == p.min, where.constData());
            QVERIFY2(q.value("max").toDouble() == p.max, where.constData());
            QVERIFY2(q.value("kind").toString() == p.kind, where.constData());
            QVERIFY2(shortTitles.value(p.id).toString() == p.shortTitle, where.constData());

            QVariant decimals;
            QVERIFY(QMetaObject::invokeMethod(model, "decimalsFor", Q_RETURN_ARG(QVariant, decimals),
                                              Q_ARG(QVariant, p.id)));
            QVERIFY2(decimals.toInt() == p.decimals, where.constData());
        }
        QCOMPARE(model->property("sensorParameters").toStringList(), ParameterCatalog::imuIds());
    }
};

QTEST_MAIN(TstParameterCatalog)
#include "tst_parametercatalog.moc"
