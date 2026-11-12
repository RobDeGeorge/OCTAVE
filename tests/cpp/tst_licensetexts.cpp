// LicenseTexts (src/util/licensetexts.{h,cpp}): every license text
// Settings > About offers is compiled in, the names in the C++ list, the
// CMake list (octave_add_license_texts) and AboutPage.qml agree, and no
// other name reads a resource.

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTest>

#include "util/licensetexts.h"

class TstLicenseTexts : public QObject
{
    Q_OBJECT

private slots:
    void everyNameHasItsText_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("phrase");
        QTest::newRow("LICENSE") << "LICENSE" << "GNU GENERAL PUBLIC LICENSE";
        QTest::newRow("NOTICE") << "NOTICE" << "Third-party code bundled in this repository";
        QTest::newRow("LGPL-3.0") << "LGPL-3.0" << "GNU LESSER GENERAL PUBLIC LICENSE\nVersion 3";
        QTest::newRow("LGPL-2.1") << "LGPL-2.1" << "Version 2.1, February 1999";
        QTest::newRow("GPL-2.0") << "GPL-2.0" << "Version 2, June 1991";
        QTest::newRow("MPL-1.1") << "MPL-1.1" << "Mozilla Public License Version 1.1";
        QTest::newRow("Apache-2.0") << "Apache-2.0" << "Version 2.0, January 2004";
        QTest::newRow("MIT") << "MIT" << "Copyright (c) Project Nayuki";
        QTest::newRow("OFL-1.1") << "OFL-1.1" << "SIL OPEN FONT LICENSE Version 1.1";
    }
    void everyNameHasItsText()
    {
        QFETCH(QString, name);
        QFETCH(QString, phrase);
        QVERIFY(LicenseTexts::names().contains(name));
        const QString text = LicenseTexts::text(name);
        QVERIFY2(text.size() > 1000, qPrintable(name));
        QVERIFY2(text.contains(phrase), qPrintable(name));
        // The popup shows one Text per line; a form feed would be a box
        QVERIFY(!text.contains(QLatin1Char('\f')));
    }

    void theTableAboveIsTheWholeList()
    {
        QCOMPARE(LicenseTexts::names().size(), 9);
    }

    void theNoticesNameTheirHolders()
    {
        const QString mit = LicenseTexts::text(QStringLiteral("MIT"));
        QVERIFY(mit.contains(QLatin1String("Copyright Fedor Indutny")));      // llhttp
        QVERIFY(mit.contains(QLatin1String("Copyright (c) 2021 Guido Zuidhof"))); // coi-serviceworker
        const QString ofl = LicenseTexts::text(QStringLiteral("OFL-1.1"));
        QVERIFY(ofl.contains(QLatin1String("The Google Sans Project Authors")));
        QVERIFY(ofl.contains(QLatin1String("The JetBrains Mono Project Authors")));
        QVERIFY(ofl.contains(QLatin1String("The Noto Project Authors")));
    }

    void otherNamesReadNothing_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("empty") << "";
        QTest::newRow("with extension") << "MIT.txt";
        QTest::newRow("lower case") << "license";
        QTest::newRow("parent") << "../licenses/MIT";
        QTest::newRow("another resource") << "../phone-server/NOTICE";
        QTest::newRow("absolute") << "/etc/hostname";
    }
    void otherNamesReadNothing()
    {
        QFETCH(QString, name);
        QVERIFY(LicenseTexts::text(name).isEmpty());
    }

    // CMake's list: nothing compiled in that names() does not serve
    void theResourceHoldsTheSameNames()
    {
        QSet<QString> compiled;
        const QStringList entries = QDir(QStringLiteral(":/licenses")).entryList(QDir::Files);
        for (QString e : entries) {
            if (e.endsWith(QLatin1String(".txt")))
                e.chop(4);
            compiled.insert(e);
        }
        const QStringList names = LicenseTexts::names();
        QCOMPARE(compiled, QSet<QString>(names.begin(), names.end()));
    }

    // AboutPage.qml: its buttons are the list, and a component names only those
    void theAboutPageAsksOnlyForThese()
    {
        QFile f(QStringLiteral(OCTAVE_SOURCE_DIR "/frontend/settings/AboutPage.qml"));
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString qml = QString::fromUtf8(f.readAll());
        const QStringList names = LicenseTexts::names();
        const QSet<QString> known(names.begin(), names.end());

        QSet<QString> buttons;
        auto it = QRegularExpression(QStringLiteral("\\{ file: \"([^\"]+)\", label:")).globalMatch(qml);
        while (it.hasNext())
            buttons.insert(it.next().captured(1));
        QCOMPARE(buttons, known);

        int componentTexts = 0;
        it = QRegularExpression(QStringLiteral("texts: \\[([^\\]]*)\\]")).globalMatch(qml);
        while (it.hasNext()) {
            const QStringList items = it.next().captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &item : items) {
                const QString name = item.trimmed().remove(QLatin1Char('"'));
                QVERIFY2(known.contains(name), qPrintable(name));
                ++componentTexts;
            }
        }
        QVERIFY(componentTexts > 10);
    }
};

QTEST_GUILESS_MAIN(TstLicenseTexts)
#include "tst_licensetexts.moc"
