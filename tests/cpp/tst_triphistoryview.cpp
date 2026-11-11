// TripHistory.qml's Trends view against a fake tripRecorder: switching
// views asks for the trends, the first number that needs a look is picked,
// the window chips re-ask (an answer for another window is ignored), a
// parameter chip asks for that parameter, a tap on a chart point opens the
// drive, and a window with no drives says so.

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUrl>

#include <memory>

#include "managers/parametercatalog.h"
#include "managers/themecatalog.h"

class FakeRecorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ yes CONSTANT)
    Q_PROPERTY(bool storeReady READ yes CONSTANT)
    Q_PROPERTY(bool recording READ no CONSTANT)
    Q_PROPERTY(qint64 currentTripId READ zero CONSTANT)
    Q_PROPERTY(int currentSampleCount READ zeroInt CONSTANT)
    Q_PROPERTY(int currentTripElapsedS READ zeroInt CONSTANT)
    Q_PROPERTY(QVariantList trips MEMBER tripRows NOTIFY tripsChanged)
    Q_PROPERTY(qint64 storageBytes READ zero CONSTANT)
    Q_PROPERTY(QString lastError READ empty CONSTANT)
    Q_PROPERTY(bool canOpenFolder READ no CONSTANT)

public:
    QVariantList tripRows;
    QList<int> trendRequests;                     // days, in order
    QList<QPair<QString, int>> paramRequests;     // (paramId, days)
    QList<qint64> detailRequests;

    bool yes() const { return true; }
    bool no() const { return false; }
    qint64 zero() const { return 0; }
    int zeroInt() const { return 0; }
    QString empty() const { return QString(); }

    Q_INVOKABLE void refresh() {}
    Q_INVOKABLE void requestDetail(qint64 id) { detailRequests << id; }
    Q_INVOKABLE void requestSeries(qint64, const QString &, int = 400, double = -1, double = -1) {}
    Q_INVOKABLE void requestHealthTrends(int days = 30) { trendRequests << days; }
    Q_INVOKABLE void requestParamTrend(const QString &id, int days = 30) { paramRequests << qMakePair(id, days); }
    Q_INVOKABLE void endTripNow() {}

signals:
    void tripsChanged();
    void detailReady(qint64 id, const QVariantMap &detail);
    void seriesReady(qint64 id, const QString &paramId, const QVariantList &points);
    void tripEnded(qint64 id, const QVariantMap &summary);
    void liveTripChanged(qint64 id);
    void healthTrendsReady(int days, const QVariantMap &result);
    void paramTrendReady(const QString &paramId, int days, const QVariantList &rows);
};

namespace {
const QString kFrontend = QStringLiteral(OCTAVE_SOURCE_DIR "/frontend");
const qint64 kDay = 86'400'000LL;
const qint64 kNow = 1'760'000'000'000LL;

const char *kQml = R"qml(
import QtQuick
import QtQuick.Controls
import "%1" as App

ApplicationWindow {
    id: win
    width: 1280; height: 720; visible: true
    StackView {
        id: stack
        anchors.fill: parent
        initialItem: App.TripHistory { stackView: stack; mainWindow: win }
    }
    readonly property var page: stack.currentItem
}
)qml";

QVariantMap trip(qint64 startMs)
{
    return {{"id", startMs}, {"startMs", startMs}, {"endMs", startMs + 1'200'000}, {"durationS", 1200.0},
            {"open", false}, {"clean", true}, {"distanceMiles", 10.0}, {"maxSpeed", 60.0}, {"sampleCount", 100}};
}

QVariantMap metric(const QString &id, const QString &status, const QVariantList &rows)
{
    return {{"id", id}, {"title", id}, {"unit", "V"}, {"decimals", 1}, {"description", "d"},
            {"rows", rows}, {"status", status}, {"note", status == "watch" ? "look at it" : "fine"},
            {"recent", rows.isEmpty() ? QVariant() : rows.last().toMap().value("v")}};
}

QVariantMap result(int days, int tripCount, const QVariantList &rows)
{
    return {{"days", days}, {"toMs", kNow}, {"tripCount", tripCount},
            {"metrics", QVariantList{metric("idleVoltage", "ok", rows), metric("settledCoolant", "watch", rows),
                                     metric("ltft2", "none", {})}},
            {"params", QVariantList{QVariantMap{{"id", "COOLANT_TEMP"}, {"title", "Coolant Temp"}, {"unit", "°C"}, {"trips", tripCount}}}}};
}
} // namespace

