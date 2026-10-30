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

#include <QList>
#include <QString>
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

// ── Moving and resizing with neighbours (the editor's drag / resize) ──
//
// Put cell `index` at `target`. When `target` covers other cells:
//   - swap: exactly one cell is covered and it fits where the moved cell
//     was, so the two trade places;
//   - push: otherwise each covered cell goes to the nearest free spot of
//     its own size (by distance from where it was).
// Cells the target does not cover never move, so the rest of the layout
// stays where the user put it. `ok` false = no arrangement fits: `reason`
// says why and `cells` is the input unchanged.
struct Arrangement
{
    bool ok = false;
    QVariantList cells;
    QList<int> moved;     // indexes of the other cells that moved
    bool swapped = false;
    QString reason;
};
Arrangement arrange(const QVariantList &cells, int cols, int rows, int index, const Rect &target);

// The free spot of r's span nearest (r.col, r.row), ignoring cell
// `exceptIndex`; r is moved there. False when no spot of that span is free.
bool nearestFree(const QVariantList &cells, int cols, int rows, Rect &r, int exceptIndex = -1);

// ── Density ──
//
// The same layout on a toCols x toRows grid (the editor's Coarse / Normal /
// Fine): every edge scaled by the grid ratio and rounded, then the cells
// placed biggest first where they land, else at the nearest free spot, else
// a step smaller. `ok` false = they don't all fit even at 1x1: `reason`
// says so and `cells` is the input unchanged.
struct Rescaled
{
    bool ok = false;
    QVariantList cells;
    QString reason;
};
Rescaled rescale(const QVariantList &cells, int fromCols, int fromRows, int toCols, int toRows);

// ── Sizes ──
//
// The smallest span whose on-screen size reaches minW x minH (any unit, the
// same as cellW / cellH / spacing), held to the grid
Rect minSpan(int cols, int rows, double cellW, double cellH, double spacing, double minW, double minH);

// Small / Medium / Large for a widget on this grid: about 1/12, 1/6 and 1/3
// of the grid's cells, the span nearest the widget's aspect ratio (w / h,
// on screen), never below minSpan nor past the grid. Repeats are dropped,
// so a small grid may offer fewer.
struct SizeOption
{
    QString label;
    int colSpan = 1;
    int rowSpan = 1;
};
QList<SizeOption> sizeOptions(int cols, int rows, double cellW, double cellH, double spacing,
                              double minW, double minH, double aspect);

} // namespace DashboardGrid

#endif // DASHBOARDGRID_H
