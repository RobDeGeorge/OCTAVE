// ThemeCatalog (src/managers/themecatalog.{h,cpp}): the built-in theme table
// from frontend/themes.json, its order and light / dark grouping, and
// activeTheme resolved as Style.qml resolves it (album art colours, a custom
// theme, a built-in, the fallback), following the settings as they change.

#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "managers/settingsmanager.h"
#include "managers/themecatalog.h"

namespace {
const QString kThemes = QStringLiteral(OCTAVE_SOURCE_DIR "/frontend/themes.json");
}

class TstThemeCatalog : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        qputenv("HOME", m_tmp->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp->path() + "/config").toUtf8());
        qputenv("APPDATA", (m_tmp->path() + "/appdata").toUtf8());
    }

    void theTableIsData()
    {
        ThemeCatalog themes(kThemes);
        QCOMPARE(themes.themeNames().size(), 17);
        QCOMPARE(themes.themeNames().first(), QStringLiteral("SolarizedLight"));
        QVERIFY(themes.themeNames().contains("Album Art Capture"));
        QCOMPARE(themes.lightThemeNames().size(), 8);
        QCOMPARE(themes.darkThemeNames().size(), 8);
        // Every name in the table, every palette the same shape
        QCOMPARE(themes.themes().keys().size(), themes.themeNames().size());
        for (const QString &name : themes.themeNames()) {
            const QVariantMap p = themes.themes().value(name).toMap();
            for (const char *key : {"base", "baseAlt", "accent", "text", "states", "sliders", "bottombar", "mediaroom", "mainmenu", "obd"})
                QVERIFY2(p.contains(key), qPrintable(name + ": " + key));
            QVERIFY2(QColor::isValidColorName(p.value("accent").toString()), qPrintable(name));
        }
        for (const QString &name : themes.lightThemeNames() + themes.darkThemeNames())
            QVERIFY2(themes.themes().contains(name), qPrintable(name));
        QCOMPARE(themes.themes().value("SolarizedLight").toMap().value("accent").toString(), QStringLiteral("#268BD2"));
        // No settings: the fallback
        QCOMPARE(themes.activeThemeName(), QStringLiteral("SolarizedLight"));
        QCOMPARE(themes.activeTheme(), themes.themes().value("SolarizedLight").toMap());
    }

    void activeThemeFollowsTheSettings()
    {
        SettingsManager settings;
        ThemeCatalog themes(kThemes, &settings);
        QSignalSpy changed(&themes, &ThemeCatalog::activeThemeChanged);

        settings.save_theme_setting("NeonMatrix");
        QCOMPARE(themes.activeThemeName(), QStringLiteral("NeonMatrix"));
        QCOMPARE(themes.activeTheme().value("accent").toString(), QStringLiteral("#00FF41"));
        QCOMPARE(changed.size(), 1);

        // Album Art Capture: the placeholder until colours exist, then those
        settings.save_theme_setting("Album Art Capture");
        QCOMPARE(themes.activeTheme(), themes.themes().value("Album Art Capture").toMap());
        QVariantMap art = themes.themes().value("NeonMatrix").toMap();
        art.insert("accent", "#123456");
        settings.set_album_art_colors(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(art)).toJson()));
        QCOMPARE(themes.activeTheme().value("accent").toString(), QStringLiteral("#123456"));

        // An unknown name: the fallback, as Style.qml does
        QCOMPARE(themes.resolve("NoSuchTheme", {}, {}), themes.themes().value("SolarizedLight").toMap());
        // A custom theme wins over a built-in of the same name
        QVariantMap custom = art;
        custom.insert("accent", "#abcdef");
        QCOMPARE(themes.resolve("NeonMatrix", {{"NeonMatrix", custom}}, {}).value("accent").toString(),
                 QStringLiteral("#abcdef"));
        // ...and a saved one is followed live
        settings.save_custom_theme("Mine", custom);
        settings.save_theme_setting("Mine");
        QCOMPARE(themes.activeThemeName(), QStringLiteral("Mine"));
        QCOMPARE(themes.activeTheme().value("accent").toString(), QStringLiteral("#abcdef"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_GUILESS_MAIN(TstThemeCatalog)
#include "tst_themecatalog.moc"
