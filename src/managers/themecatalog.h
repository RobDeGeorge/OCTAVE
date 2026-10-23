#ifndef THEMECATALOG_H
#define THEMECATALOG_H

// The colour themes as data (frontend/themes.json), so any frontend can
// draw the user's theme: the built-in palettes, their order and light /
// dark grouping, and `activeTheme`, the palette in force right now,
// resolved the way the QML UI resolves it (frontend/Style.qml):
//
//   theme "Album Art Capture" with extracted colours -> those colours
//   (settingsManager.albumArtColors); a custom theme of that name
//   (settingsManager.customThemeData); a built-in one; else SolarizedLight.
//
// Every palette has the same shape: base, baseAlt, accent, text {primary,
// secondary}, states {hover, paused, playing}, sliders, bottombar,
// mediaroom, mainmenu, obd (wiki/theme-system.html). Animation and the
// QML's transition timing stay in Style.qml.

#include <QObject>
#include <QStringList>
#include <QVariantMap>

class ThemeCatalog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap themes READ themes CONSTANT)                 // name -> palette (built-ins)
    Q_PROPERTY(QStringList themeNames READ themeNames CONSTANT)          // built-ins, file order
    Q_PROPERTY(QStringList lightThemeNames READ lightThemeNames CONSTANT)
    Q_PROPERTY(QStringList darkThemeNames READ darkThemeNames CONSTANT)
    Q_PROPERTY(QString activeThemeName READ activeThemeName NOTIFY activeThemeChanged)
    Q_PROPERTY(QVariantMap activeTheme READ activeTheme NOTIFY activeThemeChanged)

public:
    static constexpr const char *kFallbackTheme = "SolarizedLight";
    static constexpr const char *kAlbumArtTheme = "Album Art Capture";

    // `settings` is the SettingsManager, read through its properties
    // (themeSetting, customThemeData, albumArtColors) and their notify
    // signals, so QML tests can link this without it; null = the fallback
    explicit ThemeCatalog(const QString &jsonPath, QObject *settings = nullptr, QObject *parent = nullptr);

    QVariantMap themes() const { return m_themes; }
    QStringList themeNames() const { return m_themeNames; }
    QStringList lightThemeNames() const { return m_light; }
    QStringList darkThemeNames() const { return m_dark; }
    QString activeThemeName() const { return m_activeName; }
    QVariantMap activeTheme() const { return m_active; }

    // The palette for theme `name` given the custom themes and the album
    // art colours (pure, for the tests)
    QVariantMap resolve(const QString &name, const QVariantMap &customThemes, const QVariantMap &albumArtColors) const;

signals:
    void activeThemeChanged();

private slots:
    void refresh();

private:
    QObject *m_settings;
    QVariantMap m_themes;
    QStringList m_themeNames, m_light, m_dark;
    QString m_activeName;
    QVariantMap m_active;
};

#endif // THEMECATALOG_H
