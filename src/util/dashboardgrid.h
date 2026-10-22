// dashboardgrid.h
//
// Grid placement rules for dashboard specs: where a cell is, whether a
// rectangle fits the grid without covering another cell, and how far a new
// widget shrinks to fit the space it was dropped in. One copy for
// DashboardManager (spec normalising on save / load / import) and the QML
// editor (tap-to-place, drag, resize), which calls it through
// DashboardManager's Q_INVOKABLEs.

#ifndef DASHBOARDGRID_H
#define DASHBOARDGRID_H

#include <QVariantList>
#include <QVariantMap>

namespace DashboardGrid {

struct Rect
{
    int col = 0;
    int row = 0;
    int colSpan = 1;
    int rowSpan = 1;
};

// A cell's rectangle; missing keys are the spec defaults (0, 0, 1, 1)
Rect rectOf(const QVariantMap &cell);

bool overlaps(const Rect &a, const Rect &b);

// Inside a cols x rows grid and not covering any cell of `cells` other
// than `exceptIndex` (the cell being moved or resized; -1 for none)
bool canPlace(const QVariantList &cells, int cols, int rows, const Rect &r, int exceptIndex = -1);

// The span a new widget gets at (col, row): its wanted span cut to the grid
// edge, then shrunk toward 1x1 (the wider side first) until it fits. The
// result may still not fit if (col, row) itself is taken.
Rect fitAt(const QVariantList &cells, int cols, int rows, int col, int row, int colSpan, int rowSpan);

} // namespace DashboardGrid

#endif // DASHBOARDGRID_H
