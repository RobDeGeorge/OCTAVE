// fontcatalog.cpp

#include "fontcatalog.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QLoggingCategory>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(lcFonts, "octave.fonts")

FontCatalog::FontCatalog(const QString &fontsDir, QObject *parent)
    : QObject(parent)
{
    const QDir dir(fontsDir);
    const QStringList files = dir.entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf"),
                                             QStringLiteral("*.TTF"), QStringLiteral("*.OTF")},
                                            QDir::Files, QDir::Name);
    for (const QString &file : files) {
        const QString family = registerFont(dir.filePath(file));
        if (family.isEmpty())
            continue;
        m_fonts.append(QVariantMap{{QStringLiteral("name"), displayNameFor(file)},
                                   {QStringLiteral("family"), family}});
    }
    m_symbolFamily = registerFont(dir.filePath(QStringLiteral("symbols/OCTAVESymbols-Regular.ttf")));
    if (m_symbolFamily.isEmpty())
        qCWarning(lcFonts) << "Symbol font failed to load; icon glyphs fall back to the UI font";
    qCInfo(lcFonts) << m_fonts.size() << "UI fonts from" << fontsDir;
}

QString FontCatalog::displayNameFor(const QString &fileName)
{
    QString name = fileName;
    name.remove(QRegularExpression(QStringLiteral("\\.(ttf|otf)$"), QRegularExpression::CaseInsensitiveOption));
    name.replace(QRegularExpression(QStringLiteral("[-_]")), QStringLiteral(" "));
    return name;
}

QString FontCatalog::registerFont(const QString &path)
{
    // From the bytes: works for assets: paths on Android as well as files
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcFonts) << "Cannot open font" << path;
        return {};
    }
    const int id = QFontDatabase::addApplicationFontFromData(f.readAll());
    const QStringList families = id < 0 ? QStringList() : QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) {
        qCWarning(lcFonts) << "Font did not load:" << path;
        return {};
    }
    return families.first();
}
