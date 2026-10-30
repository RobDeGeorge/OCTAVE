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
        for (const auto &file : {QString("gauges/SegmentedGauge.qml"), QString("dashboards/widgets/ClockWidget.qml")})
            for (const QSize size : {QSize(160, 50), QSize(80, 32), QSize(320, 100)})
                QTest::newRow(qPrintable(file + QString::number(size.width()))) << file << size;
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
        if (file.contains("Segmented")) {
            item->setProperty("title", "A deliberately long dashboard label");
            item->setProperty("value", 123456.789);
            item->setProperty("unit", "kPa");
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

    void checkBounds(QQuickItem *widget, QQuickItem *item)
    {
        if (!item->isVisible())
            return;
        if (item->property("text").isValid()) {
            const QPointF pos = item->mapToItem(widget, QPointF());
            QVERIFY2(pos.x() >= -1 && pos.y() >= -1
                     && pos.x() + item->width() <= widget->width() + 1
                     && pos.y() + item->height() <= widget->height() + 1,
                     qPrintable(item->property("text").toString()));
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