class TstTripHistoryView : public QObject
{
    Q_OBJECT

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<FakeRecorder> m_fake;
    std::unique_ptr<ThemeCatalog> m_themes;
    std::unique_ptr<ParameterCatalog> m_catalog;
    std::unique_ptr<QObject> m_root;
    QVariantList m_rows;

    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(m_root.get()); }
    QObject *page() const { return qvariant_cast<QObject *>(m_root->property("page")); }

    // Delegates (Repeater, ListView) are only in the visual tree
    static QQuickItem *findIn(QQuickItem *item, const QString &name)
    {
        if (!item)
            return nullptr;
        if (item->objectName() == name)
            return item;
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *hit = findIn(child, name))
                return hit;
        }
        return nullptr;
    }
    QQuickItem *find(const QString &name) const { return findIn(window()->contentItem(), name); }

    // Look the item up again on every try: a ListView may re-create its
    // delegates while events are processed, so a held pointer can dangle
    void tap(const QString &name, QPointF at = QPointF(-1, -1))
    {
        QQuickItem *item = nullptr;
        // A view that has just turned visible is laid out on the next polish
        QTRY_VERIFY2((item = find(name)) && item->width() > 0 && item->height() > 0, qPrintable(name));
        if (at.x() < 0)
            at = QPointF(item->width() / 2, item->height() / 2);
        QTest::mouseClick(window(), Qt::LeftButton, {}, item->mapToScene(at).toPoint());
    }

