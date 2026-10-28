.pragma library

// Where every control of the bottom bar (or the side bar) goes, as one pure
// function of the bar's size and what is switched on. BottomBar.qml only
// places items at the rects this returns; tst_bottombarlayout runs it over a
// grid of sizes and checks the promises below.
//
// Promises, at any size:
//   - every face (the drawn part) and every hit area lies inside the bar
//   - no two faces overlap, and no two hit areas overlap
//   - sizes stay in proportion: everything is a multiple of one unit (the
//     nav button), so the bar shrinks as a whole instead of piece by piece
//   - the media buttons are nav buttons in all but colour: the same size,
//     the same gap between them; the auto clock's digits stand as tall as
//     the nav buttons' icons
//
// How it fits, along the bar's length:
//   1. at the ideal unit (from the bar's thickness) with comfortable gaps
//   2. gaps tighten (margins and group gaps first, then the gaps inside a
//      group), then the clock gives up some size
//   3. everything scales down together
//   4. if that would take the nav buttons below minUnit, controls are dropped
//      in order: shuffle, volume %, mute, prev / play / next, clock. The nav
//      buttons are never dropped; with nothing left to drop they shrink to fit.
//      A control that would be too small to use on its own (the clock, or
//      prev / play / next across a narrow side bar) is dropped by itself.

// Sizes in nav-button units (the nav button is 1 x 1)
var MEDIA = 1                // prev / play / next / shuffle / mute
var ICON = 0.7               // icon in a nav or media button (BottomBar.qml draws it)
var VOLUME_FONT = 0.36       // volume % pixel size
var CLOCK_DIGITS = ICON      // auto clock digit height...
var CLOCK_DIGITS_MIN = 0.45  // ...and as far as it may give way
var CLOCK_PAD = 0.3          // around the clock text (its tap area)
var VOLUME_PAD = 0.1
var SIDE_NAV_ASPECT = 1.4    // side bar: nav buttons are wider than tall

// [preferred, minimum] gaps in units
var MARGIN = [0.3, 0.12], GROUP_GAP = [0.6, 0.2], NAV_GAP = [0.3, 0.1]   // NAV_GAP between media buttons too

// Squeeze tiers: lower tiers give way first
var TIER_OUTER = 1, TIER_INNER = 2, TIER_CLOCK = 3

var DROP_ORDER = ["shuffle", "volume", "mute", "media", "clock"]

function _num(v, fallback) {
    return (typeof v === "number" && isFinite(v)) ? v : fallback
}

// Fit parts [{pref, min, tier}] into avail. Returns {k, sq: {tier: 0..1}}:
// sq[tier] is how far that tier moved from pref (0) to min (1); k scales
// everything once every tier is at its minimum.
function _solve(parts, avail) {
    var total = 0, tiers = []
    for (var i = 0; i < parts.length; ++i) {
        total += parts[i].pref
        if (tiers.indexOf(parts[i].tier) < 0) tiers.push(parts[i].tier)
    }
    tiers.sort(function (a, b) { return a - b })
    var sq = {}
    if (total <= avail) return { k: 1, sq: sq }
    for (var t = 0; t < tiers.length; ++t) {
        var range = 0
        for (var j = 0; j < parts.length; ++j)
            if (parts[j].tier === tiers[t]) range += parts[j].pref - parts[j].min
        if (range > 0 && total - range <= avail) {
            sq[tiers[t]] = (total - avail) / range
            return { k: 1, sq: sq }
        }
        sq[tiers[t]] = 1
        total -= range
    }
    return { k: total > 0 ? Math.max(0, avail / total) : 1, sq: sq }
}

function _size(part, s) {
    var q = s.sq[part.tier] || 0
    return s.k * (part.pref - q * (part.pref - part.min))
}

function _part(pref, min, tier) { return { pref: pref, min: min, tier: tier } }
function _gap(u, g, tier) { return _part(g[0] * u, g[1] * u, tier) }

function _hidden() { return { x: 0, y: 0, w: 0, h: 0, visible: false } }

