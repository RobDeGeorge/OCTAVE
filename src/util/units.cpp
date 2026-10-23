// units.cpp

#include "units.h"

#include <QStringList>

#include <cmath>

namespace UnitsFn {

QString formatDuration(double ms)
{
    if (!std::isfinite(ms) || ms < 0)
        ms = 0;
    const qint64 total = static_cast<qint64>(ms / 1000.0);
    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    const qint64 seconds = total % 60;
    if (hours > 0)
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString cardinal(double headingDeg)
{
    static const QStringList dirs{QStringLiteral("N"), QStringLiteral("NE"), QStringLiteral("E"), QStringLiteral("SE"),
                                  QStringLiteral("S"), QStringLiteral("SW"), QStringLiteral("W"), QStringLiteral("NW")};
    if (!std::isfinite(headingDeg))
        return dirs.first();
    const int idx = static_cast<int>(std::lround(headingDeg / 45.0)) % 8;
    return dirs.at((idx + 8) % 8);
}

} // namespace UnitsFn
