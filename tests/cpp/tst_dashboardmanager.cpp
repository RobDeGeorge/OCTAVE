// Dashboard specs: one set of rules (DashboardManager::normalizeSpec) on
// import, save and load: known schema-1 keys, cells inside the grid and never
// overlapping, scalar props, a file-safe id on save. Bad imports are refused
// with a reason, export / import round-trips without overwriting anything,
// and the shipped presets already satisfy every rule. The grid placement the
// editor calls (src/util/dashboardgrid.h) is pinned at the end.

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QRegularExpression>
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
        m_dm->setWidgetCatalogFile(QStringLiteral(OCTAVE_SOURCE_DIR "/frontend/dashboards/widget-catalog.json"));
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
                {"type": "BarGauge", "paramId": "RPM", "col": 9, "row": 5, "colSpan": 50, "rowSpan": 1,
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
        // Into a 4x2 grid: too wide for it, so as wide as the grid
        QCOMPARE(bar.value("col").toInt(), 0);
        QCOMPARE(bar.value("row").toInt(), 1);
        QCOMPARE(bar.value("colSpan").toInt(), 4);
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

    void strayGridFileIsSkipped()
    {
        // A grid.json from outside the app (hand edit, synced config, an
        // import from before the reservation) must not load: OBDMenu always
        // resolves "grid" to Parameter Cards, so it would be a card that can
        // never be shown, edited or deleted
        QFile f(m_tmp->path() + "/dashboards/grid.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"json({"schema": 1, "id": "grid", "label": "Stray", "cells": []})json");
        f.close();
        m_dm->refresh();
        for (const QVariant &v : m_dm->dashboards())
            QVERIFY(v.toMap().value("id").toString() != QLatin1String("grid"));
    }

    // ── one set of rules on save and load ───────────────────────────────

    void presetsAreAlreadyNormal()
    {
        // Every shipped dashboard renders exactly as written
        const QDir presets(QStringLiteral(OCTAVE_SOURCE_DIR "/frontend/dashboards/presets"));
        const QStringList files = presets.entryList({"*.json"}, QDir::Files);
        QVERIFY(files.size() >= 5);
        for (const QString &name : files) {
            QFile f(presets.filePath(name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            QJsonObject raw = QJsonDocument::fromJson(f.readAll()).object();
            raw.insert("schema", 1);
            const QJsonObject loaded =
                QJsonObject::fromVariantMap(m_dm->loadDashboard(raw.value("id").toString()).toMap());
            QVERIFY2(loaded == raw, qPrintable(name));
        }
    }

    void widgetCatalogIsData()
    {
        const QVariantMap catalog = m_dm->widgetCatalog();
        QCOMPARE(catalog.value("schema").toInt(), 1);
        QVERIFY(!catalog.contains("_comment"));
        QStringList types;
        for (const QVariant &w : catalog.value("widgets").toList()) {
            const QVariantMap m = w.toMap();
            types << m.value("type").toString();
            for (const char *key : {"displayName", "glyph", "defaultColSpan", "defaultRowSpan", "supportedKinds",
                                    "labelProps", "rangeProps", "colorProps", "editableProps"})
                QVERIFY2(m.contains(key), qPrintable(m.value("type").toString() + ": " + key));
            QVERIFY(!m.contains("url"));   // QML's business
        }
        QCOMPARE(types.first(), QStringLiteral("CircularGauge"));   // palette order
        QVERIFY(types.contains("MediaControlsWidget"));
        QCOMPARE(catalog.value("colorSwatches").toList().size(), 8);

        // Every type has a QML url and every url a catalog entry
        QFile qml(QStringLiteral(OCTAVE_SOURCE_DIR "/frontend/dashboards/WidgetCatalog.qml"));
        QVERIFY(qml.open(QIODevice::ReadOnly));
        const QString source = QString::fromUtf8(qml.readAll());
        QRegularExpression urlEntry(QStringLiteral("^\\s*\"(\\w+)\":\\s*Qt\\.resolvedUrl"), QRegularExpression::MultilineOption);
        QStringList withUrl;
        for (auto it = urlEntry.globalMatch(source); it.hasNext();)
            withUrl << it.next().captured(1);
        QCOMPARE(withUrl, types);

        // A missing file is an empty catalog, not a crash
        DashboardManager none;
        none.setWidgetCatalogFile(m_tmp->path() + "/nope.json");
        QVERIFY(none.widgetCatalog().isEmpty());
    }

    void editablePropsFollowTheirKind()
    {
        const QVariantMap spec = saveAndLoad(R"json({"gridColumns": 12, "gridRows": 6, "cells": [
            {"type": "CircularGauge", "col": 0, "row": 0, "colSpan": 4, "rowSpan": 4, "props": {
                "showNeedle": "yes", "showTicks": false, "decimals": 2.6, "majorTickCount": 99,
                "sweepAngle": 10, "redlineStart": 5500, "title": "Boost", "min": -5, "fillColor": "red"}},
            {"type": "BarGauge", "col": 4, "row": 0, "colSpan": 3, "rowSpan": 2, "props": {
                "orientation": "diagonal", "showValue": 1}},
            {"type": "BarGauge", "col": 7, "row": 0, "colSpan": 3, "rowSpan": 2, "props": {"orientation": "vertical"}},
            {"type": "WarningLight", "col": 0, "row": 4, "colSpan": 2, "rowSpan": 2, "props": {"label": 7, "pulse": true}},
            {"type": "FutureGauge", "col": 4, "row": 4, "colSpan": 2, "rowSpan": 2, "props": {"decimals": "x"}}]})json");
        const QVariantList cells = spec.value("cells").toList();
        QCOMPARE(cells.size(), 5);
        const QVariantMap circ = cells.at(0).toMap().value("props").toMap();
        QVERIFY(!circ.contains("showNeedle"));                       // not a bool
        QCOMPARE(circ.value("showTicks"), QVariant(false));
        QCOMPARE(circ.value("decimals").toInt(), 3);                 // int: rounded...
        QCOMPARE(circ.value("decimals").toDouble(), 3.0);
        QCOMPARE(circ.value("majorTickCount").toInt(), 20);          // ...and clamped
        QCOMPARE(circ.value("sweepAngle").toDouble(), 30.0);         // real: clamped
        QCOMPARE(circ.value("redlineStart").toDouble(), 5500.0);     // no bounds
        QCOMPARE(circ.value("title").toString(), QStringLiteral("Boost"));   // not curated: as before
        QCOMPARE(circ.value("min").toDouble(), -5.0);
        QCOMPARE(circ.value("fillColor").toString(), QStringLiteral("red"));
        const QVariantMap bar = cells.at(1).toMap().value("props").toMap();
        QVERIFY(!bar.contains("orientation"));                       // not an option
        QVERIFY(!bar.contains("showValue"));                         // a number is not a bool
        QCOMPARE(cells.at(2).toMap().value("props").toMap().value("orientation").toString(), QStringLiteral("vertical"));
        const QVariantMap light = cells.at(3).toMap().value("props").toMap();
        QVERIFY(!light.contains("label"));                           // a string prop needs a string
        QCOMPARE(light.value("pulse"), QVariant(true));
        // A type this build doesn't know keeps its props (a newer build may)
        QCOMPARE(cells.at(4).toMap().value("props").toMap().value("decimals").toString(), QStringLiteral("x"));
    }

    void offGridCellsMoveBeforeShrinking()
    {
        // A drag keeps the widget's size: so does a hand-edited file
        const QVariantMap spec = saveAndLoad(R"json({"gridColumns": 12, "gridRows": 6, "cells": [
            {"type": "CircularGauge", "col": 9, "row": 5, "colSpan": 6, "rowSpan": 2},
            {"type": "BarGauge", "col": -4, "row": 0, "colSpan": 30, "rowSpan": 1}]})json");
        const QVariantList cells = spec.value("cells").toList();
        QCOMPARE(cells.size(), 2);
        QCOMPARE(rect(cells.at(0)), (QList<int>{6, 4, 6, 2}));
        QCOMPARE(rect(cells.at(1)), (QList<int>{0, 0, 12, 1}));
    }

    void overlapsShrinkThenDrop()
    {
        // File order: the earlier cell wins; a later one shrinks toward 1x1
        // (wider side first) and is dropped only if its corner is taken
        const QVariantMap spec = saveAndLoad(R"json({"gridColumns": 4, "gridRows": 4, "cells": [
            {"type": "A", "col": 2, "row": 0, "colSpan": 2, "rowSpan": 2},
            {"type": "B", "col": 0, "row": 0, "colSpan": 4, "rowSpan": 1},
            {"type": "C", "col": 3, "row": 1, "colSpan": 1, "rowSpan": 1},
            {"type": "D", "col": 0, "row": 2, "colSpan": 2, "rowSpan": 2}]})json");
        const QVariantList cells = spec.value("cells").toList();
        QCOMPARE(cells.size(), 3);
        QCOMPARE(cells.at(1).toMap().value("type").toString(), QStringLiteral("B"));
        QCOMPARE(rect(cells.at(1)), (QList<int>{0, 0, 2, 1}));
        QCOMPARE(cells.at(2).toMap().value("type").toString(), QStringLiteral("D"));   // C dropped
    }

    void gridAndGeometryDefaults()
    {
        const QVariantMap spec = saveAndLoad(R"json({"gridColumns": 0, "gridRows": "8",
            "margins": null, "spacing": 500, "cells": []})json");
        QCOMPARE(spec.value("gridColumns").toInt(), 12);   // not 1
        QCOMPARE(spec.value("gridRows").toInt(), 6);       // a string isn't a number
        QVERIFY(!spec.contains("margins"));                // null = the renderer's default
        QCOMPARE(spec.value("spacing").toInt(), 200);
    }

    void saveKeepsUnknownTypesAndPidsDropsNestedProps()
    {
        // A newer build's widget or PID survives an older build's save
        const QVariantMap spec = saveAndLoad(R"json({"cells": [
            {"type": "FutureGauge", "paramId": "FUTURE_PID", "props": {"a": {"b": 1}, "c": [1], "d": "NaN"}}]})json");
        const QVariantMap cell = spec.value("cells").toList().value(0).toMap();
        QCOMPARE(cell.value("type").toString(), QStringLiteral("FutureGauge"));
        QCOMPARE(cell.value("paramId").toString(), QStringLiteral("FUTURE_PID"));
        QCOMPARE(cell.value("props").toMap(), (QVariantMap{{"d", "NaN"}}));
    }

    void saveWithoutAnIdMakesOne()
    {
        // A client's new dashboard in one call: the editor's own id rule
        QVariantMap spec{{"label", "Trail Rig"}, {"cells", QVariantList{}}};
        QCOMPARE(m_dm->saveDashboard(spec), QStringLiteral("trail-rig"));
        QCOMPARE(m_dm->saveDashboard(spec), QStringLiteral("trail-rig-2"));   // never overwrites
        QCOMPARE(m_dm->loadDashboard("trail-rig-2").toMap().value("label").toString(), QStringLiteral("Trail Rig"));
        spec.insert("label", "Minimal");
        QCOMPARE(m_dm->saveDashboard(spec), QStringLiteral("minimal-2"));     // not the built-in
        spec.insert("id", "");
        spec.insert("label", "  ");
        QCOMPARE(m_dm->saveDashboard(spec), QString());                       // still needs a name
    }

    void exportRecordsItsPath()
    {
        QVERIFY(m_dm->lastExportPath().isEmpty());
        QSignalSpy changed(m_dm.get(), &DashboardManager::lastExportPathChanged);
        const QString path = m_dm->exportDashboard("minimal");
        QVERIFY(!path.isEmpty());
        QCOMPARE(m_dm->lastExportPath(), path);   // what GET /export/dashboard serves
        QCOMPARE(changed.size(), 1);
        QVERIFY(m_dm->exportDashboard("nope").isEmpty());
        QCOMPARE(m_dm->lastExportPath(), path);   // a failed export leaves it
    }

    void failuresSayWhy()
    {
        QSignalSpy changed(m_dm.get(), &DashboardManager::lastErrorChanged);
        QVERIFY(m_dm->loadDashboard("no-such").toMap().isEmpty());   // the return as before
        QVERIFY(m_dm->lastError().contains("no-such"));
        QCOMPARE(m_dm->saveDashboard({{"id", "minimal"}, {"label", "M"}}), QString());
        QVERIFY(m_dm->lastError().contains("built-in"));
        QCOMPARE(m_dm->saveDashboard({{"id", "x"}, {"label", ""}}), QString());
        QCOMPARE(m_dm->lastError(), QStringLiteral("Dashboard has no name"));
        QVERIFY(!m_dm->loadDashboard("minimal").toMap().isEmpty());
        QCOMPARE(m_dm->lastError(), QString());   // cleared by one that worked
        QVERIFY(changed.size() >= 4);
    }

    void saveRefusesAPathAsId()
    {
        for (const char *id : {"../escape", "a/b", ".hidden", "x y"})
            QCOMPARE(m_dm->saveDashboard({{"id", id}, {"label", "X"}, {"cells", QVariantList{}}}), QString());
        QVERIFY(!QFile::exists(m_tmp->path() + "/escape.json"));
        QCOMPARE(m_dm->saveDashboard({{"id", "Old_Name-2"}, {"label", "X"}}), QStringLiteral("Old_Name-2"));
        QCOMPARE(m_dm->saveDashboard({{"id", "nolabel"}, {"label", "   "}}), QString());
    }

    void loadNormalizesHandEditedFiles()
    {
        writeUserFile("hand", R"json({"id": "hand", "label": "Hand", "gridColumns": 4, "gridRows": 2,
            "cells": [{"type": "BarGauge", "col": 3, "row": 0, "colSpan": 3, "rowSpan": 1, "x": 1}]})json");
        const QVariantMap cell = m_dm->loadDashboard("hand").toMap().value("cells").toList().value(0).toMap();
        QCOMPARE(rect(cell), (QList<int>{1, 0, 3, 1}));
        QVERIFY(!cell.contains("x"));

        // Over the cap: the first 256 show (import and save refuse it)
        QString many = QStringLiteral(R"({"id": "many", "label": "Many", "gridColumns": 48, "gridRows": 48, "cells": [)");
        for (int i = 0; i < 300; ++i)
            many += QStringLiteral(R"(%1{"type": "X", "col": %2, "row": %3})").arg(i ? "," : "").arg(i % 48).arg(i / 48);
        many += "]}";
        writeUserFile("many", many.toUtf8().constData());
        QCOMPARE(m_dm->loadDashboard("many").toMap().value("cells").toList().size(), 256);

        // No cell list: an empty dashboard, not a crash in the editor
        writeUserFile("empty", R"json({"id": "empty", "label": "Empty"})json");
        QCOMPARE(m_dm->loadDashboard("empty").toMap().value("cells").toList(), QVariantList{});
    }

    void newerSchemaPassesThroughLoadAndIsNotSaved()
    {
        // The renderer shows "needs a newer OCTAVE" for it; saving it here
        // would strip what this build doesn't know
        writeUserFile("future", R"json({"schema": 2, "id": "future", "label": "Future",
            "cells": [{"type": "X", "col": 99, "wild": true}]})json");
        const QVariantMap spec = m_dm->loadDashboard("future").toMap();
        QCOMPARE(spec.value("schema").toInt(), 2);
        QCOMPARE(spec.value("cells").toList().value(0).toMap().value("col").toInt(), 99);
        QCOMPARE(m_dm->saveDashboard(spec), QString());
    }

    void normalizedSpecForDrafts()
    {
        // The editor's rescued draft has no id: it stays without one
        const QVariantMap spec = m_dm->normalizedSpec({{"gridColumns", 4}, {"gridRows", 2},
            {"cells", QVariantList{QVariantMap{{"type", "BarGauge"}, {"col", 8}}}}});
        QVERIFY(!spec.contains("id"));
        QCOMPARE(rect(spec.value("cells").toList().value(0)), (QList<int>{3, 0, 1, 1}));
    }

    // ── grid placement (the editor's tap / drag / resize) ───────────────

    void gridCanPlace()
    {
        const QVariantList cells{QVariantMap{{"col", 2}, {"row", 0}, {"colSpan", 2}, {"rowSpan", 2}}};
        QVERIFY(m_dm->canPlace(cells, 4, 4, 0, 0, 2, 2, -1));
        QVERIFY(!m_dm->canPlace(cells, 4, 4, 1, 1, 2, 1, -1));   // covers (2,1)
        QVERIFY(m_dm->canPlace(cells, 4, 4, 1, 1, 2, 1, 0));     // ...unless that's the one moving
        QVERIFY(!m_dm->canPlace(cells, 4, 4, 3, 2, 2, 1, -1));   // off the right edge
        QVERIFY(!m_dm->canPlace(cells, 4, 4, -1, 0, 1, 1, -1));
        QVERIFY(!m_dm->canPlace(cells, 4, 4, 0, 0, 0, 1, -1));   // no zero spans
        // A cell with no position keys sits at (0,0), 1x1
        QVERIFY(!m_dm->canPlace({QVariantMap{}}, 4, 4, 0, 0, 1, 1, -1));
    }

    void gridFitAtShrinksWiderSideFirst()
    {
        const QVariantList cells{QVariantMap{{"col", 2}, {"row", 0}, {"colSpan", 2}, {"rowSpan", 4}}};
        // Wanted 4x3 at (0,0): cut to the free 2 columns, height kept
        QCOMPARE(m_dm->fitAt(cells, 4, 4, 0, 0, 4, 3),
                 (QVariantMap{{"col", 0}, {"row", 0}, {"colSpan", 2}, {"rowSpan", 3}}));
        // Off the grid edge: cut to it
        QCOMPARE(m_dm->fitAt({}, 4, 4, 3, 3, 6, 6),
                 (QVariantMap{{"col", 3}, {"row", 3}, {"colSpan", 1}, {"rowSpan", 1}}));
    }

private:
    // Saved under a fresh id, then read back
    QVariantMap saveAndLoad(const char *json)
    {
        QVariantMap spec = QJsonDocument::fromJson(json).object().toVariantMap();
        spec.insert("id", QStringLiteral("t%1").arg(++m_n));
        spec.insert("label", QStringLiteral("T"));
        const QString id = m_dm->saveDashboard(spec);
        if (id.isEmpty())
            return {};
        return m_dm->loadDashboard(id).toMap();
    }

    void writeUserFile(const QString &id, const char *json)
    {
        QFile f(m_tmp->path() + "/dashboards/" + id + ".json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(json);
        f.close();
        m_dm->refresh();
    }

    static QList<int> rect(const QVariant &cell)
    {
        const QVariantMap c = cell.toMap();
        return {c.value("col").toInt(), c.value("row").toInt(), c.value("colSpan").toInt(),
                c.value("rowSpan").toInt()};
    }

    int m_n = 0;
    std::unique_ptr<QTemporaryDir> m_tmp;
    std::unique_ptr<DashboardManager> m_dm;
};

QTEST_MAIN(TstDashboardManager)
#include "tst_dashboardmanager.moc"