// Snap edges (not sizes) to whole pixels: rects that do not overlap before
// rounding cannot overlap after it
function _rect(x, y, w, h) {
    var x0 = Math.round(x), y0 = Math.round(y)
    return { x: x0, y: y0, w: Math.max(0, Math.round(x + w) - x0), h: Math.max(0, Math.round(y + h) - y0), visible: true }
}

// Hit areas: each face grown by pad (clipped to the bar), then every pair
// that overlaps is cut at the midpoint between their faces along the axis the
// faces are furthest apart on. Cutting only shrinks, so pairs already
// separated stay separated.
function _hits(faces, barW, barH, padX, padY) {
    var keys = Object.keys(faces), hits = {}
    keys.forEach(function (k) {
        var f = faces[k]
        if (!f.visible) { hits[k] = _hidden(); return }
        hits[k] = { l: Math.max(0, f.x - padX), t: Math.max(0, f.y - padY),
                    r: Math.min(barW, f.x + f.w + padX), b: Math.min(barH, f.y + f.h + padY) }
    })
    for (var i = 0; i < keys.length; ++i) {
        var a = faces[keys[i]], ha = hits[keys[i]]
        if (!a.visible) continue
        for (var j = i + 1; j < keys.length; ++j) {
            var b = faces[keys[j]], hb = hits[keys[j]]
            if (!b.visible) continue
            if (ha.l >= hb.r || hb.l >= ha.r || ha.t >= hb.b || hb.t >= ha.b) continue
            var gapX = Math.max(b.x - (a.x + a.w), a.x - (b.x + b.w))
            var gapY = Math.max(b.y - (a.y + a.h), a.y - (b.y + b.h))
            var m
            if (gapX >= gapY) {
                if (a.x < b.x) { m = (a.x + a.w + b.x) / 2; ha.r = Math.min(ha.r, m); hb.l = Math.max(hb.l, m) }
                else { m = (b.x + b.w + a.x) / 2; hb.r = Math.min(hb.r, m); ha.l = Math.max(ha.l, m) }
            } else {
                if (a.y < b.y) { m = (a.y + a.h + b.y) / 2; ha.b = Math.min(ha.b, m); hb.t = Math.max(hb.t, m) }
                else { m = (b.y + b.h + a.y) / 2; hb.b = Math.min(hb.b, m); ha.t = Math.max(ha.t, m) }
            }
        }
    }
    keys.forEach(function (k) {
        var h = hits[k]
        if (h.visible === false) return
        // Floor the far edges so a cut at x.5 cannot round both ways
        var l = Math.ceil(h.l), t = Math.ceil(h.t)
        hits[k] = { x: l, y: t, w: Math.max(0, Math.floor(h.r) - l), h: Math.max(0, Math.floor(h.b) - t), visible: true }
    })
    return hits
}

