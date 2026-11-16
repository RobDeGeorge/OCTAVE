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

// The default hold margin of a gauge: 1 % of its range, whatever the range
// is (0.01 V on a 0 to 1 V oxygen sensor, 80 on an 8000 rpm tachometer).
// Only a gauge with no usable range falls back to one step of the PID's
// readout (10^-decimals, `decimals` being the parameter catalog's). It was
// "at least one whole step of the gauge's own decimals", which is 1 for a
// gauge that sets none: a lamp on a 0 to 1 V sensor could then never go
// out. `override` (the gauge's alertHysteresis, display unit) wins when it
// is a number >= 0.
function margin(min, max, decimals, override) {
    if (isFinite(override) && override >= 0) return override
    if (isFinite(min) && isFinite(max) && max > min) return (max - min) * 0.01
    var d = Math.max(0, Math.min(4, isFinite(decimals) ? decimals : 0))
    return Math.pow(10, -d)
}

// level() with a hold: a level is entered at its limit, as above, and left
// only once the value is back inside that limit by `hold`. `prev` is the
// level this gauge showed last. Without it a reading hovering at a limit
// changed level on every sample (33 ms): the colour flickered, the flash
// restarted and the dashboard's red banner popped in and out.
function levelHeld(prev, v, warnAbove, warnBelow, critAbove, critBelow, hold) {
    var now = level(v, warnAbove, warnBelow, critAbove, critBelow)
    if (!isFinite(v) || !(prev > now) || !(hold > 0)) return now
    var kept = level(v, warnAbove - hold, warnBelow + hold, critAbove - hold, critBelow + hold)
    return Math.max(now, Math.min(prev, kept))
}

// The same for a gauge, which keeps `memo` (a plain object, its own) between
// readings. New limits or a new margin start again from the plain rule, so
// an edit in the editor shows at once.
function track(memo, v, warnAbove, warnBelow, critAbove, critBelow, hold) {
    var key = warnAbove + "|" + warnBelow + "|" + critAbove + "|" + critBelow + "|" + hold
    var prev = memo.key === key ? memo.level : 0
    var now = levelHeld(prev, v, warnAbove, warnBelow, critAbove, critBelow, hold)
    memo.key = key
    memo.level = now
    return now
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
