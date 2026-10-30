// dashboardgrid.cpp

#include "dashboardgrid.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

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

namespace {
QVariantMap withRect(QVariantMap cell, const Rect &r)
{
    cell.insert(QStringLiteral("col"), r.col);
    cell.insert(QStringLiteral("row"), r.row);
    cell.insert(QStringLiteral("colSpan"), r.colSpan);
    cell.insert(QStringLiteral("rowSpan"), r.rowSpan);
    return cell;
}

bool inGrid(int cols, int rows, const Rect &r)
{
    return r.col >= 0 && r.row >= 0 && r.colSpan >= 1 && r.rowSpan >= 1
        && r.col + r.colSpan <= cols && r.row + r.rowSpan <= rows;
}

// No fixed cell (the ones not being pushed, and the moved one) under r
bool freeOf(const QList<Rect> &rects, const QList<bool> &fixed, const Rect &r)
{
    for (int i = 0; i < rects.size(); ++i)
        if (fixed.at(i) && overlaps(r, rects.at(i)))
            return false;
    return true;
}

// The free spot of `want`'s span nearest `want` (centre distance; a
// same-row move wins a tie, then reading order). False when there is none.
bool nearestFreeOf(const QList<Rect> &rects, const QList<bool> &fixed, int cols, int rows, Rect &want)
{
    Rect best;
    double bestDist = std::numeric_limits<double>::max();
    for (int row = 0; row + want.rowSpan <= rows; ++row) {
        for (int col = 0; col + want.colSpan <= cols; ++col) {
            const Rect r{col, row, want.colSpan, want.rowSpan};
            if (!freeOf(rects, fixed, r))
                continue;
            const double dc = col - want.col, dr = row - want.row;
            const double dist = std::hypot(dc, dr) + (dr == 0 ? 0.0 : 0.001);
            if (dist < bestDist) {
                bestDist = dist;
                best = r;
            }
        }
    }
    if (bestDist == std::numeric_limits<double>::max())
        return false;
    want = best;
    return true;
}
} // namespace

bool nearestFree(const QVariantList &cells, int cols, int rows, Rect &r, int exceptIndex)
{
    if (r.colSpan < 1 || r.rowSpan < 1 || r.colSpan > cols || r.rowSpan > rows)
        return false;
    QList<Rect> rects;
    QList<bool> fixed;
    for (int i = 0; i < cells.size(); ++i) {
        rects.append(rectOf(cells.at(i).toMap()));
        fixed.append(i != exceptIndex);
    }
    return nearestFreeOf(rects, fixed, cols, rows, r);
}

Arrangement arrange(const QVariantList &cells, int cols, int rows, int index, const Rect &target)
{
    Arrangement out;
    out.cells = cells;
    if (index < 0 || index >= cells.size()) {
        out.reason = QStringLiteral("No such widget");
        return out;
    }
    if (!inGrid(cols, rows, target)) {
        out.reason = QStringLiteral("That would go past the edge of the dashboard");
        return out;
    }

    QList<Rect> rects;
    for (const QVariant &c : cells)
        rects.append(rectOf(c.toMap()));
    const Rect origin = rects.at(index);

    QList<int> covered;
    for (int i = 0; i < rects.size(); ++i)
        if (i != index && overlaps(target, rects.at(i)))
            covered.append(i);

    QVariantList next = cells;
    next[index] = withRect(cells.at(index).toMap(), target);
    if (covered.isEmpty()) {
        out.ok = true;
        out.cells = next;
        return out;
    }

    // Everything that is not covered stays put; the moved cell takes target
    QList<bool> fixed(rects.size(), true);
    for (int i : covered)
        fixed[i] = false;
    rects[index] = target;

    // Swap: one covered cell, and its span fits where the moved one was
    if (covered.size() == 1) {
        const int other = covered.first();
        Rect swapped{origin.col, origin.row, rects.at(other).colSpan, rects.at(other).rowSpan};
        if (inGrid(cols, rows, swapped) && !overlaps(swapped, target) && freeOf(rects, fixed, swapped)) {
            next[other] = withRect(cells.at(other).toMap(), swapped);
            out.ok = true;
            out.swapped = true;
            out.moved = {other};
            out.cells = next;
            return out;
        }
    }

    // Push: the covered cells, biggest first (they are hardest to place),
    // each to the free spot of its size nearest where it was
    std::stable_sort(covered.begin(), covered.end(), [&rects](int a, int b) {
        return rects.at(a).colSpan * rects.at(a).rowSpan > rects.at(b).colSpan * rects.at(b).rowSpan;
    });
    for (int i : covered) {
        Rect best = rects.at(i);
        if (!nearestFreeOf(rects, fixed, cols, rows, best)) {
            out.reason = QStringLiteral("No room: make a widget smaller or move something first");
            return out;
        }
        rects[i] = best;
        fixed[i] = true;
        next[i] = withRect(cells.at(i).toMap(), best);
        out.moved.append(i);
    }
    std::sort(out.moved.begin(), out.moved.end());
    out.ok = true;
    out.cells = next;
    return out;
}

