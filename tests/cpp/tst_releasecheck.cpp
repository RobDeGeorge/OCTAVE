// The update check of a packaged build (src/util/releasecheck): the order of
// two version strings and what is taken from GitHub's "latest release"
// answer. Until 0.9.6 every packaged build said "Could not determine local
// version", as the check only knew a git checkout.

#include <QTest>

#include "util/releasecheck.h"

class TstReleaseCheck : public QObject
{
    Q_OBJECT

private slots:
    void versionsAreOrdered_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("order");
        QTest::newRow("same") << "0.9.6" << "0.9.6" << 0;
        QTest::newRow("the tag's v") << "v0.9.6" << "0.9.6" << 0;
        QTest::newRow("patch") << "0.9.5" << "0.9.6" << -1;
        QTest::newRow("numbers, not text") << "0.9.10" << "0.9.9" << 1;
        QTest::newRow("minor before patch") << "0.10.0" << "0.9.12" << 1;
        QTest::newRow("major") << "1.0.0" << "0.99.99" << 1;
        QTest::newRow("a missing part is 0") << "0.9" << "0.9.0" << 0;
        QTest::newRow("a longer one is newer") << "0.9.6.1" << "0.9.6" << 1;
        QTest::newRow("rc before its release") << "0.9.6-rc4" << "0.9.6" << -1;
        QTest::newRow("rc after the last release") << "0.9.6-rc1" << "0.9.5" << 1;
        QTest::newRow("rc numbers") << "0.9.6-rc2" << "0.9.6-rc10" << -1;
        QTest::newRow("same rc") << "v0.9.6-rc3" << "0.9.6-rc3" << 0;
        QTest::newRow("a word is a pre-release") << "0.9.6-beta" << "0.9.6" << -1;
        QTest::newRow("spaces") << " v0.9.6\n" << "0.9.6" << 0;
        QTest::newRow("dev build") << "dev" << "0.9.6" << -1;
    }
    void versionsAreOrdered()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, order);
        QCOMPARE(ReleaseCheck::compareVersions(a, b), order);
        QCOMPARE(ReleaseCheck::compareVersions(b, a), -order);
    }

    void theLatestReleaseIsRead()
    {
        const QByteArray json = R"({
            "tag_name": "v0.9.7", "name": "OCTAVE 0.9.7", "draft": false, "prerelease": false,
            "html_url": "https://github.com/RobDeGeorge/OCTAVE/releases/tag/v0.9.7"})";
        const auto r = ReleaseCheck::parseLatest(json, QStringLiteral("RobDeGeorge/OCTAVE"));
        QVERIFY(r.valid);
        QCOMPARE(r.tag, QStringLiteral("v0.9.7"));
        QCOMPARE(r.version, QStringLiteral("0.9.7"));
        QCOMPARE(r.name, QStringLiteral("OCTAVE 0.9.7"));
        QCOMPARE(r.url, QStringLiteral("https://github.com/RobDeGeorge/OCTAVE/releases/tag/v0.9.7"));
    }

    // The page is opened in the browser: only a page of this repo on github.com
    void aLinkToAnywhereElseIsNotTaken_data()
    {
        QTest::addColumn<QString>("link");
        QTest::newRow("another site") << "https://example.com/RobDeGeorge/OCTAVE/releases/tag/v0.9.7";
        QTest::newRow("another repo") << "https://github.com/someone/else/releases/tag/v0.9.7";
        QTest::newRow("a look-alike host") << "https://github.com.example.com/RobDeGeorge/OCTAVE/releases/tag/v0.9.7";
        QTest::newRow("a file") << "file:///etc/passwd";
        QTest::newRow("a script") << "javascript:alert(1)";
        QTest::newRow("none") << "";
    }
    void aLinkToAnywhereElseIsNotTaken()
    {
        QFETCH(QString, link);
        const QByteArray json = QByteArrayLiteral("{\"tag_name\":\"v0.9.7\",\"html_url\":\"") + link.toUtf8()
            + QByteArrayLiteral("\"}");
        const auto r = ReleaseCheck::parseLatest(json, QStringLiteral("RobDeGeorge/OCTAVE"));
        QVERIFY(r.valid);
        QCOMPARE(r.url, QStringLiteral("https://github.com/RobDeGeorge/OCTAVE/releases"));
    }

    void whatIsNotAReleaseIsNotOne_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("not JSON") << QByteArray("<html>rate limit</html>");
        QTest::newRow("a list") << QByteArray("[{\"tag_name\":\"v0.9.7\"}]");
        QTest::newRow("an error") << QByteArray("{\"message\":\"Not Found\"}");
        QTest::newRow("no version in the tag") << QByteArray("{\"tag_name\":\"nightly\"}");
        QTest::newRow("a path as tag") << QByteArray("{\"tag_name\":\"v1/../../x\"}");
        QTest::newRow("a draft") << QByteArray("{\"tag_name\":\"v0.9.7\",\"draft\":true}");
        QTest::newRow("a pre-release") << QByteArray("{\"tag_name\":\"v0.9.7\",\"prerelease\":true}");
    }
    void whatIsNotAReleaseIsNotOne()
    {
        QFETCH(QByteArray, json);
        QVERIFY(!ReleaseCheck::parseLatest(json, QStringLiteral("RobDeGeorge/OCTAVE")).valid);
    }
};

QTEST_GUILESS_MAIN(TstReleaseCheck)
#include "tst_releasecheck.moc"
