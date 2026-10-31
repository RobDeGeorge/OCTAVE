// Widget behavior against the real catalog/store and renderer, including
// missing samples, converted thresholds, small cells and clock preferences.
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <memory>

#include "managers/dashboardmanager.h"
#include "managers/parametercatalog.h"
#include "managers/parameterstore.h"
#include "managers/themecatalog.h"

namespace {
const QString frontend = QStringLiteral(OCTAVE_SOURCE_DIR "/frontend");
QQuickItem *findVisual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = findVisual(child, name))
            return found;
    return nullptr;
}
}

class TstDashboardWidgets : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        catalog = std::make_unique<ParameterCatalog>();
        store = std::make_unique<ParameterStore>();
        themes = std::make_unique<ThemeCatalog>(frontend + "/themes.json");
        dashboards = std::make_unique<DashboardManager>();
        dashboards->setWidgetCatalogFile(frontend + "/dashboards/widget-catalog.json");
        dashboards->setTemplatesDir(frontend + "/dashboards/templates");
        settings = std::make_unique<QQmlPropertyMap>();
        settings->insert("clockFormat24Hour", true);
        settings->insert("showClock", false);
        engine = std::make_unique<QQmlEngine>();
        engine->addImportPath(frontend);
        auto *ctx = engine->rootContext();
        ctx->setContextProperty("parameterCatalog", catalog.get());
        ctx->setContextProperty("paramStore", store.get());
        ctx->setContextProperty("themeCatalog", themes.get());
        ctx->setContextProperty("dashboardManager", dashboards.get());
        ctx->setContextProperty("settingsManager", settings.get());
        ctx->setContextProperty("width", 1280);
        ctx->setContextProperty("height", 720);
    }

    void cleanup()
    {
        root.reset();
        engine.reset();
    }

    void segmentedMissingZeroAndInvalid()
    {
        QVERIFY(load("gauges/SegmentedGauge.qml"));
        root->setProperty("paramId", "RPM");
        QVERIFY(!root->property("hasValue").toBool());
        QCOMPARE(root->property("_formatted").toString(), QStringLiteral("--"));
        QCOMPARE(root->property("_litSegments").toInt(), 0);

        store->ingest("RPM", 0);
        QTRY_VERIFY(root->property("hasValue").toBool());
        QCOMPARE(root->property("_formatted").toString(), QStringLiteral("0"));
        QCOMPARE(root->property("_litSegments").toInt(), 0);
        store->ingest("RPM", 4000);
        QTRY_COMPARE(root->property("value").toDouble(), 4000.0);
        QCOMPARE(root->property("_litSegments").toInt(), 10); // catalog RPM max = 8000

        root->setProperty("warnAbove", 3500);
        QVERIFY(root->property("alertLevel").toInt() > 0);
        root->setProperty("value", qQNaN());
        QVERIFY(!root->property("hasValue").toBool());
        QVERIFY(!root->property("alertLevel").toInt() > 0);
        QCOMPARE(root->property("_litSegments").toInt(), 0);
        root->setProperty("value", qInf());
        QVERIFY(!root->property("hasValue").toBool());
    }

    void segmentedRangeAndPrecision()
    {
        QVERIFY(load("gauges/SegmentedGauge.qml"));
        root->setProperty("paramId", "CONTROL_MODULE_VOLTAGE");
        root->setProperty("decimals", 1);
        store->ingest("CONTROL_MODULE_VOLTAGE", 14.2f);
        QTRY_COMPARE(root->property("_formatted").toString(), QStringLiteral("14.2"));
        root->setProperty("min", -10);
        root->setProperty("max", 10);
        root->setProperty("value", -20);
        QCOMPARE(root->property("_litSegments").toInt(), 0);
        root->setProperty("value", 20);
        QCOMPARE(root->property("_litSegments").toInt(), 20);
        root->setProperty("max", -10);
        QCOMPARE(root->property("_litSegments").toInt(), 0);
        root->setProperty("segmentCount", 1000);
        QCOMPARE(root->property("_count").toInt(), 40);
    }

    void rendererConvertsThresholdAndPollsOnlyTheGauge()
    {
        QVERIFY(load("dashboards/DashboardRenderer.qml"));
        const QVariantList cells{
            QVariantMap{{"type", "SegmentedGauge"}, {"paramId", "COOLANT_TEMP"},
                {"col", 0}, {"row", 0}, {"colSpan", 6}, {"rowSpan", 2},
                {"props", QVariantMap{{"min", 0}, {"max", 120}, {"warnAbove", 100}}}},
            QVariantMap{{"type", "ClockWidget"}, {"col", 6}, {"row", 0},
                {"colSpan", 6}, {"rowSpan", 2}}
        };
        root->setProperty("spec", QVariantMap{{"schema", 1}, {"cells", cells}});
        auto *item = qobject_cast<QQuickItem *>(root.get());
        QVERIFY(item);
        QTRY_VERIFY(findVisual(item, "segmentedValue"));
        QTRY_VERIFY(findVisual(item, "dashboardClockTime"));
        QCOMPARE(root->property("paramIds").toStringList(), QStringList{"COOLANT_TEMP"});
        auto *gauge = findVisual(item, "segmentedValue")->parentItem()->parentItem();
        store->ingest("COOLANT_TEMP", 100);
        QTRY_COMPARE(gauge->property("value").toDouble(), 100.0);
        QVERIFY(gauge->property("alertLevel").toInt() > 0);
        catalog->setDisplayUnits("F", "mph", "m");
        QTRY_COMPARE(gauge->property("unit").toString(), QStringLiteral("°F"));
        QCOMPARE(gauge->property("warnAbove").toDouble(), 212.0);
        QCOMPARE(gauge->property("value").toDouble(), 212.0);
        QCOMPARE(gauge->property("min").toDouble(), 32.0);
        QVERIFY(gauge->property("alertLevel").toInt() > 0);
    }

    // Every PID gauge: no sample is "--" with nothing drawn and no warning;
    // a real 0 is a value; NaN and infinity count as no sample.
    void missingIsNotZero_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("threshold"); // a warning prop set low enough to fire on 0
        QTest::addColumn<QString>("warningProp");
        for (const char *g : {"Circular", "Arc", "Bar", "Linear", "Segmented", "Sparkline"})
            QTest::newRow(g) << QStringLiteral("gauges/%1Gauge.qml").arg(QLatin1String(g)) << "warnAbove" << "alertLevel";
        QTest::newRow("Digital") << "gauges/DigitalReadout.qml" << "critAbove" << "alertLevel";
        QTest::newRow("Warning") << "gauges/WarningLight.qml" << "triggerBelow" << "_active";
    }

    // The shared alert rule: warning and critical limits, above and below,
    // inclusive; critical wins; the dials draw a band for each
    void alertLevels_data()
    {
        QTest::addColumn<QString>("file");
        for (const char *g : {"Circular", "Arc", "Bar", "Linear", "Segmented", "Sparkline"})
            QTest::newRow(g) << QStringLiteral("gauges/%1Gauge.qml").arg(QLatin1String(g));
        QTest::newRow("Digital") << "gauges/DigitalReadout.qml";
    }

    void alertLevels()
    {
        QFETCH(QString, file);
        QVERIFY(load(file));
        root->setProperty("min", 0);
        root->setProperty("max", 100);
        root->setProperty("warnAbove", 70);
        root->setProperty("critAbove", 90);
        root->setProperty("warnBelow", 20);
        root->setProperty("critBelow", 10);
        const QList<QPair<double, int>> cases{{50, 0}, {70, 1}, {89, 1}, {90, 2}, {100, 2},
                                              {20, 1}, {11, 1}, {10, 2}, {0, 2}};
        for (const auto &c : cases) {
            root->setProperty("value", c.first);
            QVERIFY2(root->property("alertLevel").toInt() == c.second,
                     qPrintable(QStringLiteral("%1 -> %2").arg(c.first).arg(root->property("alertLevel").toInt())));
        }
        root->setProperty("value", qQNaN());
        QCOMPARE(root->property("alertLevel").toInt(), 0);
        if (file.contains("Circular") || file.contains("Arc")) {
            const QVariantList bands = root->property("_bands").toList();
            QCOMPARE(bands.size(), 4);
            // The warning band stops where the critical one starts
            QCOMPARE(bands.at(0).toMap().value("from").toDouble(), 0.7);
            QCOMPARE(bands.at(0).toMap().value("to").toDouble(), 0.9);
        }
        // Flash: the critical value blinks only when asked to
        root->setProperty("value", 95);
        QCOMPARE(root->property("_alertOpacity").toDouble(), 1.0);
        root->setProperty("alertFlash", true);
        QTRY_VERIFY(root->property("_alertOpacity").toDouble() < 1.0);
        root->setProperty("value", 50);
        QTRY_COMPARE(root->property("_alertOpacity").toDouble(), 1.0);
    }

    void missingIsNotZero()
    {
        QFETCH(QString, file);
        QFETCH(QString, threshold);
        QFETCH(QString, warningProp);
        QVERIFY(load(file));
        root->setProperty("paramId", "FUEL_LEVEL");
        if (!threshold.isEmpty())
            root->setProperty(qPrintable(threshold), threshold == "triggerBelow" ? 15 : -1);
        QVERIFY(!root->property("hasValue").toBool());
        if (root->property("_normalized").isValid())
            QCOMPARE(root->property("_normalized").toDouble(), 0.0);
        if (!warningProp.isEmpty())
            QVERIFY2(!root->property(qPrintable(warningProp)).toBool(), "warning lit with no sample");
        auto *item = qobject_cast<QQuickItem *>(root.get());
        const bool readout = !file.contains("WarningLight"); // a lamp has no number
        if (readout)
            QVERIFY(hasText(item, "--"));

        store->ingest("FUEL_LEVEL", 0);
        QTRY_VERIFY(root->property("hasValue").toBool());
        if (readout)
            QVERIFY(!hasText(item, "--"));
        if (!warningProp.isEmpty())
            QVERIFY(root->property(qPrintable(warningProp)).toBool()); // 0 is a real reading
        root->setProperty("value", qQNaN());
        QVERIFY(!root->property("hasValue").toBool());
        if (!warningProp.isEmpty())
            QVERIFY(!root->property(qPrintable(warningProp)).toBool());
        root->setProperty("value", qInf());
        QVERIFY(!root->property("hasValue").toBool());
    }

    void sensorWidgetsWithoutImu()
    {
        QVERIFY(load("gauges/CompassGauge.qml"));
        QVERIFY(!root->property("hasValue").toBool());
        QVERIFY(hasText(qobject_cast<QQuickItem *>(root.get()), "--°"));
        store->ingest("HEADING", 359.6);
        QTRY_VERIFY(root->property("hasValue").toBool());
        QCOMPARE(root->property("_degrees").toInt(), 0); // never "360°"

        QVERIFY(load("gauges/GForceGauge.qml"));
        QVERIFY(!root->property("hasValue").toBool());
        QVERIFY(hasText(qobject_cast<QQuickItem *>(root.get()), "-- g"));
        root->setProperty("maxG", 1.0);
        store->ingest("LATERAL_G", 2.0);
        store->ingest("LONGITUDINAL_G", 2.0);
        QTRY_VERIFY(root->property("hasValue").toBool());
        // A diagonal past the ring stays on the ring, not in its corner
        QVariant pos;
        QMetaObject::invokeMethod(root.get(), "_ringPos", Q_RETURN_ARG(QVariant, pos),
                                  Q_ARG(QVariant, 2.0), Q_ARG(QVariant, 2.0));
        const QPointF ring = pos.toPointF();
        QVERIFY(std::abs(std::hypot(ring.x(), ring.y()) - 1.0) < 1e-6);
    }

    // BarGauge: switching orientation after creation lays out like creating
    // it so; the fill is proportional and stays inside the track.
    void barGeometry_data()
    {
        QTest::addColumn<QString>("orientation");
        QTest::addColumn<double>("fuel");
        for (const char *o : {"horizontal", "vertical"})
            for (double v : {0.0, 1.0, 8.0, 50.0, 100.0})
                QTest::newRow(qPrintable(QString("%1-%2").arg(o).arg(v))) << QString(o) << v;
    }

    void barGeometry()
    {
        QFETCH(QString, orientation);
        QFETCH(double, fuel);
        QVERIFY(load("gauges/BarGauge.qml"));
        root->setProperty("width", 284);
        root->setProperty("height", 243);
        root->setProperty("paramId", "FUEL_LEVEL");
        store->ingest("FUEL_LEVEL", fuel);
        QTRY_VERIFY(root->property("hasValue").toBool());
        // Created horizontal, then switched (the renderer's assignment order)
        root->setProperty("orientation", orientation);
        auto *item = qobject_cast<QQuickItem *>(root.get());
        auto *track = findVisual(item, "barTrack");
        auto *fill = findVisual(item, "barFill");
        QVERIFY(track && fill);
        QTRY_COMPARE(root->property("_animNorm").toDouble(), fuel / 100.0);
        const bool horizontal = orientation == "horizontal";
        if (horizontal)
            QVERIFY(track->width() > track->height());
        else
            QVERIFY(track->height() > track->width());
        QVERIFY(track->x() >= 0 && track->y() >= 0);
        QVERIFY(track->x() + track->width() <= item->width() + 0.5);
        QVERIFY(track->y() + track->height() <= item->height() + 0.5);
        const double length = horizontal ? track->width() : track->height();
        const double filled = fuel == 0 ? 0 : (horizontal ? fill->width() : fill->height());
        QVERIFY2(std::abs(filled - length * fuel / 100.0) < 0.5,
                 qPrintable(QString("fill %1 of %2").arg(filled).arg(length)));
        QVERIFY(fill->x() >= 0 && fill->y() >= 0);
        QVERIFY(fill->x() + fill->width() <= track->width() + 0.5);
        QVERIFY(fill->y() + fill->height() <= track->height() + 0.5);
    }

    void clockFormatAndHiddenBar()
    {
        QVERIFY(load("dashboards/widgets/ClockWidget.qml"));
        root->setProperty("visible", false); // stop timer for a deterministic time
        root->setProperty("_now", QDateTime(QDate(2026, 10, 29), QTime(0, 5, 9)));
        QCOMPARE(root->property("_timeText").toString(), QStringLiteral("00:05"));
        root->setProperty("showSeconds", true);
        QCOMPARE(root->property("_timeText").toString(), QStringLiteral("00:05:09"));
        settings->insert("clockFormat24Hour", false);
        QCOMPARE(root->property("_timeText").toString(), QStringLiteral("12:05:09 AM"));
        root->setProperty("_now", QDateTime(QDate(2026, 10, 29), QTime(13, 5, 9)));
        QCOMPARE(root->property("_timeText").toString(), QStringLiteral("01:05:09 PM"));
        QVERIFY(!settings->value("showClock").toBool()); // independent of the bar
    }

    void smallCellBounds_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QSize>("size");
        const QStringList files{
            "gauges/SegmentedGauge.qml", "dashboards/widgets/ClockWidget.qml",
            "gauges/CircularGauge.qml", "gauges/ArcGauge.qml", "gauges/BarGauge.qml",
            "gauges/LinearGauge.qml", "gauges/DigitalReadout.qml", "gauges/SparklineGauge.qml",
            "gauges/WarningLight.qml", "gauges/GForceGauge.qml", "gauges/CompassGauge.qml"};
        for (const auto &file : files)
            for (const QSize size : {QSize(160, 50), QSize(80, 32), QSize(320, 100), QSize(164, 103),
                                     QSize(100, 240)})
                QTest::newRow(qPrintable(file + QString::number(size.width()) + "x"
                                         + QString::number(size.height()))) << file << size;
    }

    void smallCellBounds()
    {
        QFETCH(QString, file);
        QFETCH(QSize, size);
        QVERIFY(load(file));
        auto *item = qobject_cast<QQuickItem *>(root.get());
        QQuickWindow window;
        window.resize(size);
        item->setParentItem(window.contentItem());
        item->setSize(size);
        // Worst-case text: a long title, a wide value, a unit
        if (item->property("title").isValid()) {
            item->setProperty("title", "A deliberately long dashboard label");
            item->setProperty("unit", "kPa");
        }
        if (item->property("value").isValid() && !file.contains("Clock"))
            item->setProperty("value", 123456.789);
        if (file.contains("WarningLight")) {
            item->setProperty("label", "CHECK ENGINE");
            item->setProperty("triggerAbove", 1);
        }
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(50); // let the positioners polish after resizing
        checkBounds(item, item);
        item->setParentItem(nullptr);
    }

    void paletteFitsAndScrolls_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("desktop") << QSize(1280, 720);
        QTest::newRow("small-landscape") << QSize(800, 480);
        QTest::newRow("portrait") << QSize(390, 844);
    }

    void paletteFitsAndScrolls()
    {
        QFETCH(QSize, size);
        QQmlComponent component(engine.get());
        component.setData(QString(R"qml(
            import QtQuick
            import QtQuick.Controls
            import "%1" as App
            import "%1/dashboards/editor" as Editor
            ApplicationWindow {
                width: 1280; height: 720; visible: true
                Component.onCompleted: App.Spacing.autoScaleFactor = 1.4
                Editor.PalettePopup { Component.onCompleted: open() }
            }
        )qml").arg(QUrl::fromLocalFile(frontend).toString()).toUtf8(), QUrl());
        root.reset(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        window->resize(size);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *popup = root->findChild<QObject *>("widgetPalette");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(popup->property("x").toDouble() >= 0);
        QVERIFY(popup->property("y").toDouble() >= 0);
        QVERIFY(popup->property("height").toDouble() <= size.height());
        auto *scroll = root->findChild<QObject *>("widgetPaletteScroll");
        QVERIFY(scroll);
        auto *flick = qvariant_cast<QObject *>(scroll->property("contentItem"));
        QVERIFY(flick);
        QTRY_VERIFY(flick->property("contentHeight").toDouble() > flick->property("height").toDouble());
        flick->setProperty("contentY", flick->property("contentHeight").toDouble() - flick->property("height").toDouble());
        auto *clock = findVisual(window->contentItem(), "paletteCard_ClockWidget");
        QVERIFY(clock);
        const QPointF center = clock->mapToScene(QPointF(clock->width() / 2, clock->height() / 2));
        QVERIFY(center.x() > 0 && center.x() < size.width());
        QVERIFY(center.y() > 0 && center.y() < size.height());
        QSignalSpy picked(popup, SIGNAL(widgetPicked(QString)));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        QCOMPARE(picked.size(), 1);
        QCOMPARE(picked.first().first().toString(), QStringLiteral("ClockWidget"));
    }

    // The editor changes only what an edit touches (other widgets keep their
    // loaded instance, so traces and animations survive), moves go through
    // swap / push, and undo / redo walk the edits.
    void editorEditsInPlaceAndUndoes()
    {
        QQmlComponent component(engine.get());
        component.setData(QString(R"qml(
            import QtQuick
            import QtQuick.Controls
            import "%1/dashboards/editor" as Editor
            ApplicationWindow {
                width: 1280; height: 720; visible: true
                property alias editor: ed
                Editor.DashboardEditor { id: ed; anchors.fill: parent; autoDensity: false }
            }
        )qml").arg(QUrl::fromLocalFile(frontend).toString()).toUtf8(), QUrl());
        root.reset(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QObject *editor = qvariant_cast<QObject *>(root->property("editor"));
        QVERIFY(editor);
        auto call = [editor](const char *method, QVariantList args = {}) {
            QVariant ret;
            bool ok = false;
            switch (args.size()) {
            case 0: ok = QMetaObject::invokeMethod(editor, method, Q_RETURN_ARG(QVariant, ret)); break;
            case 1: ok = QMetaObject::invokeMethod(editor, method, Q_RETURN_ARG(QVariant, ret),
                                                   Q_ARG(QVariant, args[0])); break;
            case 2: ok = QMetaObject::invokeMethod(editor, method, Q_RETURN_ARG(QVariant, ret),
                                                   Q_ARG(QVariant, args[0]), Q_ARG(QVariant, args[1])); break;
            default: ok = QMetaObject::invokeMethod(editor, method, Q_RETURN_ARG(QVariant, ret),
                                                    Q_ARG(QVariant, args[0]), Q_ARG(QVariant, args[1]),
                                                    Q_ARG(QVariant, args[2])); break;
            }
            if (!ok)
                qWarning() << "no" << method << "with" << args.size() << "args";
            return ret;
        };
        auto cells = [editor] { return editor->property("cells").toList(); };
        auto widgetAt = [window](int i) -> QObject * {
            auto *cell = findVisual(window->contentItem(), QStringLiteral("editorCell_%1").arg(i));
            if (!cell)
                return nullptr;
            auto *loader = findVisual(cell, QStringLiteral("editorWidgetLoader"));
            return loader ? qvariant_cast<QObject *>(loader->property("item")) : nullptr;
        };

        QVERIFY(!editor->property("canUndo").toBool());
        call("addCellAt", {"ClockWidget", 0, 0});           // 4x2 at 0,0
        QTRY_VERIFY(widgetAt(0));
        QObject *clock = widgetAt(0);
        call("addCellAt", {"CompassGauge", 4, 0});          // 4x4 at 4,0
        QTRY_VERIFY(widgetAt(1));
        QObject *compass = widgetAt(1);
        QCOMPARE(widgetAt(0), clock);                        // adding kept the clock
        QCOMPARE(cells().size(), 2);
        QVERIFY(editor->property("canUndo").toBool());

        // Move: only geometry changes, no widget reloads
        call("moveCell", {0, 0, 4});
        QCOMPARE(cells().at(0).toMap().value("row").toInt(), 4);
        QCOMPARE(widgetAt(0), clock);
        QCOMPARE(widgetAt(1), compass);

        // An option change reloads that widget only
        call("setCellProp", {1, "showDegrees", false});
        QTRY_VERIFY(widgetAt(1) && widgetAt(1) != compass);
        QCOMPARE(widgetAt(0), clock);
        QCOMPARE(widgetAt(1)->property("showDegrees").toBool(), false);

        // Drop the clock (4x2) onto the compass (4x4): the compass doesn't fit
        // the clock's old 4x2 spot, so it is pushed aside and nothing overlaps
        call("moveCell", {0, 4, 0});
        QCOMPARE(cells().at(0).toMap().value("col").toInt(), 4);
        QCOMPARE(cells().at(0).toMap().value("row").toInt(), 0);
        const QVariantMap pushed = cells().at(1).toMap();
        QVERIFY(dashboards->canPlace(cells(), 12, 6, pushed.value("col").toInt(), pushed.value("row").toInt(),
                                     pushed.value("colSpan").toInt(), pushed.value("rowSpan").toInt(), 1));

        // Undo walks back: push, prop, move, add, add
        call("undo");
        QCOMPARE(cells().at(0).toMap().value("row").toInt(), 4);
        QCOMPARE(cells().at(1).toMap().value("col").toInt(), 4);
        call("undo");
        QCOMPARE(cells().at(1).toMap().value("props").toMap().value("showDegrees"), QVariant());
        call("undo");
        QCOMPARE(cells().at(0).toMap().value("row").toInt(), 0);
        call("undo");
        QCOMPARE(cells().size(), 1);
        QTRY_VERIFY(!findVisual(window->contentItem(), "editorCell_1"));
        call("undo");
        QCOMPARE(cells().size(), 0);
        QVERIFY(!editor->property("canUndo").toBool());
        // ...and redo forward again
        for (int i = 0; i < 5; ++i)
            call("redo");
        QCOMPARE(cells().size(), 2);
        QCOMPARE(cells().at(0).toMap().value("col").toInt(), 4);
        QVERIFY(!editor->property("canRedo").toBool());
        // A new edit after an undo drops the redo branch
        call("undo");
        call("removeCell", {0});
        QVERIFY(!editor->property("canRedo").toBool());
        QCOMPARE(cells().size(), 1);
        QTRY_VERIFY(widgetAt(0));
        QVERIFY(QString(widgetAt(0)->metaObject()->className()).startsWith("CompassGauge"));
    }

    // Touch rules: an unselected widget doesn't move under a passing drag,
    // the selected one does; density, duplicate, swap and delete + undo
    void editorTouchEditing()
    {
        QQmlComponent component(engine.get());
        component.setData(QString(R"qml(
            import QtQuick
            import QtQuick.Controls
            import "%1/dashboards/editor" as Editor
            ApplicationWindow {
                width: 1280; height: 720; visible: true
                property alias editor: ed
                Editor.DashboardEditor { id: ed; anchors.fill: parent; autoDensity: false }
            }
        )qml").arg(QUrl::fromLocalFile(frontend).toString()).toUtf8(), QUrl());
        root.reset(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QObject *editor = qvariant_cast<QObject *>(root->property("editor"));
        QVERIFY(editor);
        auto cells = [editor] { return editor->property("cells").toList(); };
        auto cellAt = [&cells](int i) { return cells().at(i).toMap(); };
        auto center = [window](const QString &name) {
            auto *item = findVisual(window->contentItem(), name);
            return item ? item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint() : QPoint(-1, -1);
        };
        auto drag = [window](QPoint from, QPoint to) {
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
            for (int i = 1; i <= 12; ++i)
                QTest::mouseMove(window, from + (to - from) * i / 12, 16);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to, 16);
        };

        QVERIFY(invoke(editor, "addCellAt", {"ClockWidget", 0, 0}).toBool());     // 4x2 at 0,0
        QVERIFY(invoke(editor, "addCellAt", {"CompassGauge", 4, 0}).toBool());    // 4x4 at 4,0
        QCOMPARE(editor->property("selectedIndex").toInt(), 1);
        QTRY_VERIFY(findVisual(window->contentItem(), "editorCell_1"));

        auto *canvas = findVisual(window->contentItem(), "editorCanvas");
        QVERIFY(canvas);
        const double stepY = canvas->property("_cellH").toDouble() + canvas->property("_spacing").toDouble();

        // The clock isn't selected: dragging over it moves nothing
        QPoint from = center("editorCell_0");
        drag(from, from + QPoint(0, int(stepY * 3)));
        QCOMPARE(cellAt(0).value("row").toInt(), 0);
        QCOMPARE(cellAt(0).value("col").toInt(), 0);

        // Selected, it follows the finger and lands on the cell under it
        invoke(editor, "select", {0});
        QTRY_VERIFY(findVisual(window->contentItem(), "editorToolbar")->isVisible());
        from = center("editorCell_0");
        drag(from, from + QPoint(0, int(stepY * 3)));
        QTRY_COMPARE(cellAt(0).value("row").toInt(), 3);
        QCOMPARE(cellAt(0).value("col").toInt(), 0);

        // Tap-tap swap: the clock (4x2) goes where the compass (4x4) was;
        // the compass doesn't fit the clock's spot, so it moves aside
        invoke(editor, "swapCells", {0, 1});
        QCOMPARE(cellAt(0).value("col").toInt(), 4);
        QCOMPARE(cellAt(0).value("row").toInt(), 0);
        for (int i = 0; i < 2; ++i)
            QVERIFY(dashboards->canPlace(cells(), 12, 6, cellAt(i).value("col").toInt(), cellAt(i).value("row").toInt(),
                                         cellAt(i).value("colSpan").toInt(), cellAt(i).value("rowSpan").toInt(), i));

        // Duplicate lands beside, overlapping nothing
        QVERIFY(invoke(editor, "duplicateCell", {1}).toBool());
        QCOMPARE(cells().size(), 3);
        QCOMPARE(cellAt(2).value("type").toString(), QStringLiteral("CompassGauge"));
        QVERIFY(dashboards->canPlace(cells(), 12, 6, cellAt(2).value("col").toInt(), cellAt(2).value("row").toInt(),
                                     4, 4, 2));

        // A new widget with no place given finds room by itself
        // (a widget without a PID to pick, so no picker opens over the test)
        QVERIFY(invoke(editor, "addCellAt", {"ClockWidget", -1, -1}).toBool());
        QCOMPARE(cells().size(), 4);

        // Density: the same layout on 6 x 4, and undo puts the grid back
        invoke(editor, "setDensity", {"normal"});
        QCOMPARE(editor->property("gridColumns").toInt(), 6);
        QCOMPARE(editor->property("gridRows").toInt(), 4);
        QCOMPARE(editor->property("density").toString(), QStringLiteral("normal"));
        for (int i = 0; i < cells().size(); ++i)
            QVERIFY(dashboards->canPlace(cells(), 6, 4, cellAt(i).value("col").toInt(), cellAt(i).value("row").toInt(),
                                         cellAt(i).value("colSpan").toInt(), cellAt(i).value("rowSpan").toInt(), i));
        invoke(editor, "undo");
        QCOMPARE(editor->property("gridColumns").toInt(), 12);
        QCOMPARE(editor->property("gridRows").toInt(), 6);
        QCOMPARE(editor->property("density").toString(), QStringLiteral("custom"));

        // Delete shows "removed · Undo"; Undo brings it back
        invoke(editor, "deleteCell", {0});
        QCOMPARE(cells().size(), 3);
        QTRY_VERIFY(findVisual(window->contentItem(), "editorSnackAction")->isVisible());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center("editorSnackAction"));
        QTRY_COMPARE(cells().size(), 4);
        QCOMPARE(cellAt(0).value("type").toString(), QStringLiteral("ClockWidget"));

        // Settings: the sheet opens beside the dashboard, which zooms out to
        // fit what is left and takes no drags meanwhile
        invoke(editor, "select", {1});
        invoke(editor, "openSheet");
        QTRY_VERIFY(findVisual(window->contentItem(), "editorPanel")->isVisible());
        QTRY_VERIFY(canvas->scale() < 0.9);
        QVERIFY(!canvas->property("interactive").toBool());
        QVERIFY(!findVisual(window->contentItem(), "editorToolbar")->isVisible());
        invoke(editor, "closeSheet");
        QTRY_COMPARE(canvas->scale(), 1.0);
        QVERIFY(canvas->property("interactive").toBool());

        // Close with changes asks first
        auto *dialog = root->findChild<QObject *>("editorCloseDialog");
        QVERIFY(dialog);
        invoke(editor, "close");
        QTRY_VERIFY(dialog->property("opened").toBool());
    }

    // Phase 2: a dashboard from a template, data-first add, the settings
    // sheet's sections, Test value and JSON, and the PID picker's groups
    void editorCreationAndSettings()
    {
        QQmlComponent component(engine.get());
        component.setData(QString(R"qml(
            import QtQuick
            import QtQuick.Controls
            import "%1/dashboards/editor" as Editor
            ApplicationWindow {
                width: 1280; height: 720; visible: true
                property alias editor: ed
                property alias fromTemplate: tpl
                property alias picker: pk
                Editor.DashboardEditor { id: ed; anchors.fill: parent; autoDensity: false }
                Editor.DashboardEditor {
                    id: tpl; visible: false; width: 1280; height: 720; autoDensity: false
                    initialSpec: dashboardManager.fromTemplate("hero-4", [])
                }
                Editor.PIDPicker { id: pk }
            }
        )qml").arg(QUrl::fromLocalFile(frontend).toString()).toUtf8(), QUrl());
        root.reset(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QObject *editor = qvariant_cast<QObject *>(root->property("editor"));
        QObject *tpl = qvariant_cast<QObject *>(root->property("fromTemplate"));
        QObject *picker = qvariant_cast<QObject *>(root->property("picker"));
        QVERIFY(editor && tpl && picker);
        auto cellAt = [editor](int i) { return editor->property("cells").toList().at(i).toMap(); };

        // From a template: its cells and name, nothing to save or undo yet
        QCOMPARE(tpl->property("cells").toList().size(), 5);
        QCOMPARE(tpl->property("editingId").toString(), QString());
        QVERIFY(!tpl->property("dirty").toBool());
        QVERIFY(!tpl->property("canUndo").toBool());

        // Data first: the suggested widget, already bound, no picker
        QVERIFY(invoke(editor, "addForParam", {"COOLANT_TEMP"}).toBool());
        QCOMPARE(cellAt(0).value("type").toString(), QStringLiteral("ArcGauge"));
        QCOMPARE(cellAt(0).value("paramId").toString(), QStringLiteral("COOLANT_TEMP"));
        QVERIFY(invoke(editor, "addForParam", {"LATERAL_G"}).toBool());
        QCOMPARE(cellAt(1).value("type").toString(), QStringLiteral("GForceGauge"));
        QCOMPARE(cellAt(1).value("paramId").toString(), QString());   // reads its own data
        QVERIFY(invoke(editor, "addForParam", {"SPEED"}).toBool());
        QCOMPARE(cellAt(2).value("type").toString(), QStringLiteral("DigitalReadout"));
        auto *pidPicker = root->findChild<QObject *>("pidPicker");
        QVERIFY(!pidPicker || !pidPicker->property("opened").toBool());

        auto widgetAt = [window](int i) -> QObject * {
            auto *cell = findVisual(window->contentItem(), QStringLiteral("editorCell_%1").arg(i));
            auto *loader = cell ? findVisual(cell, QStringLiteral("editorWidgetLoader")) : nullptr;
            return loader ? qvariant_cast<QObject *>(loader->property("item")) : nullptr;
        };

        // The sheet: sections for what the widget has
        invoke(editor, "select", {0});
        invoke(editor, "openSheet");
        QTRY_VERIFY(findVisual(window->contentItem(), "editorPanel")->isVisible());
        QVERIFY(findVisual(window->contentItem(), "panelSection_data"));
        QVERIFY(findVisual(window->contentItem(), "panelSection_range"));
        QVERIFY(findVisual(window->contentItem(), "panelSection_json"));

        // Test value: this widget shows it, with its alert level; Live drops it
        invoke(editor, "setCellProp", {0, "critAbove", 90});
        QTRY_VERIFY(widgetAt(0) && widgetAt(0)->property("critAbove").toDouble() == 90);
        invoke(editor, "setTestValue", {0, 100});
        QTRY_COMPARE(widgetAt(0)->property("value").toDouble(), 100.0);
        QCOMPARE(widgetAt(0)->property("alertLevel").toInt(), 2);
        invoke(editor, "clearTestValue", {0});
        QTRY_VERIFY(widgetAt(0) && !widgetAt(0)->property("hasValue").toBool());   // live again: no reading
        // ...and never in the spec
        QVERIFY(!cellAt(0).value("props").toMap().contains("value"));

        // JSON: applied through the spec rules, the rest reported
        QString status = invoke(editor, "setCellPropsJson", {0, R"({"critAbove": 95, "bogus": {"a": 1}})"}).toString();
        QVERIFY2(status.contains("bogus"), qPrintable(status));
        QCOMPARE(cellAt(0).value("props").toMap().value("critAbove").toDouble(), 95.0);
        QVERIFY(!cellAt(0).value("props").toMap().contains("bogus"));
        status = invoke(editor, "setCellPropsJson", {0, "{ nope"}).toString();
        QVERIFY(status.startsWith("Not valid JSON"));
        QCOMPARE(cellAt(0).value("props").toMap().value("critAbove").toDouble(), 95.0);

        // A widget without a PID has no Data section
        invoke(editor, "select", {1});
        QTRY_VERIFY(!findVisual(window->contentItem(), "panelSection_data"));

        // PID picker: grouped by system in catalog order, recent picks first
        picker->setProperty("supportedKinds", QStringList{"*"});
        QVERIFY(QMetaObject::invokeMethod(picker, "open"));
        QTRY_VERIFY(picker->property("opened").toBool());
        QVariantList rows = picker->property("_rows").toList();
        QCOMPARE(rows.first().toMap().value("label").toString(), QStringLiteral("Engine"));
        QStringList headers;
        for (const QVariant &r : rows)
            if (r.toMap().value("header").toBool())
                headers << r.toMap().value("label").toString();
        QCOMPARE(headers, catalog->groups());
        QVERIFY(QMetaObject::invokeMethod(picker, "_pick", Q_ARG(QVariant, QVariant("OIL_TEMP"))));
        rows = picker->property("_rows").toList();
        QCOMPARE(rows.at(0).toMap().value("label").toString(), QStringLiteral("Recent"));
        QCOMPARE(rows.at(1).toMap().value("pid").toString(), QStringLiteral("OIL_TEMP"));
        // Only temperatures for a widget that takes them
        picker->setProperty("supportedKinds", QStringList{"temperature"});
        for (const QVariant &r : picker->property("_rows").toList())
            if (!r.toMap().value("header").toBool())
                QVERIFY(ParameterCatalog::find(r.toMap().value("pid").toString())->kind == "temperature");
    }

    // "+ New": a live card per template; a tap hands over the filled spec;
    // the demo data it ran on is put back as it was
    void templateGalleryPicks()
    {
        QQmlComponent component(engine.get());
        component.setData(QString(R"qml(
            import QtQuick
            import QtQuick.Controls
            import "%1/dashboards/editor" as Editor
            ApplicationWindow {
                width: 1280; height: 720; visible: true
                property alias gallery: g
                property var picked: null
                Editor.TemplateGallery { id: g; onTemplatePicked: function(spec) { picked = spec } }
            }
        )qml").arg(QUrl::fromLocalFile(frontend).toString()).toUtf8(), QUrl());
        root.reset(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QObject *gallery = qvariant_cast<QObject *>(root->property("gallery"));
        QVERIFY(QMetaObject::invokeMethod(gallery, "open"));
        QTRY_VERIFY(gallery->property("opened").toBool());
        QTRY_VERIFY(findVisual(window->contentItem(), "templateCard_hero-4"));
        QVERIFY(findVisual(window->contentItem(), "templateCard_blank"));
        auto *card = findVisual(window->contentItem(), "templateCard_two-up");
        QVERIFY(card);
        QTRY_VERIFY(card->width() > 0 && card->height() > card->width() * 0.62);   // laid out
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          card->mapToScene(QPointF(card->width() / 2, card->height() / 2)).toPoint());
        QTRY_VERIFY(!gallery->property("opened").toBool());
        const QVariantMap spec = root->property("picked").toMap();
        QCOMPARE(spec.value("label").toString(), QStringLiteral("2 up"));
        QCOMPARE(spec.value("cells").toList().size(), 2);
        QCOMPARE(spec.value("cells").toList().at(0).toMap().value("paramId").toString(), QStringLiteral("RPM"));
    }

    // Phase 3: the portrait layout in the renderer and the editor, the
    // polling-cost numbers, the driving lock
    void portraitLayoutsAndDrivingLock()
    {
        // Renderer: an upright area lays the cells out by layouts.portrait
        QVERIFY(load("dashboards/DashboardRenderer.qml"));
        QFile sport(frontend + "/dashboards/presets/sport.json");
        QVERIFY(sport.open(QIODevice::ReadOnly));
        QVariantMap spec = dashboards->normalizedSpec(QJsonDocument::fromJson(sport.readAll()).object().toVariantMap());
        // The grids only: no widgets loading while the shape flips
        spec["cells"] = QVariantList();
        QVariantMap layouts = spec.value("layouts").toMap();
        QVariantMap portraitLayout = layouts.value("portrait").toMap();
        portraitLayout["cells"] = QVariantList();
        layouts["portrait"] = portraitLayout;
        spec["layouts"] = layouts;
        root->setProperty("width", 400);
        root->setProperty("height", 800);
        root->setProperty("spec", spec);
        QVERIFY(root->property("portrait").toBool());
        QCOMPARE(root->property("_cols").toInt(), 8);
        QCOMPARE(root->property("_rows").toInt(), 12);
        root->setProperty("width", 1200);
        root->setProperty("height", 600);
        QCOMPARE(root->property("_cols").toInt(), 12);

        QQmlComponent component(engine.get());
        component.setData(QString(R"qml(
            import QtQuick
            import QtQuick.Controls
            import "%1/dashboards/editor" as Editor
            ApplicationWindow {
                width: 1280; height: 720; visible: true
                property alias editor: ed
                Editor.DashboardEditor { id: ed; anchors.fill: parent; autoDensity: false }
            }
        )qml").arg(QUrl::fromLocalFile(frontend).toString()).toUtf8(), QUrl());
        root.reset(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QObject *editor = qvariant_cast<QObject *>(root->property("editor"));
        auto cellAt = [editor](int i) { return editor->property("cells").toList().at(i).toMap(); };
        auto rect = [](const QVariantMap &c) {
            return QStringLiteral("%1,%2 %3x%4").arg(c.value("col").toInt()).arg(c.value("row").toInt())
                .arg(c.value("colSpan").toInt()).arg(c.value("rowSpan").toInt());
        };

        QVERIFY(invoke(editor, "addForParam", {"RPM"}).toBool());
        QVERIFY(invoke(editor, "addForParam", {"COOLANT_TEMP"}).toBool());
        const QString landRpm = rect(cellAt(0));

        // Polling cost: two values from the car share the adapter
        QCOMPARE(editor->property("polledCount").toInt(), 2);
        QCOMPARE(editor->property("updatesPerValue").toDouble(), 7.5);

        // Portrait: derived the first time, on the turned grid, in an
        // upright canvas
        invoke(editor, "setLayoutMode", {"portrait"});
        QCOMPARE(editor->property("layoutMode").toString(), QStringLiteral("portrait"));
        QCOMPARE(editor->property("gridColumns").toInt(), 6);
        QCOMPARE(editor->property("gridRows").toInt(), 12);
        auto *canvas = findVisual(window->contentItem(), "editorCanvas");
        QTRY_VERIFY(canvas->height() > canvas->width());
        QCOMPARE(cellAt(0).value("paramId").toString(), QStringLiteral("RPM"));
        // Moving in portrait leaves the landscape layout alone...
        const int derivedRow = cellAt(0).value("row").toInt();
        invoke(editor, "moveCell", {0, 0, 99});   // to the bottom (clamped)
        const QString portRpm = rect(cellAt(0));
        QVERIFY(cellAt(0).value("row").toInt() > derivedRow);
        // ...and an option set here is the widget's in both
        invoke(editor, "setCellProp", {0, "critAbove", 6000});
        invoke(editor, "setLayoutMode", {"landscape"});
        QCOMPARE(editor->property("gridColumns").toInt(), 12);
        QCOMPARE(rect(cellAt(0)), landRpm);
        QCOMPARE(cellAt(0).value("props").toMap().value("critAbove").toInt(), 6000);
        // A widget added in landscape gets a place in portrait too
        QVERIFY(invoke(editor, "addForParam", {"SPEED"}).toBool());
        invoke(editor, "setLayoutMode", {"portrait"});
        QCOMPARE(editor->property("cells").toList().size(), 3);
        QCOMPARE(rect(cellAt(0)), portRpm);
        for (int i = 0; i < 3; ++i)
            QVERIFY(dashboards->canPlace(editor->property("cells").toList(), 6, 12, cellAt(i).value("col").toInt(),
                                         cellAt(i).value("row").toInt(), cellAt(i).value("colSpan").toInt(),
                                         cellAt(i).value("rowSpan").toInt(), i));
        // Undo walks back through the switch
        invoke(editor, "undo");
        QCOMPARE(editor->property("layoutMode").toString(), QStringLiteral("landscape"));

        // What is saved: landscape cells, a placement each in portrait
        invoke(editor, "redo");
        QVariantMap saved = invoke(editor, "specToSave").toMap();
        QCOMPARE(saved.value("schema").toInt(), 2);
        QCOMPARE(saved.value("gridColumns").toInt(), 12);
        QCOMPARE(rect(saved.value("cells").toList().at(0).toMap()), landRpm);
        const QVariantMap portrait = saved.value("layouts").toMap().value("portrait").toMap();
        QCOMPARE(portrait.value("cells").toList().size(), 3);
        QCOMPARE(rect(portrait.value("cells").toList().at(0).toMap()), portRpm);
        QVERIFY(!saved.value("cells").toList().at(0).toMap().contains("_uid"));
        saved.insert("label", "Round trip");
        const QVariantMap normal = dashboards->normalizedSpec(saved);
        QCOMPARE(normal.value("layouts").toMap().value("portrait").toMap().value("cells").toList().size(), 3);

        // Driving lock: moving, nothing changes; a passenger can edit
        store->ingest("SPEED", 30);   // MPH
        QTRY_VERIFY(editor->property("drivingLocked").toBool());
        QTRY_VERIFY(findVisual(window->contentItem(), "editorDrivingLock")->isVisible());
        QVERIFY(!canvas->property("interactive").toBool());
        invoke(editor, "addForParam", {"MAF"});   // refused: "Editing is paused..."
        QCOMPARE(editor->property("cells").toList().size(), 3);
        editor->setProperty("passengerOverride", true);
        QVERIFY(!editor->property("drivingLocked").toBool());
        QVERIFY(canvas->property("interactive").toBool());
        editor->setProperty("passengerOverride", false);
        store->ingest("SPEED", 0);
        QTRY_VERIFY(!editor->property("drivingLocked").toBool());
    }

    void alertBannerAndAutoShow()
    {
        // The renderer names a widget at its critical limit, unless the
        // spec turns the banner off
        QVERIFY(load("dashboards/DashboardRenderer.qml"));
        QVariantMap cell{{"type", "DigitalReadout"}, {"paramId", "COOLANT_TEMP"}, {"col", 0}, {"row", 0},
                         {"colSpan", 1}, {"rowSpan", 1}, {"props", QVariantMap{{"critAbove", 110}, {"warnAbove", 100}}}};
        QVariantMap spec{{"gridColumns", 2}, {"gridRows", 1}, {"cells", QVariantList{cell}}};
        root->setProperty("spec", spec);
        store->ingest("COOLANT_TEMP", 105);   // a warning: the gauge's, not the banner's
        QTest::qWait(80);
        QCOMPARE(root->property("activeAlerts").toList().size(), 0);
        store->ingest("COOLANT_TEMP", 120);
        QTRY_COMPARE(root->property("activeAlerts").toList().size(), 1);
        const QString text = root->property("activeAlerts").toList().at(0).toMap().value("text").toString();
        QVERIFY2(text.contains("120") || text.contains("248"), qPrintable(text));   // °C or °F
        spec["alertBanner"] = false;
        root->setProperty("spec", spec);
        QCOMPARE(root->property("activeAlerts").toList().size(), 0);

        // Auto-show: up when the rule starts to hold, back when it has
        // stopped for clearChecks checks, once per crossing
        QQmlComponent rulesComponent(engine.get(), QUrl::fromLocalFile(frontend + "/dashboards/AutoShowRules.qml"));
        std::unique_ptr<QObject> rules(rulesComponent.create());
        QVERIFY2(rules, qPrintable(rulesComponent.errorString()));
        rules->setProperty("clearChecks", 2);
        rules->setProperty("rules", QVariantList{QVariantMap{
            {"id", "hot"}, {"showWhen", QVariantMap{{"paramId", "COOLANT_TEMP"}, {"above", 110}}}}});
        rules->setProperty("activeId", "sport");
        QStringList switched;
        connect(rules.get(), SIGNAL(switchTo(QString)), this, SLOT(noteSwitch(QString)));
        m_switched = &switched;
        store->ingest("COOLANT_TEMP", 90);
        QTRY_COMPARE(store->value("COOLANT_TEMP").toInt(), 90);
        invoke(rules.get(), "check");
        QVERIFY(switched.isEmpty());
        store->ingest("COOLANT_TEMP", 115);
        QTRY_COMPARE(store->value("COOLANT_TEMP").toInt(), 115);
        invoke(rules.get(), "check");
        QCOMPARE(switched, QStringList{"hot"});
        rules->setProperty("activeId", "hot");
        invoke(rules.get(), "check");   // still holds: stays
        store->ingest("COOLANT_TEMP", 95);
        QTRY_COMPARE(store->value("COOLANT_TEMP").toInt(), 95);
        invoke(rules.get(), "check");
        QCOMPARE(switched.size(), 1);
        invoke(rules.get(), "check");
        QCOMPARE(switched, (QStringList{"hot", "sport"}));
        // Swiped away while it holds: it doesn't come back until it crosses again
        rules->setProperty("activeId", "sport");
        store->ingest("COOLANT_TEMP", 115);
        QTRY_COMPARE(store->value("COOLANT_TEMP").toInt(), 115);
        invoke(rules.get(), "check");
        QCOMPARE(switched.size(), 3);
        rules->setProperty("activeId", "minimal");   // the user's pick
        invoke(rules.get(), "check");
        QCOMPARE(switched.size(), 3);
        QCOMPARE(rules->property("shownId").toString(), QString());
        m_switched = nullptr;
    }

    void importReportStyleAndOptions()
    {
        QTemporaryDir tmp;
        dashboards->setUserDir(tmp.path() + "/dashboards");
        QQmlComponent component(engine.get());
        component.setData(QString(R"qml(
            import QtQuick
            import QtQuick.Controls
            import "%1/dashboards" as Dashboards
            import "%1/dashboards/editor" as Editor
            ApplicationWindow {
                width: 1280; height: 720; visible: true
                property alias editor: ed
                property alias report: rep
                Editor.DashboardEditor { id: ed; anchors.fill: parent; autoDensity: false }
                Dashboards.ImportReportPopup { id: rep }
            }
        )qml").arg(QUrl::fromLocalFile(frontend).toString()).toUtf8(), QUrl());
        root.reset(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));

        // Import report, with no car connected
        QObject *report = qvariant_cast<QObject *>(root->property("report"));
        const QVariantList lines = invoke(report, "lines", {QVariantMap{
            {"id", "x"}, {"label", "X"}, {"cells", 3}, {"paramIds", QStringList{"RPM", "NOT_A_PID"}},
            {"unknownTypes", QStringList{"FancyGauge"}}, {"droppedCells", QStringList{"BarGauge"}},
            {"droppedProps", 2}, {"movedCells", 1}, {"portraitDropped", false}}}).toList();
        QStringList texts;
        for (const QVariant &l : lines) texts << l.toMap().value("text").toString();
        QCOMPARE(texts.size(), 6);
        QVERIFY2(texts[0].startsWith("No car connected"), qPrintable(texts[0]));
        QVERIFY2(texts[1].startsWith("1 value this version of OCTAVE doesn't know") && texts[1].contains("NOT_A_PID"),
                 qPrintable(texts[1]));
        QVERIFY2(texts[2].contains("FancyGauge"), qPrintable(texts[2]));
        QVERIFY2(texts[3].startsWith("1 widget was left out"), qPrintable(texts[3]));
        QVERIFY2(texts[4].startsWith("2 settings"), qPrintable(texts[4]));
        QVERIFY2(texts[5].startsWith("1 widget was moved"), qPrintable(texts[5]));
        QCOMPARE(invoke(report, "lines", {QVariantMap{{"id", "y"}, {"paramIds", QStringList{}}}}).toList().size(), 0);

        // Style: the look without the data, across types, to every one of a type
        QObject *editor = qvariant_cast<QObject *>(root->property("editor"));
        auto props = [editor](int i) { return editor->property("cells").toList().at(i).toMap().value("props").toMap(); };
        QVERIFY(invoke(editor, "addCellAt", {"CircularGauge", -1, -1}).toBool());
        QVERIFY(invoke(editor, "addCellAt", {"CircularGauge", -1, -1}).toBool());
        QVERIFY(invoke(editor, "addCellAt", {"DigitalReadout", -1, -1}).toBool());
        invoke(editor, "setCellProp", {0, "showTicks", false});
        invoke(editor, "setCellProp", {0, "critAbove", 6000});
        invoke(editor, "setCellProps", {0, QStringList{"fillColor", "needleColor"}, "#ff0000"});
        invoke(editor, "setCellProp", {1, "majorTickCount", 5});
        editor->setProperty("selectedIndex", 0);
        QCOMPARE(editor->property("sameTypeCount").toInt(), 1);
        invoke(editor, "copyStyle", {0});
        invoke(editor, "pasteStyle", {2});
        QCOMPARE(props(2).value("valueColor").toString(), QStringLiteral("#ff0000"));
        QVERIFY(!props(2).contains("showTicks"));
        QVERIFY(!props(2).contains("critAbove"));
        QCOMPARE(invoke(editor, "applyStyleToAll", {0}).toInt(), 1);
        QCOMPARE(props(1).value("showTicks"), QVariant(false));
        QCOMPARE(props(1).value("needleColor").toString(), QStringLiteral("#ff0000"));
        QVERIFY(!props(1).contains("majorTickCount"));   // replaced by the source's (unset)
        QVERIFY(!props(1).contains("critAbove"));        // data stays the widget's own
        QCOMPARE(invoke(editor, "applyStyleToAll", {0}).toInt(), 0);
        invoke(editor, "undo");
        QCOMPARE(props(1).value("majorTickCount").toInt(), 5);

        // My widgets: saved from a widget, placed again with its options
        const int before = editor->property("cells").toList().size();
        QCOMPARE(invoke(editor, "saveAsPreset", {0, "Red dial"}).toString(), QStringLiteral("red-dial"));
        QCOMPARE(dashboards->widgetPresets().size(), 1);
        const QVariantMap preset = dashboards->widgetPresets().at(0).toMap();
        QCOMPARE(preset.value("props").toMap().value("critAbove").toInt(), 6000);
        QVERIFY(invoke(editor, "addPreset", {preset, -1, -1}).toBool());
        QCOMPARE(editor->property("cells").toList().size(), before + 1);
        const QVariantMap placed = editor->property("cells").toList().last().toMap();
        QCOMPARE(placed.value("type").toString(), QStringLiteral("CircularGauge"));
        QCOMPARE(placed.value("props").toMap().value("fillColor").toString(), QStringLiteral("#ff0000"));
        // ...listed first in the gallery, under Mine; a tap places it
        invoke(editor, "openPalette", {-1, -1});
        auto *palette = root->findChild<QObject *>("widgetPalette");
        QTRY_VERIFY(palette->property("opened").toBool());
        QTRY_VERIFY(findVisual(window->contentItem(), "palettePreset_red-dial"));
        QVERIFY(findVisual(window->contentItem(), "paletteCategory_Mine"));
        QSignalSpy presetPicked(palette, SIGNAL(presetPicked(QVariant)));
        // Looked up again each time: the gallery rebuilds its cards while it opens
        auto mineCard = [&]() { return findVisual(window->contentItem(), "palettePreset_red-dial"); };
        QTRY_VERIFY(mineCard() && mineCard()->width() > 0 && mineCard()->height() > 0 && mineCard()->isVisible());
        QTest::qWait(300);   // the popup's enter transition
        auto *mine = mineCard();
        QVERIFY(mine);
        const QPointF at = mine->mapToScene(QPointF(mine->width() / 2, mine->height() * 0.75));
        // (the card goes with the gallery, which closes on the tap)
        const QString where = QStringLiteral("tap at %1,%2 on %3x%4").arg(at.x()).arg(at.y())
                                  .arg(mine->width()).arg(mine->height());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at.toPoint());
        QVERIFY2(presetPicked.size() == 1, qPrintable(where));
        QCOMPARE(editor->property("cells").toList().size(), before + 2);
        invoke(editor, "undo");

        // Dashboard options go into what is saved
        invoke(editor, "setShowWhen", {QVariantMap{{"paramId", "COOLANT_TEMP"}, {"above", 105}}});
        invoke(editor, "setAlertBanner", {false});
        QVERIFY(editor->property("dirty").toBool());
        QVariantMap saved = invoke(editor, "specToSave").toMap();
        QCOMPARE(saved.value("showWhen").toMap(), (QVariantMap{{"paramId", "COOLANT_TEMP"}, {"above", 105}}));
        QCOMPARE(saved.value("alertBanner"), QVariant(false));
        invoke(editor, "setShowWhen", {QVariant()});
        invoke(editor, "setAlertBanner", {true});
        saved = invoke(editor, "specToSave").toMap();
        QVERIFY(!saved.contains("showWhen"));
        QVERIFY(!saved.contains("alertBanner"));
    }

public slots:
    void noteSwitch(const QString &id) { if (m_switched) m_switched->append(id); }

private:
    QStringList *m_switched = nullptr;
    // A QML method of `obj` with up to three arguments, its result as a variant
    static QVariant invoke(QObject *obj, const char *method, const QVariantList &args = {})
    {
        QVariant ret;
        bool ok = false;
        switch (args.size()) {
        case 0: ok = QMetaObject::invokeMethod(obj, method, Q_RETURN_ARG(QVariant, ret)); break;
        case 1: ok = QMetaObject::invokeMethod(obj, method, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, args[0])); break;
        case 2: ok = QMetaObject::invokeMethod(obj, method, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, args[0]),
                                               Q_ARG(QVariant, args[1])); break;
        default: ok = QMetaObject::invokeMethod(obj, method, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, args[0]),
                                                Q_ARG(QVariant, args[1]), Q_ARG(QVariant, args[2])); break;
        }
        if (!ok)
            qWarning() << "no" << method << "with" << args.size() << "args";
        return ret;
    }

    bool load(const QString &file)
    {
        QQmlComponent component(engine.get(), QUrl::fromLocalFile(frontend + '/' + file));
        root.reset(component.create());
        if (!root)
            qWarning().noquote() << component.errorString();
        if (root) {
            root->setProperty("width", 400);
            root->setProperty("height", 120);
        }
        return bool(root);
    }

    static bool hasText(QQuickItem *item, const QString &text)
    {
        if (item->isVisible() && item->property("text").toString().startsWith(text))
            return true;
        for (auto *child : item->childItems())
            if (hasText(child, text))
                return true;
        return false;
    }

    void checkBounds(QQuickItem *widget, QQuickItem *item)
    {
        if (!item->isVisible())
            return;
        if (item->property("text").isValid() && !item->property("text").toString().isEmpty()) {
            // The painted text, in the widget's coordinates (scale included)
            const QRectF r = item->mapRectToItem(widget, QRectF(0, 0, item->width(), item->height()));
            QVERIFY2(r.left() >= -1 && r.top() >= -1
                     && r.right() <= widget->width() + 1 && r.bottom() <= widget->height() + 1,
                     qPrintable(QString("%1 at %2,%3 %4x%5 in %6x%7").arg(item->property("text").toString())
                                .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height())
                                .arg(widget->width()).arg(widget->height())));
        }
        for (auto *child : item->childItems())
            checkBounds(widget, child);
    }

    std::unique_ptr<ParameterCatalog> catalog;
    std::unique_ptr<ParameterStore> store;
    std::unique_ptr<ThemeCatalog> themes;
    std::unique_ptr<DashboardManager> dashboards;
    std::unique_ptr<QQmlPropertyMap> settings;
    std::unique_ptr<QQmlEngine> engine;
    std::unique_ptr<QObject> root;
};

QTEST_MAIN(TstDashboardWidgets)
#include "tst_dashboardwidgets.moc"