Rescaled rescale(const QVariantList &cells, int fromCols, int fromRows, int toCols, int toRows)
{
    Rescaled out;
    out.cells = cells;
    if (fromCols < 1 || fromRows < 1 || toCols < 1 || toRows < 1) {
        out.reason = QStringLiteral("That grid size is not valid");
        return out;
    }
    const double kx = double(toCols) / fromCols, ky = double(toRows) / fromRows;
    // Each edge scaled and rounded, so neighbours that touched still touch
    auto scaled = [](int start, int span, double k, int limit, int &outStart, int &outSpan) {
        outStart = qBound(0, int(std::lround(start * k)), limit - 1);
        const int end = qBound(outStart + 1, int(std::lround((start + span) * k)), limit);
        outSpan = end - outStart;
    };
    QList<Rect> want;
    for (const QVariant &c : cells) {
        const Rect r = rectOf(c.toMap());
        Rect s;
        scaled(r.col, r.colSpan, kx, toCols, s.col, s.colSpan);
        scaled(r.row, r.rowSpan, ky, toRows, s.row, s.rowSpan);
        want.append(s);
    }

    // Biggest first: they are the hardest to place
    QList<int> order;
    for (int i = 0; i < want.size(); ++i)
        order.append(i);
    std::stable_sort(order.begin(), order.end(), [&want](int a, int b) {
        return want.at(a).colSpan * want.at(a).rowSpan > want.at(b).colSpan * want.at(b).rowSpan;
    });

    QList<Rect> placed(want.size());
    QList<bool> fixed(want.size(), false);
    QVariantList next = cells;
    for (int i : order) {
        Rect r = want.at(i);
        // Its own size anywhere near, else a step smaller (the longer side
        // first) until something fits
        while (!nearestFreeOf(placed, fixed, toCols, toRows, r)) {
            if (r.colSpan == 1 && r.rowSpan == 1) {
                out.reason = QStringLiteral("Not every widget fits on a grid that size: remove one first");
                return out;
            }
            if (r.colSpan >= r.rowSpan && r.colSpan > 1)
                --r.colSpan;
            else
                --r.rowSpan;
            r.col = want.at(i).col;
            r.row = want.at(i).row;
        }
        placed[i] = r;
        fixed[i] = true;
        next[i] = withRect(cells.at(i).toMap(), r);
    }
    out.ok = true;
    out.cells = next;
    return out;
}

Rect minSpan(int cols, int rows, double cellW, double cellH, double spacing, double minW, double minH)
{
    auto span = [spacing](double cell, double want, int limit) {
        int n = 1;
        while (n < limit && n * cell + (n - 1) * spacing < want - 0.5)
            ++n;
        return n;
    };
    return Rect{0, 0, span(cellW, minW, qMax(1, cols)), span(cellH, minH, qMax(1, rows))};
}

QList<SizeOption> sizeOptions(int cols, int rows, double cellW, double cellH, double spacing,
                              double minW, double minH, double aspect)
{
    QList<SizeOption> out;
    if (cols < 1 || rows < 1)
        return out;
    const Rect least = minSpan(cols, rows, cellW, cellH, spacing, minW, minH);
    const double want = aspect > 0 ? aspect : 1.0;
    const struct { const char *label; double share; } steps[] = {{"S", 1.0 / 12}, {"M", 1.0 / 6}, {"L", 1.0 / 3}};
    for (const auto &step : steps) {
        const double area = step.share * cols * rows;
        SizeOption best;
        double bestScore = std::numeric_limits<double>::max();
        for (int cs = least.colSpan; cs <= cols; ++cs) {
            for (int rs = least.rowSpan; rs <= rows; ++rs) {
                const double w = cs * cellW + (cs - 1) * spacing;
                const double h = rs * cellH + (rs - 1) * spacing;
                // Off in area and off in shape, both as log ratios
                const double score = std::abs(std::log((cs * rs) / area))
                                   + 1.5 * std::abs(std::log((w / qMax(1e-6, h)) / want));
                if (score < bestScore) {
                    bestScore = score;
                    best = {QString::fromLatin1(step.label), cs, rs};
                }
            }
        }
        if (!out.isEmpty() && out.last().colSpan == best.colSpan && out.last().rowSpan == best.rowSpan)
            continue;
        if (!out.isEmpty() && out.last().colSpan * out.last().rowSpan >= best.colSpan * best.rowSpan)
            continue;
        out.append(best);
    }
    return out;
}

} // namespace DashboardGrid
