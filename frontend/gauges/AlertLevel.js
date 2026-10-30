// AlertLevel.js
//
// The shared warning / critical rule of the value gauges (Circular, Arc,
// Bar, Segmented, Linear, DigitalReadout, Sparkline). A gauge declares
// warnAbove / warnBelow / critAbove / critBelow (NaN = unset, in the
// gauge's display unit) and colours its value warnColor at level 1 and
// critColor at level 2. docs/GAUGE_AUTHORING.md, "Alerts".
.pragma library

// 0 = normal, 1 = warning, 2 = critical; 0 without a finite reading.
// A limit is inclusive: at critAbove the value is already critical.
function level(v, warnAbove, warnBelow, critAbove, critBelow) {
    if (!isFinite(v)) return 0
    if ((isFinite(critAbove) && v >= critAbove) || (isFinite(critBelow) && v <= critBelow)) return 2
    if ((isFinite(warnAbove) && v >= warnAbove) || (isFinite(warnBelow) && v <= warnBelow)) return 1
    return 0
}

// The bands to draw on a scale from min to max, as fractions of it:
// [{from, to, level}], warning bands stopping where the critical ones start.
// Empty for an unusable range.
function bands(min, max, warnAbove, warnBelow, critAbove, critBelow) {
    var out = []
    if (!(isFinite(min) && isFinite(max) && max > min)) return out
    function t(v) { return Math.max(0, Math.min(1, (v - min) / (max - min))) }
    function add(a, b, lvl) {
        var from = t(a), to = t(b)
        if (to - from > 0.001) out.push({ "from": from, "to": to, "level": lvl })
    }
    if (isFinite(warnAbove))
        add(warnAbove, (isFinite(critAbove) && critAbove > warnAbove) ? critAbove : max, 1)
    if (isFinite(critAbove)) add(critAbove, max, 2)
    if (isFinite(warnBelow))
        add((isFinite(critBelow) && critBelow < warnBelow) ? critBelow : min, warnBelow, 1)
    if (isFinite(critBelow)) add(min, critBelow, 2)
    return out
}