private slots:
    void init()
    {
        m_engine = std::make_unique<QQmlEngine>();
        m_engine->addImportPath(kFrontend);
        m_fake = std::make_unique<FakeRecorder>();
        // Three drives, a day apart, the newest a day before "now"
        m_rows.clear();
        for (int i = 0; i < 3; ++i) {
            const qint64 start = kNow - (3 - i) * kDay;
            m_fake->tripRows.prepend(trip(start));
            m_rows.append(QVariantMap{{"id", start}, {"startMs", start}, {"v", 13.5 + i * 0.2}});
        }
        m_engine->rootContext()->setContextProperty("tripRecorder", m_fake.get());
        m_engine->rootContext()->setContextProperty("settingsManager", QVariant::fromValue<QObject *>(nullptr));
        m_themes = std::make_unique<ThemeCatalog>(kFrontend + "/themes.json");
        m_engine->rootContext()->setContextProperty("themeCatalog", m_themes.get());
        m_catalog = std::make_unique<ParameterCatalog>();
        m_engine->rootContext()->setContextProperty("parameterCatalog", m_catalog.get());
        QQmlComponent comp(m_engine.get());
        comp.setData(QString::fromLatin1(kQml).arg(QUrl::fromLocalFile(kFrontend).toString()).toUtf8(), QUrl());
        m_root.reset(comp.create());
        QVERIFY2(m_root, qPrintable(comp.errorString()));
        QVERIFY(QTest::qWaitForWindowExposed(window()));
    }

    void cleanup()
    {
        m_root.reset();
        m_engine.reset();
        m_fake.reset();
    }

    void switchingToTrendsAsksAndPicksTheFlaggedNumber()
    {
        QCOMPARE(page()->property("view").toString(), QStringLiteral("drives"));
        QVERIFY(m_fake->trendRequests.isEmpty());   // nothing asked until Trends is opened
        tap("tripView_trends");
        QCOMPARE(page()->property("view").toString(), QStringLiteral("trends"));
        QCOMPARE(m_fake->trendRequests, QList<int>{30});
        emit m_fake->healthTrendsReady(30, result(30, 3, m_rows));
        QCOMPARE(page()->property("trendId").toString(), QStringLiteral("settledCoolant"));
        QTRY_VERIFY(find("tripTrendMetric_idleVoltage") && find("tripTrendMetric_idleVoltage")->isVisible());
        QVERIFY(find("tripTrendPanel")->isVisible());
        QCOMPARE(page()->property("trendRows").toList().size(), 3);

        // Another number, from its card
        tap("tripTrendMetric_idleVoltage");
        QCOMPARE(page()->property("trendId").toString(), QStringLiteral("idleVoltage"));
    }

    void windowChipsAskAgainAndStaleAnswersAreIgnored()
    {
        tap("tripView_trends");
        emit m_fake->healthTrendsReady(30, result(30, 3, m_rows));
        QTRY_VERIFY(find("tripTrendDays_90"));
        tap("tripTrendDays_90");
        QCOMPARE(page()->property("trendDays").toInt(), 90);
        QCOMPARE(m_fake->trendRequests.last(), 90);
        // The 30-day answer arriving late does not replace the 90-day view
        emit m_fake->healthTrendsReady(30, result(30, 1, m_rows.mid(2)));
        QCOMPARE(page()->property("trends").toMap().value("tripCount").toInt(), 3);
        emit m_fake->healthTrendsReady(90, result(90, 3, m_rows));
        QCOMPARE(page()->property("trends").toMap().value("days").toInt(), 90);
    }

    void parameterChipAsksForThatParameter()
    {
        tap("tripView_trends");
        emit m_fake->healthTrendsReady(30, result(30, 3, m_rows));
        QTRY_VERIFY(find("tripTrendParam_COOLANT_TEMP"));
        tap("tripTrendParam_COOLANT_TEMP");
        QCOMPARE(page()->property("trendKind").toString(), QStringLiteral("param"));
        QCOMPARE(m_fake->paramRequests.size(), 1);
        QCOMPARE(m_fake->paramRequests.first().first, QStringLiteral("COOLANT_TEMP"));
        QCOMPARE(m_fake->paramRequests.first().second, 30);
        QVariantList rows;
        for (const QVariant &r : m_rows) {
            QVariantMap m = r.toMap();
            m["lo"] = 20.0;
            m["hi"] = 90.0;
            rows << m;
        }
        emit m_fake->paramTrendReady("OIL_TEMP", 30, rows);        // not the one shown
        QCOMPARE(page()->property("trendRows").toList().size(), 0);
        emit m_fake->paramTrendReady("COOLANT_TEMP", 30, rows);
        QCOMPARE(page()->property("trendRows").toList().size(), 3);
        // Changing the window re-asks for the parameter too
        tap("tripTrendDays_7");
        QCOMPARE(m_fake->paramRequests.last().second, 7);
    }

    void tappingAPointOpensThatDrive()
    {
        tap("tripView_trends");
        emit m_fake->healthTrendsReady(30, result(30, 3, m_rows));
        QQuickItem *chart = nullptr;
        QTRY_VERIFY((chart = find("tripTrendChart")) && chart->width() > 100);
        const qint64 middle = m_rows.at(1).toMap().value("startMs").toLongLong();
        QVariant xv;
        QVERIFY(QMetaObject::invokeMethod(chart, "xOf", Q_RETURN_ARG(QVariant, xv), Q_ARG(QVariant, QVariant(double(middle)))));
        const double x = xv.toDouble();
        m_fake->detailRequests.clear();
        tap("tripTrendChart", QPointF(x, chart->height() / 2));
        QCOMPARE(page()->property("view").toString(), QStringLiteral("drives"));
        QCOMPARE(qvariant_cast<QVariantMap>(page()->property("selectedTrip")).value("id").toLongLong(), middle);
        QVERIFY(m_fake->detailRequests.contains(middle));
    }

    void aWindowWithoutDrivesSaysSo()
    {
        tap("tripView_trends");
        emit m_fake->healthTrendsReady(30, result(30, 0, {}));
        QQuickItem *list = find("tripTrendMetrics");
        QVERIFY(list);
        QCOMPARE(list->property("count").toInt(), 0);
        QVERIFY(find("tripTrendMetric_idleVoltage") == nullptr);
    }

    // Side by side in a wide window; the list above the drive in one taller
    // than wide, each as wide as the page (0.9.6: the page had no upright
    // layout, the list was a 36 % sliver of a phone's width)
    void theTwoPanelsStackWhenUpright()
    {
        QQuickItem *list = find("tripListCard");
        QQuickItem *detail = find("tripDetailCard");
        QQuickItem *layout = find("tripHistoryLayout");
        QVERIFY(list && detail && layout);
        QTRY_VERIFY(detail->width() > 0);
        QVERIFY(detail->x() >= list->x() + list->width());
        QCOMPARE(detail->y(), list->y());
        QVERIFY(list->width() < layout->width() / 2);

        window()->resize(480, 900);
        QTRY_VERIFY(layout->height() > layout->width() && detail->y() >= list->y() + list->height());
        QCOMPARE(detail->x(), list->x());
        QCOMPARE(list->width(), layout->width());
        QCOMPARE(detail->width(), layout->width());
        QVERIFY(list->height() >= 150);
        QVERIFY(detail->height() > list->height());

        window()->resize(1280, 720);
        QTRY_VERIFY(detail->x() >= list->x() + list->width() && detail->y() == list->y());
    }
};

QTEST_MAIN(TstTripHistoryView)
#include "tst_triphistoryview.moc"
