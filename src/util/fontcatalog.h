// fontcatalog.h
//
// The fonts OCTAVE ships (frontend/assets/fonts), registered with Qt once at
// startup: the UI fonts the font picker offers and the icon-glyph font in
// its symbols/ subfolder. Replaces Main.qml's FolderListModel and dynamic
// FontLoaders, whose async loading could miss the saved font setting at
// startup. Context property "fontCatalog".

#ifndef FONTCATALOG_H
#define FONTCATALOG_H

#include <QObject>
#include <QString>
#include <QVariantList>

class FontCatalog : public QObject
{
    Q_OBJECT
    // {name, family} per UI font, by file name: `name` is what the picker
    // shows and fontSetting stores, `family` what Text.font.family takes
    Q_PROPERTY(QVariantList fonts READ fonts CONSTANT)
    // Family of the icon-glyph font ("" if it failed to load)
    Q_PROPERTY(QString symbolFamily READ symbolFamily CONSTANT)

public:
    // `fontsDir`: the folder with the .ttf / .otf files (assets: on Android)
    explicit FontCatalog(const QString &fontsDir, QObject *parent = nullptr);

    QVariantList fonts() const { return m_fonts; }
    QString symbolFamily() const { return m_symbolFamily; }

    // "GoogleSans-Regular.ttf" -> "GoogleSans Regular"
    static QString displayNameFor(const QString &fileName);

private:
    static QString registerFont(const QString &path);   // family, or "" on failure

    QVariantList m_fonts;
    QString m_symbolFamily;
};

#endif // FONTCATALOG_H
