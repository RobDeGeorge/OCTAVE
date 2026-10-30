// Widget behavior against the real catalog/store and renderer, including
// missing samples, converted thresholds, small cells and clock preferences.
#include <QDateTime>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
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
        QVERIFY(root->property("_warning").toBool());
        root->setProperty("value", qQNaN());
        QVERIFY(!root->property("hasValue").toBool());
        QVERIFY(!root->property("_warning").toBool());
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
        QVERIFY(gauge->property("_warning").toBool());
        catalog->setDisplayUnits("F", "mph", "m");
        QTRY_COMPARE(gauge->property("unit").toString(), QStringLiteral("°F"));
        QCOMPARE(gauge->property("warnAbove").toDouble(), 212.0);
        QCOMPARE(gauge->property("value").toDouble(), 212.0);
        QCOMPARE(gauge->property("min").toDouble(), 32.0);
        QVERIFY(gauge->property("_warning").toBool());
    }

    // Every PID gauge: no sample is "--" with nothing drawn and no warning;
    // a real 0 is a value; NaN and infinity count as no sample.
    void missingIsNotZero_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("threshold"); // a warning prop set low enough to fire on 0
        QTest::addColumn<QString>("warningProp");
        QTest::newRow("Circular") << "gauges/CircularGauge.qml" << "" << "";
        QTest::newRow("Arc") << "gauges/ArcGauge.qml" << "" << "";
        QTest::newRow("Bar") << "gauges/BarGauge.qml" << "warnAbove" << "_warning";
        QTest::newRow("Linear") << "gauges/LinearGauge.qml" << "" << "";
        QTest::newRow("Digital") << "gauges/DigitalReadout.qml" << "" << "";
        QTest::newRow("Sparkline") << "gauges/SparklineGauge.qml" << "" << "";
        QTest::newRow("Warning") << "gauges/WarningLight.qml" << "triggerBelow" << "_active";
        QTest::newRow("Segmented") << "gauges/SegmentedGauge.qml" << "warnAbove" << "_warning";
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
                Editor.DashboardEditor { id: ed; anchors.fill: parent }
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

private:
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
