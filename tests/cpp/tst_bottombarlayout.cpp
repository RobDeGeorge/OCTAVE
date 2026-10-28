// frontend/BottomBarLayout.js, the bottom / side bar's geometry: over a grid
// of bar sizes, both orientations and every on/off combination, nothing
// leaves the bar, no two faces or hit areas overlap, every face sits inside
// its own hit area, and the nav buttons keep a usable size whenever the bar
// has room for them.

#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QTest>

namespace {
const QString kLayoutJs = QStringLiteral(OCTAVE_SOURCE_DIR "/frontend/BottomBarLayout.js");

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool visible = false;
};

Rect toRect(const QJSValue &v)
{
    return { v.property("x").toNumber(), v.property("y").toNumber(), v.property("w").toNumber(),
             v.property("h").toNumber(), v.property("visible").toBool() };
}

bool overlaps(const Rect &a, const Rect &b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool inside(const Rect &r, double w, double h)
{
    return r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h;
}

bool contains(const Rect &outer, const Rect &inner)
{
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w
           && inner.y + inner.h <= outer.y + outer.h;
}
} // namespace

class TstBottomBarLayout : public QObject
{
    Q_OBJECT

private:
    QJSEngine m_engine;
    QJSValue m_compute;

    QJSValue compute(bool vertical, int w, int h, int navCount, bool media, bool clock, bool clockAuto,
                     double clockPx = 18, double scale = 1)
    {
        QJSValue p = m_engine.newObject();
        p.setProperty("vertical", vertical);
        p.setProperty("width", w);
        p.setProperty("height", h);
        p.setProperty("navCount", navCount);
        p.setProperty("showMedia", media);
        p.setProperty("showClock", clock);
        p.setProperty("clockAuto", clockAuto);
        p.setProperty("clockPx", clockPx);
        // "12:45 PM" and "100%" in a typical sans at 1 px
        p.setProperty("clockWidthPerPx", 4.3);
        p.setProperty("clockHeightPerPx", 1.17);
        p.setProperty("clockDigitHeightPerPx", 0.72);
        p.setProperty("volumeWidthPerPx", 2.5);
        p.setProperty("volumeHeightPerPx", 1.17);
        p.setProperty("minUnit", 36);
        p.setProperty("scale", scale);
        QJSValue g = m_compute.call({ p });
        if (g.isError())
            qWarning() << g.toString();
        return g;
    }

    // Every promise of BottomBarLayout.js for one result
    void checkInvariants(const QJSValue &g, int w, int h, int navCount, const QByteArray &label)
    {
        QVERIFY2(!g.isError(), label);
        QList<Rect> faces, hits;
        const QStringList keys = { "prev", "play", "next", "shuffle", "mute", "volume", "clock" };
        for (const QString &k : keys) {
            faces << toRect(g.property("items").property(k));
            hits << toRect(g.property("hits").property(k));
        }
        QCOMPARE(g.property("nav").property("length").toInt(), navCount);
        for (int i = 0; i < navCount; ++i) {
            faces << toRect(g.property("nav").property(i));
            hits << toRect(g.property("navHits").property(i));
            QVERIFY2(faces.last().visible, label + " nav hidden");
        }
        for (int i = 0; i < faces.size(); ++i) {
            if (!faces[i].visible)
                continue;
            const QByteArray which = label + " item " + QByteArray::number(i);
            if (w > 0 && h > 0)
                QVERIFY2(faces[i].w > 0 && faces[i].h > 0, which + " empty");
            QVERIFY2(inside(faces[i], w, h), which + " face leaves the bar");
            QVERIFY2(inside(hits[i], w, h), which + " hit area leaves the bar");
            QVERIFY2(contains(hits[i], faces[i]), which + " face outside its hit area");
            for (int j = i + 1; j < faces.size(); ++j) {
                if (!faces[j].visible)
                    continue;
                const QByteArray pair = which + " / " + QByteArray::number(j);
                QVERIFY2(!overlaps(faces[i], faces[j]), pair + " faces overlap");
                QVERIFY2(!overlaps(hits[i], hits[j]), pair + " hit areas overlap");
            }
        }
    }

    void grid(bool vertical, int w0, int w1, int wStep, int h0, int h1, int hStep)
    {
        for (int w = w0; w <= w1; w += wStep)
            for (int h = h0; h <= h1; h += hStep)
                for (int nav : { 5, 7 })
                    for (int flags = 0; flags < 8; ++flags) {
                        const bool media = flags & 1, clock = flags & 2, autoClock = flags & 4;
                        const QJSValue g = compute(vertical, w, h, nav, media, clock, autoClock, 64);
                        checkInvariants(g, w, h, nav,
                                        QByteArray(vertical ? "V " : "H ") + QByteArray::number(w) + "x"
                                            + QByteArray::number(h) + " nav" + QByteArray::number(nav)
                                            + " f" + QByteArray::number(flags));
                        if (QTest::currentTestFailed())
                            return;
                    }
    }

private slots:
    void initTestCase()
    {
        QFile f(kLayoutJs);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QString src = QString::fromUtf8(f.readAll());
        src.remove(QStringLiteral(".pragma library"));
        QJSValue r = m_engine.evaluate(src, kLayoutJs);
        QVERIFY2(!r.isError(), qPrintable(r.toString()));
        m_compute = m_engine.globalObject().property("compute");
        QVERIFY(m_compute.isCallable());
    }

