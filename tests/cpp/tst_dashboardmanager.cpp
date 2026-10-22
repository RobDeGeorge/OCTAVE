// Dashboard sharing: imports are sanitized to the known schema-1 keys and
// clamped into the grid, bad input is refused with a reason, and export /
// import round-trips without overwriting anything.

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "managers/dashboardmanager.h"

class TstDashboardManager : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        // Keep the export out of the real ~/Downloads
        qputenv("HOME", m_tmp->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp->path() + "/config").toUtf8());
        qputenv("XDG_DOWNLOAD_DIR", (m_tmp->path() + "/dl").toUtf8());
        m_dm = std::make_unique<DashboardManager>();
        m_dm->setPresetsDir(QStringLiteral(OCTAVE_SOURCE_DIR "/frontend/dashboards/presets"));
        m_dm->setUserDir(m_tmp->path() + "/dashboards");
    }

    void cleanup()
    {
        m_dm.reset();
        m_tmp.reset();
    }

    void sanitizeKeepsKnownKeysAndClamps()
    {
        const QString raw = R"json({
            "schema": 1, "id": "../../evil", "label": "  My   Dash  ", "source": "Evil.qml",
            "gridColumns": 4, "gridRows": 2, "extra": {"x": 1},
            "cells": [
                {"type": "BarGauge", "paramId": "RPM", "col": 9, "row": -3, "colSpan": 50, "rowSpan": 1,
                 "props": {"warnAbove": 6000, "title": "Revs", "nested": {"a": 1}, "list": [1]},
                 "onClicked": "Qt.quit()"},
                "not-a-cell",
                {"type": "", "col": 0},
                {"type": "DigitalReadout", "col": "2", "row": true}
            ]
        })json";
        const QString id = m_dm->importDashboardFromText(raw);
        QVERIFY2(!id.isEmpty(), qPrintable(m_dm->lastShareError()));
        QVERIFY(id != QLatin1String("../../evil"));
        QVERIFY(!id.contains(QLatin1Char('/')));
        QCOMPARE(m_dm->lastShareError(), QString());

        const QVariantMap spec = m_dm->loadDashboard(id).toMap();
        const QStringList keys = spec.keys();
        const QSet<QString> got(keys.cbegin(), keys.cend());
        QCOMPARE(got, (QSet<QString>{"id", "schema", "label", "gridColumns", "gridRows", "cells"}));
        QCOMPARE(spec.value("label").toString(), QStringLiteral("My Dash"));

        const QVariantList cells = spec.value("cells").toList();
        QCOMPARE(cells.size(), 2);
        const QVariantMap bar = cells.at(0).toMap();
        const QStringList barKeys = bar.keys();
        QCOMPARE(QSet<QString>(barKeys.cbegin(), barKeys.cend()),
                 (QSet<QString>{"type", "paramId", "col", "row", "colSpan", "rowSpan", "props"}));
        // Clamped into a 4x2 grid
        QCOMPARE(bar.value("col").toInt(), 3);
        QCOMPARE(bar.value("row").toInt(), 0);
        QCOMPARE(bar.value("colSpan").toInt(), 1);
        // Scalars only
        QCOMPARE(bar.value("props").toMap(), (QVariantMap{{"warnAbove", 6000}, {"title", "Revs"}}));
        // Strings and bools aren't numbers
        const QVariantMap digital = cells.at(1).toMap();
        QCOMPARE(digital.value("col").toInt(), 0);
        QCOMPARE(digital.value("row").toInt(), 0);
    }

    void sanitizeRejects_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("reason");
        QTest::newRow("newer schema") << R"json({"schema": 2, "cells": []})json" << "newer version";
        QTest::newRow("bad schema") << R"json({"schema": 0, "cells": []})json" << "bad schema";
        QTest::newRow("no cells") << R"json({"label": "x"})json" << "no cells";
        QString many = QStringLiteral("{\"cells\": [");
        for (int i = 0; i < 257; ++i)
            many += i ? ",{}" : "{}";
        many += "]}";
        QTest::newRow("too many") << many << "too many";
        QTest::newRow("array") << "[1, 2]" << "invalid JSON";
    }

    void sanitizeRejects()
    {
        QFETCH(QString, raw);
        QFETCH(QString, reason);
        QCOMPARE(m_dm->importDashboardFromText(raw), QString());
        QVERIFY2(m_dm->lastShareError().contains(reason), qPrintable(m_dm->lastShareError()));
    }

    void exportImportRoundTrip()
    {
        const QString path = m_dm->exportDashboard("minimal");
        QVERIFY2(!path.isEmpty(), qPrintable(m_dm->lastShareError()));
        QVERIFY2(path.startsWith(m_tmp->path()), qPrintable(path));
        QVERIFY(path.endsWith("/OCTAVE-dashboards/minimal.json"));

        const QString newId = m_dm->importDashboard(path);
        QVERIFY(!newId.isEmpty());
        QVERIFY(newId != QLatin1String("minimal"));   // never overwrites; built-in id regenerated
        QVERIFY(!m_dm->isBuiltIn(newId));
        QCOMPARE(m_dm->loadDashboard(newId).toMap().value("cells"),
                 m_dm->loadDashboard("minimal").toMap().value("cells"));

        // Importing the same file again gets yet another id
        const QString again = m_dm->importDashboard(path);
        QVERIFY(!again.isEmpty());
        QVERIFY(again != newId);
        QCOMPARE(m_dm->lastShareError(), QString());
    }

    void importFailureSetsError()
    {
        QCOMPARE(m_dm->importDashboardFromText("{not json"), QString());
        QVERIFY(m_dm->lastShareError().contains("invalid JSON"));
        QCOMPARE(m_dm->importDashboard(m_tmp->path() + "/missing.json"), QString());
        QCOMPARE(m_dm->lastShareError(), QStringLiteral("Could not open the file"));
    }

    // ── id generation (the editor asks uniqueIdForLabel before saving) ──

    void uniqueIdSlugifiesAndCounts()
    {
        QCOMPARE(m_dm->uniqueIdForLabel("  Trail Rig! "), QStringLiteral("trail-rig"));
        QCOMPARE(m_dm->uniqueIdForLabel("***"), QStringLiteral("dashboard"));
        QCOMPARE(m_dm->uniqueIdForLabel("Minimal"), QStringLiteral("minimal-2"));   // built-in taken
        QVERIFY(!m_dm->saveDashboard({{"id", "minimal-2"}, {"label", "Minimal"}}).isEmpty());
        QCOMPARE(m_dm->uniqueIdForLabel("Minimal"), QStringLiteral("minimal-3"));
    }

    void uniqueIdSkipsHiddenBuiltIns()
    {
        // Delete built-in Sport, then make a new "Sport": its id must not be
        // the hidden preset's, or saveDashboard refuses it
        QVERIFY(m_dm->deleteDashboard("sport"));
        QVERIFY(m_dm->loadDashboard("sport").toMap().isEmpty());
        const QString id = m_dm->uniqueIdForLabel("Sport");
        QCOMPARE(id, QStringLiteral("sport-2"));
        QCOMPARE(m_dm->saveDashboard({{"id", id}, {"label", "Sport"}, {"cells", QVariantList{}}}), id);
    }

    void parameterCardsIdIsReserved()
    {
        // "grid" is OBDMenu's Parameter Cards entry: a dashboard with that id
        // could never be selected
        QCOMPARE(m_dm->uniqueIdForLabel("Grid"), QStringLiteral("grid-2"));
        QCOMPARE(m_dm->saveDashboard({{"id", "grid"}, {"label", "Grid"}}), QString());

        const QString copy = m_dm->duplicateDashboard("minimal", "Grid");
        QCOMPARE(copy, QStringLiteral("grid-2"));

        const QString imported = m_dm->importDashboardFromText(
            R"json({"schema": 1, "label": "GRID", "cells": []})json");
        QVERIFY2(!imported.isEmpty(), qPrintable(m_dm->lastShareError()));
        QVERIFY(imported != QLatin1String("grid"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    std::unique_ptr<DashboardManager> m_dm;
};

QTEST_MAIN(TstDashboardManager)
#include "tst_dashboardmanager.moc"
