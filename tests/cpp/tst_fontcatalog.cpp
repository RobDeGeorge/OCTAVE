// The bundled fonts register in C++ at startup (FontCatalog), so the saved
// font applies on the first frame; Main.qml used to scan the folder with a
// FolderListModel and load each file through a dynamic FontLoader, and a
// font still loading when the setting was applied was silently refused.

#include <QTest>

#include "util/fontcatalog.h"

class TstFontCatalog : public QObject
{
    Q_OBJECT

private slots:
    void displayNames()
    {
        QCOMPARE(FontCatalog::displayNameFor("GoogleSans-Regular.ttf"), QStringLiteral("GoogleSans Regular"));
        QCOMPARE(FontCatalog::displayNameFor("JetBrains_Mono.OTF"), QStringLiteral("JetBrains Mono"));
    }

    void bundledFontsRegister()
    {
        FontCatalog catalog(QStringLiteral(OCTAVE_SOURCE_DIR "/frontend/assets/fonts"));
        QStringList names;
        for (const QVariant &f : catalog.property("fonts").toList()) {
            names << f.toMap().value("name").toString();
            QVERIFY(!f.toMap().value("family").toString().isEmpty());
        }
        QCOMPARE(names, (QStringList{"GoogleSans Regular", "JetBrainsMono Regular"}));   // symbols/ is not offered
        QVERIFY(!catalog.symbolFamily().isEmpty());
    }

    void missingFolderIsEmpty()
    {
        FontCatalog catalog(QStringLiteral("/nonexistent"));
        QVERIFY(catalog.fonts().isEmpty());
        QVERIFY(catalog.symbolFamily().isEmpty());
    }
};

QTEST_MAIN(TstFontCatalog)
#include "tst_fontcatalog.moc"
