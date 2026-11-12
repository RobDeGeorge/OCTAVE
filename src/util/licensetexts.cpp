#include "licensetexts.h"

#include <QFile>

namespace LicenseTexts {

QStringList names()
{
    // The same list as OCTAVE_LICENSE_TEXTS in CMakeLists.txt, after the two
    // files of the repository root
    static const QStringList all = {
        QStringLiteral("LICENSE"),    QStringLiteral("NOTICE"),
        QStringLiteral("LGPL-3.0"),   QStringLiteral("LGPL-2.1"),
        QStringLiteral("GPL-2.0"),    QStringLiteral("MPL-1.1"),
        QStringLiteral("Apache-2.0"), QStringLiteral("MIT"),
        QStringLiteral("OFL-1.1"),
    };
    return all;
}

QString text(const QString &name)
{
    // Only the names above: the argument comes from QML and from the service
    if (!names().contains(name))
        return {};
    QFile f(QStringLiteral(":/licenses/") + name);
    if (!f.exists())
        f.setFileName(QStringLiteral(":/licenses/") + name + QStringLiteral(".txt"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace LicenseTexts