// The horizontal bar: [media] ... [nav, centred on the bar] ... [clock]
function _horizontal(p, on) {
    var L = p.width, t = p.height
    var u = Math.min(0.68 * t * p.scale, 0.85 * t)
    var volPx = VOLUME_FONT * u
    var clockCap = 0.8 * t / p.clockHeightPerPx      // text no taller than 80% of the bar
    var clockPref = Math.min(p.clockAuto ? CLOCK_DIGITS * u / p.clockDigitHeightPerPx : p.clockPx, clockCap)
    var clockMin = p.clockAuto ? Math.min(clockPref, CLOCK_DIGITS_MIN * u / p.clockDigitHeightPerPx) : 0.7 * clockPref

    // Along the length, in order
    var media = []
    if (on.media) media.push(["prev", MEDIA * u], ["play", MEDIA * u], ["next", MEDIA * u])
    if (on.shuffle) media.push(["shuffle", MEDIA * u])
    if (on.mute) media.push(["mute", MEDIA * u])
    if (on.volume) media.push(["volume", p.volumeWidthPerPx * volPx + VOLUME_PAD * u])

    var parts = [], mediaParts = [], navParts = [], clockPart = null
    var margin = _gap(u, MARGIN, TIER_OUTER)
    parts.push(margin)
    for (var i = 0; i < media.length; ++i) {
        if (i > 0) { var g = _gap(u, NAV_GAP, TIER_INNER); parts.push(g); mediaParts.push(g) }
        var mp = _part(media[i][1], media[i][1], TIER_INNER)
        parts.push(mp); mediaParts.push(mp)
    }
    var gapLeft = _gap(u, GROUP_GAP, TIER_OUTER)
    if (media.length) parts.push(gapLeft)
    for (var n = 0; n < p.navCount; ++n) {
        if (n > 0) { var ng = _gap(u, NAV_GAP, TIER_INNER); parts.push(ng); navParts.push(ng) }
        var np = _part(u, u, TIER_INNER)
        parts.push(np); navParts.push(np)
    }
    var gapRight = _gap(u, GROUP_GAP, TIER_OUTER)
    if (on.clock) {
        parts.push(gapRight)
        clockPart = _part(p.clockWidthPerPx * clockPref + CLOCK_PAD * u,
                          p.clockWidthPerPx * clockMin + CLOCK_PAD * u, TIER_CLOCK)
        parts.push(clockPart)
    }
    var marginEnd = _gap(u, MARGIN, TIER_OUTER)
    parts.push(marginEnd)

    var s = _solve(parts, L)
    var ku = s.k * u
    var faces = {}, cy = t / 2

    // Media, packed from the left
    var x = _size(margin, s), mi = 0
    for (i = 0; i < media.length; ++i) {
        if (i > 0) x += _size(mediaParts[mi++], s)
        var w = _size(mediaParts[mi++], s)
        var h = media[i][0] === "volume" ? p.volumeHeightPerPx * volPx * s.k : w
        faces[media[i][0]] = _rect(x, cy - h / 2, w, h)
        x += w
    }
    var leftEdge = media.length ? x + _size(gapLeft, s) : _size(margin, s)

    // Clock, packed from the right
    var rightEdge = L - _size(marginEnd, s)
    var clockPxOut = 0
    if (on.clock) {
        var cw = _size(clockPart, s)
        clockPxOut = (cw - CLOCK_PAD * ku) / p.clockWidthPerPx
        // As tall as a nav button unless the digits need more
        var ch = Math.min(t, Math.max(ku, p.clockDigitHeightPerPx * clockPxOut + CLOCK_PAD * ku))
        faces.clock = _rect(rightEdge - cw, cy - ch / 2, cw, ch)
        rightEdge -= cw + _size(gapRight, s)
    }

    // Nav, centred on the bar when the sides leave room, else between them
    var navLen = 0
    for (i = 0; i < navParts.length; ++i) navLen += _size(navParts[i], s)
    var nx = Math.max(leftEdge, Math.min(rightEdge - navLen, (L - navLen) / 2))
    var nav = [], ni = 0
    for (n = 0; n < p.navCount; ++n) {
        if (n > 0) nx += _size(navParts[ni++], s)
        var nw = _size(navParts[ni++], s)
        nav.push(_rect(nx, cy - nw / 2, nw, nw))
        nx += nw
    }
    return { unit: ku, mediaUnit: ku, faces: faces, nav: nav, clockPx: clockPxOut, volumePx: volPx * s.k,
             padAlong: 0.5 * ku, along: "x" }
}

