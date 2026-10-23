#include "themecatalog.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcThemes, "octave.themes")

ThemeCatalog::ThemeCatalog(const QString &jsonPath, QObject *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    QFile f(jsonPath);
    QJsonParseError error{};
    const QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &error) : QJsonDocument();
    if (doc.isObject()) {
        const QVariantMap data = doc.object().toVariantMap();
        m_themes = data.value(QStringLiteral("themes")).toMap();
        m_themeNames = data.value(QStringLiteral("themeNames")).toStringList();
        m_light = data.value(QStringLiteral("lightThemeNames")).toStringList();
        m_dark = data.value(QStringLiteral("darkThemeNames")).toStringList();
    } else {
        qCWarning(lcThemes) << "No theme table at" << jsonPath << error.errorString();
    }

    if (m_settings) {
        connect(m_settings, SIGNAL(themeSettingChanged(QString)), this, SLOT(refresh()));
        connect(m_settings, SIGNAL(customThemesChanged()), this, SLOT(refresh()));
        connect(m_settings, SIGNAL(albumArtColorsChanged()), this, SLOT(refresh()));
    }
    refresh();
}

QVariantMap ThemeCatalog::resolve(const QString &name, const QVariantMap &customThemes,
                                  const QVariantMap &albumArtColors) const
{
    if (name == QLatin1String(kAlbumArtTheme) && !albumArtColors.isEmpty())
        return albumArtColors;
    if (customThemes.contains(name))
        return customThemes.value(name).toMap();
    if (m_themes.contains(name))
        return m_themes.value(name).toMap();
    return m_themes.value(QLatin1String(kFallbackTheme)).toMap();
}

void ThemeCatalog::refresh()
{
    const QString name = m_settings ? m_settings->property("themeSetting").toString() : QString();
    const QVariantMap palette = m_settings
        ? resolve(name, m_settings->property("customThemeData").toMap(), m_settings->property("albumArtColors").toMap())
        : resolve(name, {}, {});
    if (name == m_activeName && palette == m_active)
        return;
    m_activeName = name.isEmpty() ? QString::fromLatin1(kFallbackTheme) : name;
    m_active = palette;
    emit activeThemeChanged();
}
