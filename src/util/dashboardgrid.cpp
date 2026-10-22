// dashboardgrid.cpp

#include "dashboardgrid.h"

#include <QtGlobal>

namespace DashboardGrid {

Rect rectOf(const QVariantMap &cell)
{
    Rect r;
    r.col = cell.value(QStringLiteral("col"), 0).toInt();
    r.row = cell.value(QStringLiteral("row"), 0).toInt();
    r.colSpan = cell.value(QStringLiteral("colSpan"), 1).toInt();
    r.rowSpan = cell.value(QStringLiteral("rowSpan"), 1).toInt();
    return r;
}

bool overlaps(const Rect &a, const Rect &b)
{
    return a.col < b.col + b.colSpan && b.col < a.col + a.colSpan
        && a.row < b.row + b.rowSpan && b.row < a.row + a.rowSpan;
}

bool canPlace(const QVariantList &cells, int cols, int rows, const Rect &r, int exceptIndex)
{
    if (r.col < 0 || r.row < 0 || r.colSpan < 1 || r.rowSpan < 1)
        return false;
    if (r.col + r.colSpan > cols || r.row + r.rowSpan > rows)
        return false;
    for (int i = 0; i < cells.size(); ++i) {
        if (i == exceptIndex)
            continue;
        if (overlaps(r, rectOf(cells.at(i).toMap())))
            return false;
    }
    return true;
}

Rect fitAt(const QVariantList &cells, int cols, int rows, int col, int row, int colSpan, int rowSpan)
{
    Rect r{col, row, qMax(1, qMin(colSpan, cols - col)), qMax(1, qMin(rowSpan, rows - row))};
    while (!canPlace(cells, cols, rows, r) && (r.colSpan > 1 || r.rowSpan > 1)) {
        if (r.colSpan >= r.rowSpan && r.colSpan > 1)
            --r.colSpan;
        else
            --r.rowSpan;
    }
    return r;
}

} // namespace DashboardGrid
