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
        QCOMPARE(root->property("alertLevel").toInt(), 0);
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

    // The hold: a level is left once the value is back inside its limit by
    // 1 % of the gauge's range, whatever that range is. It was "at least one
    // whole unit" for a gauge that sets no decimals, so a lamp on a 0 to 1 V
    // sensor never went out again.
    void alertHoldScalesWithTheRange()
    {
        // A lamp on an oxygen sensor (0 to 1 V): the hold is 0.01 V
        QVERIFY(load("gauges/WarningLight.qml"));
        root->setProperty("paramId", "O2_B1S1");
        root->setProperty("triggerAbove", 0.8);
        root->setProperty("value", 0.5);
        QVERIFY(!root->property("_active").toBool());
        root->setProperty("value", 0.8);
        QVERIFY(root->property("_active").toBool());
        root->setProperty("value", 0.795);   // back under the limit, inside the hold
        QVERIFY(root->property("_active").toBool());
        root->setProperty("value", 0.78);    // under 0.8 - 0.01
        QVERIFY(!root->property("_active").toBool());

        // A readout of the system voltage (10 to 15 V): the hold is 0.05 V
        QVERIFY(load("gauges/DigitalReadout.qml"));
        root->setProperty("paramId", "CONTROL_MODULE_VOLTAGE");
        root->setProperty("critBelow", 11.5);
        root->setProperty("value", 12.0);
        QCOMPARE(root->property("alertLevel").toInt(), 0);
        root->setProperty("value", 11.4);
        QCOMPARE(root->property("alertLevel").toInt(), 2);
        root->setProperty("value", 11.53);   // over the limit, inside the hold
        QCOMPARE(root->property("alertLevel").toInt(), 2);
        root->setProperty("value", 11.6);    // over 11.5 + 0.05
        QCOMPARE(root->property("alertLevel").toInt(), 0);
        // alertHysteresis still wins, and 0 turns the hold off
        root->setProperty("alertHysteresis", 0.0);
        root->setProperty("value", 11.4);
        QCOMPARE(root->property("alertLevel").toInt(), 2);
        root->setProperty("value", 11.51);
        QCOMPARE(root->property("alertLevel").toInt(), 0);

        // The dashboard's red strip and the gauge it names leave critical at
        // the same reading
        QVERIFY(load("dashboards/DashboardRenderer.qml"));
        const QVariantList cells{QVariantMap{{"type", "SegmentedGauge"}, {"paramId", "CONTROL_MODULE_VOLTAGE"},
            {"col", 0}, {"row", 0}, {"colSpan", 6}, {"rowSpan", 2}, {"props", QVariantMap{{"critBelow", 11.5}}}}};
        root->setProperty("spec", QVariantMap{{"schema", 1}, {"cells", cells}});
        auto *item = qobject_cast<QQuickItem *>(root.get());
        QVERIFY(item);
        QTRY_VERIFY(findVisual(item, "segmentedValue"));
        auto *gauge = findVisual(item, "segmentedValue")->parentItem()->parentItem();
        const auto strip = [this] { return root->property("activeAlerts").toList().size(); };
        store->ingest("CONTROL_MODULE_VOLTAGE", 11.4f);
        QTRY_COMPARE(gauge->property("alertLevel").toInt(), 2);
        QTRY_COMPARE(strip(), 1);
        store->ingest("CONTROL_MODULE_VOLTAGE", 11.53f);
        QTRY_VERIFY(qAbs(gauge->property("value").toDouble() - 11.53) < 1e-3);
        QCOMPARE(gauge->property("alertLevel").toInt(), 2);
        QCOMPARE(strip(), 1);
        store->ingest("CONTROL_MODULE_VOLTAGE", 11.6f);
        QTRY_VERIFY(qAbs(gauge->property("value").toDouble() - 11.6) < 1e-3);
        QCOMPARE(gauge->property("alertLevel").toInt(), 0);
        QCOMPARE(strip(), 0);
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

    // An upright area lays the cells out by layouts.portrait
    void rendererPortraitLayout()
    {
        // Renderer: an upright area lays the cells out by layouts.portrait
        QVERIFY(load("dashboards/DashboardRenderer.qml"));
        QFile sport(QStringLiteral(OCTAVE_SOURCE_DIR "/tests/cpp/fixtures/presets/sport.json"));
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
    }

    // Home is a dashboard: what the renderer tells its host
    void rendererTapActionsDemandAndFrames()
    {
        QVERIFY(load("dashboards/DashboardRenderer.qml"));
        const auto cell = [](const QString &type, const QString &paramId, int col, const QVariantMap &props = {}) {
            return QVariantMap{{"type", type}, {"paramId", paramId}, {"col", col}, {"row", 0},
                               {"colSpan", 1}, {"rowSpan", 1}, {"props", props}};
        };
        const QVariantList cells{
            cell("NowPlayingWidget", "", 0), cell("DigitalReadout", "SPEED", 1), cell("DigitalReadout", "PITCH", 2),
            cell("VehicleWidget", "", 3), cell("MediaControlsWidget", "", 4), cell("ShiftLightWidget", "", 5),
            cell("DigitalReadout", "SPEED", 6, {{"tapAction", "media"}}), cell("ClockWidget", "", 7, {{"frame", true}})};
        QVariantMap spec{{"gridColumns", 8}, {"gridRows", 1}, {"margins", 0}, {"spacing", 0}, {"cells", cells}};
        root->setProperty("spec", spec);

        // Off unless the host asks: the OBD menu's dashboards are display only
        QCOMPARE(root->property("interactive").toBool(), false);
        const auto action = [&](int i) {
            QVariant out;
            QMetaObject::invokeMethod(root.get(), "tapActionFor", Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, cells.at(i)));
            return out.toString();
        };
        QCOMPARE(action(0), QStringLiteral("media"));
        QCOMPARE(action(1), QStringLiteral("obd"));
        QCOMPARE(action(2), QStringLiteral("sensors"));   // an IMU reading
        QCOMPARE(action(3), QStringLiteral("vehicle"));
        QCOMPARE(action(4), QStringLiteral("none"));      // its buttons are its own
        QCOMPARE(action(5), QStringLiteral("obd"));
        QCOMPARE(action(6), QStringLiteral("media"));     // the cell's own choice wins
        QCOMPARE(action(7), QStringLiteral("none"));

        // The shift light keeps RPM polled though it binds no PID; each id once
        QCOMPARE(root->property("paramIds").toStringList(), QStringList({"SPEED", "PITCH", "RPM"}));
        QVERIFY(root->property("usesImu").toBool());
        spec["cells"] = QVariantList{cells.at(0), cells.at(1)};
        root->setProperty("spec", spec);
        QVERIFY(!root->property("usesImu").toBool());
        spec["cells"] = QVariantList{cell("GForceGauge", "", 0)};
        root->setProperty("spec", spec);
        QVERIFY(root->property("usesImu").toBool());

        // Where a cell stands, for the host's prompts
        spec["cells"] = cells;
        root->setProperty("spec", spec);
        root->setProperty("width", 800);
        root->setProperty("height", 100);
        QVariant rect;
        QMetaObject::invokeMethod(root.get(), "cellRect", Q_RETURN_ARG(QVariant, rect), Q_ARG(QVariant, 3));
        QCOMPARE(rect.toRectF(), QRectF(300, 0, 100, 100));
    }

    void readoutCardStyle()
    {
        QVERIFY(load("gauges/DigitalReadout.qml"));
        root->setProperty("paramId", "CONTROL_MODULE_VOLTAGE");
        store->ingest("CONTROL_MODULE_VOLTAGE", 14.2f);
        QTRY_COMPARE(root->property("_formatted").toString(), QStringLiteral("14"));
        // The card shows the reading's own decimals
        root->setProperty("card", true);
        QCOMPARE(root->property("_formatted").toString(), QStringLiteral("14.2"));
        root->setProperty("card", false);
        // Never "-0"
        root->setProperty("value", -0.3);
        QCOMPARE(root->property("_formatted").toString(), QStringLiteral("0"));
        root->setProperty("value", -1.3);
        QCOMPARE(root->property("_formatted").toString(), QStringLiteral("-1"));
    }

    void alertBanner()
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

    }

private:
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
