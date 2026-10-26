#ifndef POINTERPATH_H
#define POINTERPATH_H

// Touches on a mirrored iPhone turned into relative mouse reports. iOS only
// takes a relative pointer (Settings > Accessibility > Touch > AssistiveTouch
// with a Bluetooth mouse), so a tap at a spot becomes: push the pointer into
// the top-left corner (where it stops, so its position is known), move it to
// the spot in small steps (small steps keep iOS's pointer acceleration out of
// the way), click. While the position stays known, later taps move from
// there. `scale` is the calibration: mouse counts across the screen's short
// side, so the long side takes proportionally more.

#include <QList>
#include <QPointF>
#include <QSizeF>

namespace PointerPath {

struct Report {
    int dx = 0;
    int dy = 0;
    bool button = false;
    int wheel = 0;
    int pan = 0;
    bool operator==(const Report &o) const
    {
        return dx == o.dx && dy == o.dy && button == o.button && wheel == o.wheel && pan == o.pan;
    }
};

constexpr int kStep = 12;          // counts per report while placing the pointer
constexpr int kHomeReports = 40;   // full-speed reports into the corner
constexpr int kHomeStep = 127;

// Mouse counts across the screen's width and height for a picture of `screen`
// (any unit; only the aspect matters)
QSizeF countsAcross(const QSizeF &screen, double scale);

// Reports that put the pointer in the top-left corner from anywhere
QList<Report> home();

// Reports that move from `from` to `to` (0..1 of the screen) with the button
// held or not; the moves are rounded to whole counts, the remainder is
// carried so a long drag doesn't drift
QList<Report> move(QPointF from, QPointF to, const QSizeF &counts, bool button, QPointF *carry = nullptr);

} // namespace PointerPath

#endif // POINTERPATH_H
