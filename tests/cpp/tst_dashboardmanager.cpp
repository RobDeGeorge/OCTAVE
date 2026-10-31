// Dashboard specs: one set of rules (DashboardManager::normalizeSpec) on
// import, save and load: known schema-1 keys, cells inside the grid and never
// overlapping, scalar props, a file-safe id on save. Bad imports are refused
// with a reason, export / import round-trips without overwriting anything,
// and the shipped presets already satisfy every rule. The grid placement the
// editor calls (src/util/dashboardgrid.h) is pinned at the end.

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QTest>

#include <memory>

#include "managers/dashboardmanager.h"
#include "managers/parametercatalog.h"

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
        m_dm->setTemplatesDir(QStringLiteral(OCTAVE_SOURCE_DIR "/frontend/dashboards/templates"));
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
        QTest::newRow("newer schema") << R"json({"schema": 3, "cells": []})json" << "newer version";
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
            for (const char *key : {"displayName", "glyph", "defaultColSpan", "defaultRowSpan", "minSize", "aspect",
                                    "supportedKinds", "labelProps", "rangeProps", "colorProps", "editableProps"})
                QVERIFY2(m.contains(key), qPrintable(m.value("type").toString() + ": " + key));
            QVERIFY(m.value("minSize").toMap().value("w").toDouble() > 0);
            QVERIFY(m.value("minSize").toMap().value("h").toDouble() > 0);
            QVERIFY(m.value("aspect").toDouble() > 0);
            QVERIFY(!m.contains("url"));   // QML's business
            // The gallery's category and line; each option's settings section
            QVERIFY(catalog.value("categories").toStringList().contains(m.value("category").toString()));
            QVERIFY(!m.value("description").toString().isEmpty());
            for (const QVariant &p : m.value("editableProps").toList())
                QVERIFY2((QStringList{"look", "range", "advanced"}).contains(p.toMap().value("section").toString()),
                         qPrintable(m.value("type").toString() + "." + p.toMap().value("key").toString()));
        }
        // Data-first add proposes a widget the catalog has
        const QVariantMap suggestions = catalog.value("suggestions").toMap();
        for (const char *group : {"byId", "byKind"})
            for (const QVariant &type : suggestions.value(group).toMap())
                QVERIFY2(types.contains(type.toString()), qPrintable(type.toString()));
        for (const char *kind : {"percentage", "temperature", "voltage", "pressure", "bidirectional", "numeric"})
            QVERIFY(suggestions.value("byKind").toMap().contains(kind));
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
        writeUserFile("future", R"json({"schema": 3, "id": "future", "label": "Future",
            "cells": [{"type": "X", "col": 99, "wild": true}]})json");
        const QVariantMap spec = m_dm->loadDashboard("future").toMap();
        QCOMPARE(spec.value("schema").toInt(), 3);
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

    // ── arrange: move / resize with swap and push ──

    static QVariantMap cell(int col, int row, int cs, int rs, const char *tag)
    {
        return {{"col", col}, {"row", row}, {"colSpan", cs}, {"rowSpan", rs}, {"tag", tag}};
    }
    static QString at(const QVariantMap &c)
    {
        return QStringLiteral("%1,%2 %3x%4").arg(c.value("col").toInt()).arg(c.value("row").toInt())
            .arg(c.value("colSpan").toInt()).arg(c.value("rowSpan").toInt());
    }

    void arrangeMovesIntoFreeSpace()
    {
        const QVariantList cells{cell(0, 0, 2, 2, "a"), cell(2, 0, 2, 2, "b")};
        const QVariantMap r = m_dm->arrange(cells, 6, 4, 0, 0, 2, 2, 2);
        QVERIFY(r.value("ok").toBool());
        QVERIFY(r.value("moved").toList().isEmpty());
        QCOMPARE(at(r.value("cells").toList().at(0).toMap()), QStringLiteral("0,2 2x2"));
        QCOMPARE(r.value("cells").toList().at(1).toMap(), cells.at(1).toMap());   // untouched
        QCOMPARE(r.value("cells").toList().at(0).toMap().value("tag").toString(), QStringLiteral("a"));
    }

    void arrangeSwapsEqualNeighbours()
    {
        // a | b | c on a full row: dropping a onto b swaps them, c stays
        const QVariantList cells{cell(0, 0, 2, 2, "a"), cell(2, 0, 2, 2, "b"), cell(4, 0, 2, 2, "c")};
        const QVariantMap r = m_dm->arrange(cells, 6, 2, 0, 2, 0, 2, 2);
        QVERIFY(r.value("ok").toBool());
        QVERIFY(r.value("swapped").toBool());
        const QVariantList out = r.value("cells").toList();
        QCOMPARE(at(out.at(0).toMap()), QStringLiteral("2,0 2x2"));
        QCOMPARE(at(out.at(1).toMap()), QStringLiteral("0,0 2x2"));
        QCOMPARE(out.at(2).toMap(), cells.at(2).toMap());
        QCOMPARE(r.value("moved").toList(), (QVariantList{1}));
    }

    void arrangeSwapsWhenTheOtherFitsTheGap()
    {
        // A 1x1 dropped on a 2x2 whose span fits where the 1x1 was: they swap
        const QVariantList cells{cell(0, 0, 1, 1, "small"), cell(2, 0, 2, 2, "big")};
        QVariantMap r = m_dm->arrange(cells, 6, 2, 0, 2, 0, 1, 1);
        QVERIFY(r.value("ok").toBool());
        QVERIFY(r.value("swapped").toBool());
        QCOMPARE(at(r.value("cells").toList().at(0).toMap()), QStringLiteral("2,0 1x1"));
        QCOMPARE(at(r.value("cells").toList().at(1).toMap()), QStringLiteral("0,0 2x2"));

        // ...and pushed instead when it doesn't (the 1x1 sat in the last
        // column, too narrow for a 2x2): the big one goes to the nearest
        // free 2x2
        const QVariantList edge{cell(5, 0, 1, 1, "small"), cell(2, 0, 2, 2, "big")};
        r = m_dm->arrange(edge, 6, 2, 0, 2, 0, 1, 1);
        QVERIFY(r.value("ok").toBool());
        QVERIFY(!r.value("swapped").toBool());
        QCOMPARE(at(r.value("cells").toList().at(0).toMap()), QStringLiteral("2,0 1x1"));
        QCOMPARE(at(r.value("cells").toList().at(1).toMap()), QStringLiteral("3,0 2x2"));
    }

    void arrangePushesOnlyCoveredCells()
    {
        // Growing a to 4 wide covers b; d (not covered) never moves even
        // though moving it would make a tidier layout
        const QVariantList cells{cell(0, 0, 2, 1, "a"), cell(2, 0, 2, 1, "b"), cell(0, 2, 2, 1, "d")};
        const QVariantMap r = m_dm->arrange(cells, 4, 3, 0, 0, 0, 4, 1);
        QVERIFY(r.value("ok").toBool());
        const QVariantList out = r.value("cells").toList();
        QCOMPARE(at(out.at(0).toMap()), QStringLiteral("0,0 4x1"));
        QCOMPARE(at(out.at(1).toMap()), QStringLiteral("2,1 2x1"));   // straight down, nearest
        QCOMPARE(out.at(2).toMap(), cells.at(2).toMap());
        QCOMPARE(r.value("moved").toList(), (QVariantList{1}));
    }

    void arrangeRefusesWithAReason()
    {
        // A full 2x1 grid: making a 2 wide leaves nowhere for b
        const QVariantList cells{cell(0, 0, 1, 1, "a"), cell(1, 0, 1, 1, "b")};
        QVariantMap r = m_dm->arrange(cells, 2, 1, 0, 0, 0, 2, 1);
        QVERIFY(!r.value("ok").toBool());
        QVERIFY(r.value("reason").toString().contains("No room"));
        QCOMPARE(r.value("cells").toList(), cells);   // unchanged
        r = m_dm->arrange(cells, 2, 1, 0, 1, 0, 2, 1);
        QVERIFY(!r.value("ok").toBool());
        QVERIFY(r.value("reason").toString().contains("edge"));
        QVERIFY(!m_dm->arrange(cells, 2, 1, 5, 0, 0, 1, 1).value("ok").toBool());
    }

    void arrangeNeverOverlaps_data()
    {
        QTest::addColumn<int>("seed");
        for (int i = 0; i < 40; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }

    void arrangeNeverOverlaps()
    {
        // Random layouts and random targets: a result is either refused or
        // valid (inside the grid, no overlaps, every cell kept)
        QFETCH(int, seed);
        QRandomGenerator rng(seed);
        const int cols = 4 + rng.bounded(9), rows = 3 + rng.bounded(6);
        QVariantList cells;
        for (int tries = 0; tries < 60; ++tries) {
            const int cs = 1 + rng.bounded(3), rs = 1 + rng.bounded(3);
            const int c = rng.bounded(cols), r = rng.bounded(rows);
            if (m_dm->canPlace(cells, cols, rows, c, r, cs, rs, -1))
                cells.append(cell(c, r, cs, rs, "x"));
        }
        if (cells.isEmpty())
            return;
        for (int step = 0; step < 30; ++step) {
            const int i = rng.bounded(cells.size());
            const int cs = 1 + rng.bounded(4), rs = 1 + rng.bounded(4);
            const QVariantMap r = m_dm->arrange(cells, cols, rows, i, rng.bounded(cols), rng.bounded(rows), cs, rs);
            if (!r.value("ok").toBool())
                continue;
            const QVariantList out = r.value("cells").toList();
            QCOMPARE(out.size(), cells.size());
            for (int a = 0; a < out.size(); ++a)
                QVERIFY2(m_dm->canPlace(out, cols, rows, out.at(a).toMap().value("col").toInt(),
                                        out.at(a).toMap().value("row").toInt(),
                                        out.at(a).toMap().value("colSpan").toInt(),
                                        out.at(a).toMap().value("rowSpan").toInt(), a),
                         qPrintable(at(out.at(a).toMap())));
            cells = out;
        }
    }

    // ── templates ──

    void templatesLoadInOrderAndFit()
    {
        const QVariantList templates = m_dm->templates();
        QCOMPARE(templates.size(), 10);
        QCOMPARE(templates.first().toMap().value("id").toString(), QStringLiteral("hero-4"));
        QCOMPARE(templates.last().toMap().value("id").toString(), QStringLiteral("blank"));
        QStringList types;
        for (const QVariant &w : m_dm->widgetCatalog().value("widgets").toList())
            types << w.toMap().value("type").toString();
        for (const QVariant &t : templates) {
            const QVariantMap tpl = t.toMap();
            const QString id = tpl.value("id").toString();
            QVERIFY2(!tpl.value("description").toString().isEmpty(), qPrintable(id));
            const QVariantList cells = tpl.value("cells").toList();
            for (const QVariant &c : cells) {
                const QVariantMap cell = c.toMap();
                QVERIFY2(types.contains(cell.value("type").toString()), qPrintable(id));
                for (const QVariant &pid : cell.value("slot").toMap().value("prefer").toList())
                    QVERIFY2(ParameterCatalog::find(pid.toString()), qPrintable(id + ": " + pid.toString()));
            }
            // Nothing dropped or moved by the spec rules: the cells fit the grid as written
            const QVariantMap spec = m_dm->fromTemplate(id, {});
            QCOMPARE(spec.value("cells").toList().size(), cells.size());
            QVERIFY(!spec.contains("id"));
            for (int i = 0; i < cells.size(); ++i)
                QCOMPARE(at(spec.value("cells").toList().at(i).toMap()), at(cells.at(i).toMap()));
        }
    }

    void fromTemplateFillsSlots()
    {
        // No car known: each slot takes its first preferred PID nobody took
        QVariantMap spec = m_dm->fromTemplate("hero-4", {});
        QCOMPARE(spec.value("label").toString(), QStringLiteral("Hero + 4"));
        QStringList pids;
        for (const QVariant &c : spec.value("cells").toList()) {
            QVERIFY(!c.toMap().contains("slot"));
            pids << c.toMap().value("paramId").toString();
        }
        QCOMPARE(pids, (QStringList{"RPM", "SPEED", "COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE", "FUEL_LEVEL"}));

        // A car without RPM or a voltage: the tach falls back to SPEED, the
        // speed slot to nothing (no other numeric PID), voltage stays unbound
        spec = m_dm->fromTemplate("hero-4", {"SPEED", "COOLANT_TEMP", "ENGINE_LOAD", "INTAKE_TEMP"});
        pids.clear();
        for (const QVariant &c : spec.value("cells").toList())
            pids << c.toMap().value("paramId").toString();
        QCOMPARE(pids, (QStringList{"SPEED", "", "COOLANT_TEMP", "", "ENGINE_LOAD"}));

        // Self-binding widgets need nothing; an unknown template is empty
        spec = m_dm->fromTemplate("track", {"RPM"});
        QCOMPARE(spec.value("cells").toList().at(2).toMap().value("type").toString(), QStringLiteral("GForceGauge"));
        QVERIFY(m_dm->fromTemplate("nope", {}).isEmpty());
        QCOMPARE(m_dm->fromTemplate("blank", {}).value("cells").toList().size(), 0);
    }

    // ── nearest free spot, density ──

    void nearestFreeFindsTheClosestGap()
    {
        const QVariantList cells{cell(0, 0, 2, 2, "a"), cell(2, 0, 2, 2, "b")};
        // Free already: stays
        QVariantMap r = m_dm->nearestFree(cells, 6, 4, 4, 0, 2, 2, -1);
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(at(r), QStringLiteral("4,0 2x2"));
        // Taken: the closest gap of that size, the same row first
        r = m_dm->nearestFree(cells, 6, 4, 1, 0, 2, 2, -1);
        QCOMPARE(at(r), QStringLiteral("1,2 2x2"));
        // Its own cell doesn't block it
        r = m_dm->nearestFree(cells, 6, 4, 0, 0, 2, 2, 0);
        QCOMPARE(at(r), QStringLiteral("0,0 2x2"));
        // Nothing that big is free
        QVERIFY(!m_dm->nearestFree(cells, 6, 4, 0, 0, 6, 4, -1).value("ok").toBool());
        QVERIFY(!m_dm->nearestFree(cells, 6, 4, 0, 0, 7, 1, -1).value("ok").toBool());
    }

    void rescaleKeepsTheLayout()
    {
        // 12 x 8 halved to 6 x 4: everything exactly half
        const QVariantList cells{cell(0, 0, 6, 4, "a"), cell(6, 0, 6, 4, "b"), cell(0, 4, 4, 4, "c"),
                                 cell(4, 4, 4, 4, "d"), cell(8, 4, 4, 4, "e")};
        QVariantMap r = m_dm->rescale(cells, 12, 8, 6, 4);
        QVERIFY(r.value("ok").toBool());
        QVariantList out = r.value("cells").toList();
        QCOMPARE(at(out.at(0).toMap()), QStringLiteral("0,0 3x2"));
        QCOMPARE(at(out.at(1).toMap()), QStringLiteral("3,0 3x2"));
        QCOMPARE(at(out.at(2).toMap()), QStringLiteral("0,2 2x2"));
        QCOMPARE(at(out.at(3).toMap()), QStringLiteral("2,2 2x2"));
        QCOMPARE(at(out.at(4).toMap()), QStringLiteral("4,2 2x2"));
        QCOMPARE(out.at(4).toMap().value("tag").toString(), QStringLiteral("e"));   // the rest of the cell kept
        // ...and back up again
        r = m_dm->rescale(out, 6, 4, 12, 8);
        QCOMPARE(r.value("cells").toList(), cells);
    }

    void rescaleResolvesCollisions()
    {
        // Two 1-column cells side by side on 12 columns round onto the same
        // column of 4: one of them moves aside, nothing overlaps
        const QVariantList cells{cell(0, 0, 1, 1, "a"), cell(1, 0, 1, 1, "b")};
        const QVariantMap r = m_dm->rescale(cells, 12, 8, 4, 3);
        QVERIFY(r.value("ok").toBool());
        const QVariantList out = r.value("cells").toList();
        for (int i = 0; i < out.size(); ++i) {
            const QVariantMap c = out.at(i).toMap();
            QVERIFY2(m_dm->canPlace(out, 4, 3, c.value("col").toInt(), c.value("row").toInt(),
                                    c.value("colSpan").toInt(), c.value("rowSpan").toInt(), i),
                     qPrintable(at(c)));
        }
        // More widgets than cells: refused, with a reason
        QVariantList many;
        for (int i = 0; i < 13; ++i)
            many.append(cell(i % 12, i / 12, 1, 1, "x"));
        const QVariantMap no = m_dm->rescale(many, 12, 8, 4, 3);
        QVERIFY(!no.value("ok").toBool());
        QVERIFY(!no.value("reason").toString().isEmpty());
        QCOMPARE(no.value("cells").toList(), many);
    }

    // ── portrait ──

    void derivePortraitKeepsShapeAndOrder()
    {
        // Hero + 4 on 6 x 4: the dial on top keeps its round shape, the four
        // readouts follow two by two, on the turned 4 x 6 grid
        const QVariantList cells{cell(0, 0, 4, 4, "dial"), cell(4, 0, 2, 1, "a"), cell(4, 1, 2, 1, "b"),
                                 cell(4, 2, 2, 1, "c"), cell(4, 3, 2, 1, "d")};
        const QVariantMap p = m_dm->derivePortrait(cells, 6, 4);
        QCOMPARE(p.value("gridColumns").toInt(), 4);
        QCOMPARE(p.value("gridRows").toInt(), 6);
        const QVariantList places = p.value("cells").toList();
        QCOMPARE(places.size(), 5);
        QCOMPARE(at(places.at(0).toMap()), QStringLiteral("0,0 4x3"));
        QCOMPARE(at(places.at(1).toMap()), QStringLiteral("0,3 2x1"));
        QCOMPARE(at(places.at(2).toMap()), QStringLiteral("2,3 2x1"));
        QCOMPARE(at(places.at(3).toMap()), QStringLiteral("0,4 2x1"));
        QCOMPARE(at(places.at(4).toMap()), QStringLiteral("2,4 2x1"));
        // The same every time
        QCOMPARE(m_dm->derivePortrait(cells, 6, 4), p);
    }

    void derivePortraitAlwaysFits_data()
    {
        QTest::addColumn<int>("seed");
        for (int i = 0; i < 30; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }

    void derivePortraitAlwaysFits()
    {
        QFETCH(int, seed);
        QRandomGenerator rng(seed);
        const int cols = 4 + rng.bounded(9), rows = 3 + rng.bounded(6);
        QVariantList cells;
        for (int tries = 0; tries < 80; ++tries) {
            const int cs = 1 + rng.bounded(4), rs = 1 + rng.bounded(4);
            const int c = rng.bounded(cols), r = rng.bounded(rows);
            if (m_dm->canPlace(cells, cols, rows, c, r, cs, rs, -1))
                cells.append(cell(c, r, cs, rs, "x"));
        }
        const QVariantMap p = m_dm->derivePortrait(cells, cols, rows);
        const int pc = p.value("gridColumns").toInt(), pr = p.value("gridRows").toInt();
        QCOMPARE(pc, rows);
        QVERIFY(pr >= cols);
        const QVariantList places = p.value("cells").toList();
        QCOMPARE(places.size(), cells.size());
        for (int i = 0; i < places.size(); ++i) {
            const QVariantMap c = places.at(i).toMap();
            QVERIFY2(m_dm->canPlace(places, pc, pr, c.value("col").toInt(), c.value("row").toInt(),
                                    c.value("colSpan").toInt(), c.value("rowSpan").toInt(), i),
                     qPrintable(at(c)));
        }
    }

    void portraitLayoutIsSchema2()
    {
        // Without layouts a spec stays schema 1, readable by older builds
        QVariantMap plain = m_dm->normalizedSpec({{"gridColumns", 6}, {"gridRows", 4},
                                                  {"cells", QVariantList{cell(0, 0, 2, 2, "a")}}});
        QCOMPARE(plain.value("schema").toInt(), 1);
        QVERIFY(!plain.contains("layouts"));

        // With one: schema 2, a placement per kept cell. The second cell
        // sits on the first and is dropped; its placement goes with it.
        const QVariantList cells{QVariantMap{{"type", "DigitalReadout"}, {"col", 0}, {"row", 0}, {"colSpan", 2}, {"rowSpan", 2}},
                                 QVariantMap{{"type", "DigitalReadout"}, {"col", 0}, {"row", 0}, {"colSpan", 6}, {"rowSpan", 4}},
                                 QVariantMap{{"type", "ClockWidget"}, {"col", 2}, {"row", 0}, {"colSpan", 2}, {"rowSpan", 1}}};
        // (cell 1 shrinks to 1x1 at 0,0, which is taken: dropped)
        const QVariantList places{cell(0, 0, 4, 2, "p0"), cell(0, 2, 4, 2, "p1"), cell(0, 4, 4, 1, "p2")};
        QVariantMap spec = m_dm->normalizedSpec({{"schema", 2}, {"gridColumns", 6}, {"gridRows", 4}, {"cells", cells},
            {"layouts", QVariantMap{{"portrait", QVariantMap{{"gridColumns", 4}, {"gridRows", 6}, {"cells", places}}}}}});
        QCOMPARE(spec.value("schema").toInt(), 2);
        QCOMPARE(spec.value("cells").toList().size(), 2);
        QVariantList kept = spec.value("layouts").toMap().value("portrait").toMap().value("cells").toList();
        QCOMPARE(kept.size(), 2);
        QCOMPARE(at(kept.at(0).toMap()), QStringLiteral("0,0 4x2"));
        QCOMPARE(at(kept.at(1).toMap()), QStringLiteral("0,4 4x1"));

        // A missing or colliding placement goes to the nearest free spot
        spec = m_dm->normalizedSpec({{"gridColumns", 6}, {"gridRows", 4}, {"cells", QVariantList{cells.at(0), cells.at(2)}},
            {"layouts", QVariantMap{{"portrait", QVariantMap{{"cells", QVariantList{cell(0, 0, 4, 2, "p0")}}}}}}});
        kept = spec.value("layouts").toMap().value("portrait").toMap().value("cells").toList();
        QCOMPARE(kept.size(), 2);
        QCOMPARE(spec.value("layouts").toMap().value("portrait").toMap().value("gridColumns").toInt(), 4);   // default: turned
        QVERIFY(m_dm->canPlace(kept, 4, 6, kept.at(1).toMap().value("col").toInt(), kept.at(1).toMap().value("row").toInt(),
                               kept.at(1).toMap().value("colSpan").toInt(), kept.at(1).toMap().value("rowSpan").toInt(), 1));
    }

    void orientedSpecSwapsInThePortraitLayout()
    {
        const QVariantMap spec{{"schema", 2}, {"gridColumns", 6}, {"gridRows", 4},
            {"cells", QVariantList{QVariantMap{{"type", "ClockWidget"}, {"col", 0}, {"row", 0}, {"colSpan", 2}, {"rowSpan", 1},
                                               {"props", QVariantMap{{"showDate", true}}}}}},
            {"layouts", QVariantMap{{"portrait", QVariantMap{{"gridColumns", 4}, {"gridRows", 6},
                {"cells", QVariantList{cell(1, 2, 3, 2, "")}}}}}}};
        QCOMPARE(m_dm->orientedSpec(spec, false), spec);
        const QVariantMap p = m_dm->orientedSpec(spec, true);
        QCOMPARE(p.value("gridColumns").toInt(), 4);
        QCOMPARE(p.value("gridRows").toInt(), 6);
        QVERIFY(!p.contains("layouts"));
        const QVariantMap c = p.value("cells").toList().first().toMap();
        QCOMPARE(at(c), QStringLiteral("1,2 3x2"));
        QCOMPARE(c.value("props").toMap().value("showDate").toBool(), true);   // the cell's own options
        // No portrait layout: derived (the same rules as derivePortrait)
        QVariantMap noLayout = spec;
        noLayout.remove("layouts");
        const QVariantMap d = m_dm->orientedSpec(noLayout, true);
        QCOMPARE(d.value("gridColumns").toInt(), 4);
        QCOMPARE(at(d.value("cells").toList().first().toMap()),
                 at(m_dm->derivePortrait(noLayout.value("cells").toList(), 6, 4).value("cells").toList().first().toMap()));
    }

    void presetsHavePortraitLayouts()
    {
        for (const QString id : {"sport", "minimal", "fullgrid", "performance", "tj-wrangler"}) {
            const QVariantMap spec = m_dm->loadDashboard(id).toMap();
            QVERIFY2(spec.value("schema").toInt() == 2, qPrintable(id));
            const QVariantMap portrait = spec.value("layouts").toMap().value("portrait").toMap();
            QCOMPARE(portrait.value("cells").toList().size(), spec.value("cells").toList().size());
            QVERIFY(portrait.value("gridRows").toInt() >= portrait.value("gridColumns").toInt());
        }
    }

    // ── sizes ──

    void importReportSaysWhatChanged()
    {
        QSignalSpy spy(m_dm.get(), &DashboardManager::lastImportReportChanged);
        const QString raw = R"json({
            "schema": 1, "label": "Shared", "gridColumns": 4, "gridRows": 2,
            "cells": [
                {"type": "CircularGauge", "paramId": "RPM", "col": 0, "row": 0, "colSpan": 2, "rowSpan": 2,
                 "props": {"showNeedle": "yes", "nested": {"a": 1}}},
                {"type": "BarGauge", "paramId": "RPM", "col": 1, "row": 1, "colSpan": 1, "rowSpan": 1},
                {"type": "FancyNewGauge", "paramId": "BOOST", "col": 9, "row": 0, "colSpan": 1, "rowSpan": 1},
                {"type": "DigitalReadout", "paramId": "", "col": 2, "row": 1, "colSpan": 1, "rowSpan": 1}
            ]
        })json";
        const QString id = m_dm->importDashboardFromText(raw);
        QVERIFY2(!id.isEmpty(), qPrintable(m_dm->lastShareError()));
        QCOMPARE(spy.count(), 1);
        const QVariantMap r = m_dm->lastImportReport();
        QCOMPARE(r.value("id").toString(), id);
        QCOMPARE(r.value("label").toString(), QStringLiteral("Shared"));
        QCOMPARE(r.value("cells").toInt(), 3);
        QCOMPARE(r.value("droppedCells").toStringList(), QStringList{"BarGauge"});
        QCOMPARE(r.value("droppedProps").toInt(), 2);   // a bool as a string, a nested object
        QCOMPARE(r.value("movedCells").toInt(), 1);     // col 9 on a 4-wide grid
        QCOMPARE(r.value("unknownTypes").toStringList(), QStringList{"FancyNewGauge"});
        QCOMPARE(r.value("paramIds").toStringList(), (QStringList{"RPM", "BOOST"}));
        QCOMPARE(r.value("portraitDropped").toBool(), false);

        // A refused import clears it
        QCOMPARE(m_dm->importDashboardFromText("{not json"), QString());
        QVERIFY(!m_dm->lastImportReport().isEmpty());   // parse errors never reach importSpec
        QCOMPARE(m_dm->importDashboardFromText(R"({"schema": 99, "cells": []})"), QString());
        QVERIFY(m_dm->lastImportReport().isEmpty());
    }

    void optionalKeysAreCheckedAndKept()
    {
        QImage img(640, 360, QImage::Format_RGB32);
        img.fill(Qt::darkCyan);
        const QString thumb = m_dm->thumbnailFromImage(QVariant::fromValue(img));
        QVERIFY(thumb.startsWith("data:image/"));
        QVERIFY(thumb.size() < 96 * 1024);
        QCOMPARE(m_dm->thumbnailFromImage(QVariant(QStringLiteral("not an image"))), QString());

        QVariantMap spec{{"label", "Extras"}, {"cells", QVariantList{}}, {"thumbnail", thumb},
                         {"alertBanner", false},
                         {"showWhen", QVariantMap{{"paramId", "COOLANT_TEMP"}, {"above", 105}, {"junk", 1}}}};
        const QString id = m_dm->saveDashboard(spec);
        QVERIFY2(!id.isEmpty(), qPrintable(m_dm->lastError()));
        QVariantMap got = m_dm->loadDashboard(id).toMap();
        QCOMPARE(got.value("thumbnail").toString(), thumb);
        QCOMPARE(got.value("alertBanner"), QVariant(false));
        QCOMPARE(got.value("showWhen").toMap(), (QVariantMap{{"paramId", "COOLANT_TEMP"}, {"above", 105.0}}));
        QCOMPARE(got.value("schema").toInt(), 1);

        // Wrong shapes are dropped, not kept
        spec = {{"id", id}, {"label", "Extras"}, {"cells", QVariantList{}},
                {"thumbnail", "javascript:alert(1)"}, {"alertBanner", "no"},
                {"showWhen", QVariantMap{{"paramId", "RPM"}}}};
        QVERIFY(!m_dm->saveDashboard(spec).isEmpty());
        got = m_dm->loadDashboard(id).toMap();
        QVERIFY(!got.contains("thumbnail"));
        QVERIFY(!got.contains("alertBanner"));
        QVERIFY(!got.contains("showWhen"));
    }

    void previewShowsAndExpires()
    {
        QSignalSpy spy(m_dm.get(), &DashboardManager::previewChanged);
        QVariantMap spec{{"label", "Remote"}, {"gridColumns", 4}, {"gridRows", 2},
                         {"thumbnail", "data:image/png;base64,AAAA"},
                         {"cells", QVariantList{QVariantMap{{"type", "DigitalReadout"}, {"paramId", "RPM"},
                                                            {"col", 9}, {"row", 0}, {"colSpan", 1}, {"rowSpan", 1}}}}};
        QVERIFY(m_dm->previewDashboard(spec));
        QCOMPARE(spy.count(), 1);
        QVariantMap p = m_dm->preview();
        QCOMPARE(p.value("id").toString(), QString());   // not saved yet
        const QVariantMap shown = p.value("spec").toMap();
        QCOMPARE(shown.value("cells").toList().at(0).toMap().value("col").toInt(), 3);   // held to the rules
        QVERIFY(!shown.contains("id"));
        QVERIFY(!shown.contains("thumbnail"));
        // The same again changes nothing; a change does
        QVERIFY(m_dm->previewDashboard(spec));
        QCOMPARE(spy.count(), 1);
        spec["id"] = "remote";
        QVERIFY(m_dm->previewDashboard(spec));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(m_dm->preview().value("id").toString(), QStringLiteral("remote"));
        // Unusable: refused, and the preview ends
        QVERIFY(!m_dm->previewDashboard(QVariantMap{{"schema", 99}, {"cells", QVariantList{}}}));
        QVERIFY(m_dm->preview().isEmpty());
        QCOMPARE(spy.count(), 3);
        QVERIFY(m_dm->previewDashboard(spec));
        m_dm->endPreview();
        QVERIFY(m_dm->preview().isEmpty());
        m_dm->endPreview();
        QCOMPARE(spy.count(), 5);
    }

    void widgetPresetsSaveShareAndImport()
    {
        QSignalSpy spy(m_dm.get(), &DashboardManager::widgetPresetsChanged);
        QCOMPARE(m_dm->widgetPresets().size(), 0);
        const QString id = m_dm->saveWidgetPreset(QVariantMap{
            {"label", "  Redline   Tach "}, {"baseType", "CircularGauge"}, {"paramId", "RPM"},
            {"props", QVariantMap{{"showNeedle", true}, {"redlineStart", 6500}, {"showTicks", "no"},
                                  {"nested", QVariantMap{{"a", 1}}}}}});
        QCOMPARE(id, QStringLiteral("redline-tach"));
        QCOMPARE(spy.count(), 1);
        QVERIFY(QFile::exists(m_tmp->path() + "/widgets/redline-tach.json"));
        const QVariantMap p = m_dm->widgetPresets().at(0).toMap();
        QCOMPARE(p.value("label").toString(), QStringLiteral("Redline Tach"));
        QCOMPARE(p.value("paramId").toString(), QStringLiteral("RPM"));
        QCOMPARE(p.value("props").toMap(), (QVariantMap{{"showNeedle", true}, {"redlineStart", 6500}}));

        // Shared as text, imported as a new one beside it
        const QString text = m_dm->widgetPresetText(id);
        QVERIFY(text.contains("\"kind\":\"widgetPreset\""));
        QVERIFY(!text.contains("\"id\""));
        QCOMPARE(m_dm->importWidgetPresetFromText(text), QStringLiteral("redline-tach-2"));
        QCOMPARE(m_dm->widgetPresets().size(), 2);
        // Kept apart from dashboards both ways
        QCOMPARE(m_dm->importDashboardFromText(text), QString());
        QVERIFY(m_dm->lastShareError().contains("widget, not a dashboard"));
        QCOMPARE(m_dm->importWidgetPresetFromText(R"({"schema": 1, "label": "D", "cells": []})"), QString());
        QVERIFY(m_dm->lastShareError().contains("dashboard, not a widget"));
        QCOMPARE(m_dm->importWidgetPresetFromText(R"({"kind": "widgetPreset", "label": "X"})"), QString());
        QCOMPARE(m_dm->saveWidgetPreset(QVariantMap{{"baseType", "BarGauge"}}), QString());   // no name

        // Overwrite by id, delete, and a fresh manager reads the folder
        QCOMPARE(m_dm->saveWidgetPreset(QVariantMap{{"id", id}, {"label", "Tach"}, {"baseType", "ArcGauge"}}), id);
        QCOMPARE(m_dm->widgetPresets().size(), 2);
        QVERIFY(m_dm->deleteWidgetPreset("redline-tach-2"));
        QVERIFY(!m_dm->deleteWidgetPreset("../evil"));
        DashboardManager again;
        again.setWidgetCatalogFile(QStringLiteral(OCTAVE_SOURCE_DIR "/frontend/dashboards/widget-catalog.json"));
        again.setUserDir(m_tmp->path() + "/dashboards");
        QCOMPARE(again.widgetPresets().size(), 1);
        QCOMPARE(again.widgetPresets().at(0).toMap().value("baseType").toString(), QStringLiteral("ArcGauge"));
    }

    void minSpanReachesTheReadableSize()
    {
        // CircularGauge minSize 110x110 dp; 40 dp cells with 16 dp gutters:
        // 2 cells = 96, 3 cells = 152
        QCOMPARE(m_dm->minSpan("CircularGauge", 12, 8, 40, 40, 16),
                 (QVariantMap{{"colSpan", 3}, {"rowSpan", 3}}));
        // Held to the grid on a tiny one
        QCOMPARE(m_dm->minSpan("CircularGauge", 2, 2, 20, 20, 0),
                 (QVariantMap{{"colSpan", 2}, {"rowSpan", 2}}));
        // Unknown type: any size
        QCOMPARE(m_dm->minSpan("Nope", 12, 8, 40, 40, 16), (QVariantMap{{"colSpan", 1}, {"rowSpan", 1}}));
    }

    void sizeOptionsGrowAndKeepShape()
    {
        // 12x8 grid of 80x60 dp cells (a 1280x720-ish canvas)
        const QVariantList dial = m_dm->sizeOptions("CircularGauge", 12, 8, 80, 60, 16);
        QCOMPARE(dial.size(), 3);
        int lastArea = 0;
        for (const QVariant &v : dial) {
            const QVariantMap o = v.toMap();
            const int cs = o.value("colSpan").toInt(), rs = o.value("rowSpan").toInt();
            QVERIFY(cs * rs > lastArea);
            lastArea = cs * rs;
            const double w = cs * 80 + (cs - 1) * 16, h = rs * 60 + (rs - 1) * 16;
            QVERIFY2(w / h > 0.6 && w / h < 1.6, qPrintable(QString("%1x%2").arg(cs).arg(rs)));   // roughly round
            QVERIFY(w >= 110 && h >= 110);
        }
        QCOMPARE(dial.first().toMap().value("label").toString(), QStringLiteral("S"));
        // A bar is wide
        const QVariantMap bar = m_dm->sizeOptions("BarGauge", 12, 8, 80, 60, 16).first().toMap();
        QVERIFY(bar.value("colSpan").toInt() > bar.value("rowSpan").toInt());
        // A tiny grid offers what fits, no repeats
        const QVariantList tiny = m_dm->sizeOptions("CircularGauge", 2, 2, 60, 60, 8);
        QVERIFY(!tiny.isEmpty() && tiny.size() <= 3);
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