// The side bar: [media rows] ... [nav, centred] ... [clock], top to bottom
function _vertical(p, on) {
    var L = p.height, t = p.width
    var mx = 0.08 * t, w = t - 2 * mx
    var u = Math.min(w / SIDE_NAV_ASPECT * p.scale, w)
    // prev / play / next share one row, so the media unit is set by the width
    var rowUnits = 3 * MEDIA + 2 * NAV_GAP[1]
    var um = Math.min(u, w / rowUnits)
    var volPx = VOLUME_FONT * um
    var clockFit = (w - CLOCK_PAD * u) / p.clockWidthPerPx
    var clockPref = Math.max(0, Math.min(p.clockAuto ? CLOCK_DIGITS * u / p.clockDigitHeightPerPx : p.clockPx, clockFit))
    var clockMin = 0.7 * clockPref

    var rows = []
    if (on.media) rows.push({ h: MEDIA * um, items: [["prev", MEDIA * um], ["play", MEDIA * um], ["next", MEDIA * um]] })
    var row2 = []
    if (on.shuffle) row2.push(["shuffle", MEDIA * um])
    if (on.mute) row2.push(["mute", MEDIA * um])
    if (row2.length) rows.push({ h: MEDIA * um, items: row2 })
    if (on.volume) rows.push({ h: p.volumeHeightPerPx * volPx, items: [["volume", Math.min(w, p.volumeWidthPerPx * volPx + VOLUME_PAD * um)]] })

    var parts = [], rowParts = [], navParts = [], clockPart = null
    var margin = _gap(u, MARGIN, TIER_OUTER)
    parts.push(margin)
    for (var r = 0; r < rows.length; ++r) {
        if (r > 0) { var g = _gap(um, NAV_GAP, TIER_INNER); parts.push(g); rowParts.push(g) }
        var rp = _part(rows[r].h, rows[r].h, TIER_INNER)
        parts.push(rp); rowParts.push(rp)
    }
    var gapTop = _gap(u, GROUP_GAP, TIER_OUTER)
    if (rows.length) parts.push(gapTop)
    for (var n = 0; n < p.navCount; ++n) {
        if (n > 0) { var ng = _gap(u, NAV_GAP, TIER_INNER); parts.push(ng); navParts.push(ng) }
        var np = _part(u, u, TIER_INNER)
        parts.push(np); navParts.push(np)
    }
    var gapBottom = _gap(u, GROUP_GAP, TIER_OUTER)
    if (on.clock) {
        parts.push(gapBottom)
        clockPart = _part(p.clockHeightPerPx * clockPref + CLOCK_PAD * u,
                          p.clockHeightPerPx * clockMin + CLOCK_PAD * u, TIER_CLOCK)
        parts.push(clockPart)
    }
    var marginEnd = _gap(u, MARGIN, TIER_OUTER)
    parts.push(marginEnd)

    var s = _solve(parts, L)
    var faces = {}, cx = t / 2
    var rowGap = NAV_GAP[1] * um * s.k     // between items in a row (across the bar)

    var y = _size(margin, s), ri = 0
    for (r = 0; r < rows.length; ++r) {
        if (r > 0) y += _size(rowParts[ri++], s)
        var rh = _size(rowParts[ri++], s)
        var items = rows[r].items, rowW = 0
        for (var i = 0; i < items.length; ++i) rowW += items[i][1] * s.k + (i > 0 ? rowGap : 0)
        var x = cx - rowW / 2
        for (i = 0; i < items.length; ++i) {
            if (i > 0) x += rowGap
            var iw = items[i][1] * s.k
            var ih = items[i][0] === "volume" ? rh : iw
            faces[items[i][0]] = _rect(x, y + (rh - ih) / 2, iw, ih)
            x += iw
        }
        y += rh
    }
    var topEdge = rows.length ? y + _size(gapTop, s) : _size(margin, s)

    var bottomEdge = L - _size(marginEnd, s)
    var clockPxOut = 0
    if (on.clock) {
        var ch = _size(clockPart, s)
        clockPxOut = (ch - CLOCK_PAD * u * s.k) / p.clockHeightPerPx
        var cw = Math.min(w, p.clockWidthPerPx * clockPxOut + CLOCK_PAD * u * s.k)
        faces.clock = _rect(cx - cw / 2, bottomEdge - ch, cw, ch)
        bottomEdge -= ch + _size(gapBottom, s)
    }

    var navLen = 0
    for (i = 0; i < navParts.length; ++i) navLen += _size(navParts[i], s)
    var ny = Math.max(topEdge, Math.min(bottomEdge - navLen, (L - navLen) / 2))
    var nav = [], ni = 0
    var navW = Math.min(w, SIDE_NAV_ASPECT * u) * s.k
    for (n = 0; n < p.navCount; ++n) {
        if (n > 0) ny += _size(navParts[ni++], s)
        var nh = _size(navParts[ni++], s)
        nav.push(_rect(cx - navW / 2, ny, navW, nh))
        ny += nh
    }
    return { unit: u * s.k, mediaUnit: um * s.k, faces: faces, nav: nav, clockPx: clockPxOut, volumePx: volPx * s.k,
             padAlong: 0.5 * u * s.k, along: "y" }
}

