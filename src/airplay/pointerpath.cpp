#include "pointerpath.h"

#include <QtMath>

namespace PointerPath {

QSizeF countsAcross(const QSizeF &screen, double scale)
{
    const double shortSide = qMin(screen.width(), screen.height());
    if (shortSide <= 0 || scale <= 0)
        return {};
    return {scale * screen.width() / shortSide, scale * screen.height() / shortSide};
}

QList<Report> home()
{
    QList<Report> out;
    out.reserve(kHomeReports);
    for (int i = 0; i < kHomeReports; ++i)
        out.append({-kHomeStep, -kHomeStep, false});
    return out;
}

QList<Report> move(QPointF from, QPointF to, const QSizeF &counts, bool button, QPointF *carry)
{
    QList<Report> out;
    const QPointF c = carry ? *carry : QPointF();
    const double wantX = (to.x() - from.x()) * counts.width() + c.x();
    const double wantY = (to.y() - from.y()) * counts.height() + c.y();
    int dx = int(std::lround(wantX));
    int dy = int(std::lround(wantY));
    if (carry)
        *carry = QPointF(wantX - dx, wantY - dy);
    // Equal-length steps along the line, none longer than kStep on either axis
    const int steps = qMax(1, int(std::ceil(qMax(qAbs(dx), qAbs(dy)) / double(kStep))));
    if (dx == 0 && dy == 0)
        return out;
    int sentX = 0, sentY = 0;
    for (int i = 1; i <= steps; ++i) {
        const int x = int(std::lround(double(dx) * i / steps));
        const int y = int(std::lround(double(dy) * i / steps));
        out.append({x - sentX, y - sentY, button});
        sentX = x;
        sentY = y;
    }
    return out;
}

} // namespace PointerPath