    void horizontalGrid() { grid(false, 320, 3840, 160, 32, 220, 12); }
    void verticalGrid() { grid(true, 40, 320, 12, 240, 2160, 120); }

    // UI scale and absurd inputs (a huge manual clock, zero size) stay contained
    void extremes()
    {
        for (double scale : { 0.5, 1.5, 2.0 })
            for (bool vertical : { false, true }) {
                const int w = vertical ? 128 : 1280, h = vertical ? 800 : 100;
                checkInvariants(compute(vertical, w, h, 7, true, true, true, 18, scale), w, h, 7, "scale");
            }
        checkInvariants(compute(false, 1280, 100, 7, true, true, false, 500), 1280, 100, 7, "big clock H");
        checkInvariants(compute(true, 128, 800, 7, true, true, false, 500), 128, 800, 7, "big clock V");
        checkInvariants(compute(false, 0, 0, 7, true, true, true), 0, 0, 7, "zero");
    }

    // The reference screen (the Jeep's 1280 x 800) shows everything at full size
    void referenceScreenKeepsEverything()
    {
        for (bool vertical : { false, true }) {
            const int w = vertical ? 128 : 1280, h = vertical ? 800 : 100;
            const QJSValue g = compute(vertical, w, h, 7, true, true, true);
            QCOMPARE(g.property("dropped").property("length").toInt(), 0);
            QVERIFY(g.property("items").property("shuffle").property("visible").toBool());
            QVERIFY(g.property("items").property("clock").property("visible").toBool());
            QVERIFY(g.property("unit").toNumber() >= 60);
        }
    }

    // Short of room, the bar drops controls (shuffle first) rather than
    // shrinking the nav buttons below the minimum
    void dropsBeforeShrinkingTooFar()
    {
        const QJSValue g = compute(false, 520, 60, 7, true, true, true);
        QVERIFY(g.property("dropped").property("length").toInt() > 0);
        QCOMPARE(g.property("dropped").property(0).toString(), QStringLiteral("shuffle"));
        QVERIFY(g.property("unit").toNumber() >= 36 - 0.01);
    }

    // Nav buttons are one size and evenly spaced
    void navButtonsUniform()
    {
        for (bool vertical : { false, true }) {
            const int w = vertical ? 110 : 1100, h = vertical ? 700 : 80;
            const QJSValue nav = compute(vertical, w, h, 7, true, true, true).property("nav");
            const Rect first = toRect(nav.property(0)), second = toRect(nav.property(1));
            const double step = vertical ? second.y - first.y : second.x - first.x;
            for (int i = 1; i < 7; ++i) {
                const Rect a = toRect(nav.property(i - 1)), b = toRect(nav.property(i));
                QVERIFY(qAbs(b.w - first.w) <= 1 && qAbs(b.h - first.h) <= 1);
                QVERIFY(qAbs((vertical ? b.y - a.y : b.x - a.x) - step) <= 1);
            }
        }
    }

    // Along the bottom, the media buttons are nav buttons: the same size,
    // the same step from one to the next; the auto clock is as tall
    void mediaButtonsMatchNav()
    {
        for (int w : { 1280, 1600, 1920 }) {
            const QJSValue g = compute(false, w, 100, 5, true, true, true);
            QCOMPARE(g.property("dropped").property("length").toInt(), 0);
            const QJSValue items = g.property("items"), nav = g.property("nav");
            const Rect n0 = toRect(nav.property(0)), n1 = toRect(nav.property(1));
            const double step = n1.x - n0.x;
            const char *order[] = { "prev", "play", "next", "shuffle", "mute" };
            for (int i = 0; i < 5; ++i) {
                const Rect m = toRect(items.property(order[i]));
                QVERIFY2(qAbs(m.w - n0.w) <= 1 && qAbs(m.h - n0.h) <= 1, order[i]);
                QCOMPARE(m.y, n0.y);
                if (i > 0)
                    QVERIFY2(qAbs(m.x - toRect(items.property(order[i - 1])).x - step) <= 1, order[i]);
            }
            QVERIFY(qAbs(toRect(items.property("clock")).h - n0.h) <= 1);
        }
    }
};

QTEST_GUILESS_MAIN(TstBottomBarLayout)
#include "tst_bottombarlayout.moc"