// What has to go for g to be acceptable: "" when nothing, else the next
// name in DROP_ORDER, or a control that is too small however much room the
// rest gets (prev / play / next in a narrow side bar, the clock)
function _toDrop(g, on, minUnit, idealUnit) {
    if (on.media && MEDIA * g.mediaUnit < 0.6 * minUnit) return "media"
    if (on.clock && g.clockPx < Math.max(12, 0.35 * minUnit)) return "clock"
    if (g.unit >= Math.min(minUnit, idealUnit) - 0.01) return ""
    for (var d = 0; d < DROP_ORDER.length; ++d)
        if (on[DROP_ORDER[d]]) return DROP_ORDER[d]
    return ""
}

// p: {vertical, width, height, navCount, showMedia, showClock, clockAuto,
//     clockPx, clockWidthPerPx, clockHeightPerPx, clockDigitHeightPerPx,
//     volumeWidthPerPx, volumeHeightPerPx, minUnit, scale}
// Returns {unit, items: {prev, play, next, shuffle, mute, volume, clock},
//          hits: {same keys, nav0..}, nav: [rect], navHits: [rect],
//          clockPx, volumePx, dropped: [names]}
// Every rect is {x, y, w, h, visible} in bar coordinates.
function compute(p) {
    p = {
        vertical: !!p.vertical,
        width: Math.max(0, _num(p.width, 0)),
        height: Math.max(0, _num(p.height, 0)),
        navCount: Math.max(0, Math.floor(_num(p.navCount, 0))),
        showMedia: p.showMedia !== false,
        showClock: p.showClock !== false,
        clockAuto: p.clockAuto !== false,
        clockPx: Math.max(1, _num(p.clockPx, 18)),
        clockWidthPerPx: Math.max(0.1, _num(p.clockWidthPerPx, 2.6)),
        clockHeightPerPx: Math.max(0.1, _num(p.clockHeightPerPx, 1.2)),
        clockDigitHeightPerPx: Math.max(0.1, _num(p.clockDigitHeightPerPx, 0.72)),
        volumeWidthPerPx: Math.max(0.1, _num(p.volumeWidthPerPx, 2.4)),
        volumeHeightPerPx: Math.max(0.1, _num(p.volumeHeightPerPx, 1.2)),
        minUnit: Math.max(1, _num(p.minUnit, 32)),
        scale: Math.max(0.25, Math.min(4, _num(p.scale, 1)))
    }
    var on = { media: p.showMedia, shuffle: p.showMedia, mute: p.showMedia, volume: p.showMedia, clock: p.showClock }
    var dropped = []
    var build = p.vertical ? _vertical : _horizontal
    var idealUnit = p.vertical ? Math.min((p.width * 0.84) / SIDE_NAV_ASPECT * p.scale, p.width * 0.84)
                               : Math.min(0.68 * p.height * p.scale, 0.85 * p.height)
    var g = build(p, on)
    for (var what = _toDrop(g, on, p.minUnit, idealUnit); what !== ""; what = _toDrop(g, on, p.minUnit, idealUnit)) {
        on[what] = false
        if (what === "media") on.shuffle = on.mute = on.volume = false
        dropped.push(what)
        g = build(p, on)
    }

    var items = {}
    ;["prev", "play", "next", "shuffle", "mute", "volume", "clock"].forEach(function (k) {
        items[k] = g.faces[k] || _hidden()
    })
    var all = {}
    Object.keys(items).forEach(function (k) { all[k] = items[k] })
    g.nav.forEach(function (r, i) { all["nav" + i] = r })
    var bw = p.width, bh = p.height
    var hits = g.along === "x" ? _hits(all, bw, bh, g.padAlong, bh) : _hits(all, bw, bh, bw, g.padAlong)
    var navHits = g.nav.map(function (r, i) { return hits["nav" + i] })
    return { unit: g.unit, items: items, hits: hits, nav: g.nav, navHits: navHits,
             clockPx: Math.max(1, g.clockPx), volumePx: Math.max(1, g.volumePx), dropped: dropped }
}
