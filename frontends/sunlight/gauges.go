package main

import (
	"image"
	"math"

	"gioui.org/f32"
	"gioui.org/widget"

	"octave/sunlight/dash"
	"octave/sunlight/link"
)

// reading is a cell's value with everything needed to draw it: the PID's
// catalog row, overridden by the cell's props (title, unit, min, max, decimals).
type reading struct {
	title, unit string
	min, max    float64
	decimals    int
	value       float64
	ok          bool
}

func readCell(st *link.State, c dash.Cell) reading {
	p, _ := st.Param(c.ParamID)
	r := reading{title: p.Title, unit: p.Unit, min: p.Min, max: p.Max, decimals: p.Decimals}
	if r.title == "" {
		r.title = c.ParamID
	}
	r.title = c.Str("title", r.title)
	r.unit = c.Str("unit", r.unit)
	r.min = c.Num("min", r.min)
	r.max = c.Num("max", r.max)
	r.decimals = int(c.Num("decimals", float64(r.decimals)))
	if r.max <= r.min {
		r.max = r.min + 1
	}
	r.value, r.ok = st.Value(c.ParamID)
	return r
}

func (r reading) frac() float64 { return clamp01((r.value - r.min) / (r.max - r.min)) }
func (r reading) text() string  { return Format(r.value, r.ok, r.decimals) }

// cellUI keeps per-cell input state across frames (media buttons, taps).
type cellUI struct {
	prev, play, next widget.Clickable
}

// drawCell renders one dashboard cell inside r.
func (a *App) drawCell(p *Painter, r image.Rectangle, c dash.Cell, ui *cellUI) {
	st := a.session.State
	pal := p.pal
	p.Box(r, pal.Ink)
	in := r.Inset(p.Rule() * 2)
	switch c.Type {
	case "DigitalReadout":
		rd := readCell(st, c)
		rows := splitTitle(in, c.Bool("showTitle", true))
		if c.Bool("showTitle", true) {
			p.Text(rows[0], upper(rd.title), sansBold, 0, Left, pal.Ink)
		}
		txt := rd.text()
		if pad := int(c.Num("padDigits", 0)); pad > 0 && rd.ok {
			for len(txt) < pad {
				txt = "0" + txt
			}
		}
		body := rows[1]
		if c.Bool("showUnit", true) && rd.unit != "" {
			body, unitR := cutRight(body, body.Dx()/5)
			p.Text(body, txt, monoBold, 0, Right, pal.Ink)
			p.Text(image.Rect(unitR.Min.X+p.Rule(), unitR.Max.Y-unitR.Dy()/3, unitR.Max.X, unitR.Max.Y), rd.unit, sansBold, 0, Left, pal.Ink)
		} else {
			p.Text(body, txt, monoBold, 0, Center, pal.Ink)
		}
	case "CircularGauge":
		a.dial(p, in, c, 135, 270, false)
	case "ArcGauge":
		a.dial(p, in, c, 180, 180, true)
	case "BarGauge":
		a.bar(p, in, c)
	case "LinearGauge":
		a.linear(p, in, c)
	case "SparklineGauge":
		a.sparkline(p, in, c)
	case "WarningLight":
		a.warning(p, r, c)
	case "GForceGauge":
		a.gforce(p, in, c)
	case "CompassGauge":
		a.compass(p, in, c)
	case "NowPlayingWidget":
		a.nowPlayingCell(p, in, c)
	case "MediaControlsWidget":
		b := split(in, 3, p.Rule()*2, false)
		if p.Button(b[0], &ui.prev, "PREV", false) {
			a.session.Go("nowPlaying", "previous")
		}
		playing := st.Bool("nowPlaying", "playing")
		if p.Button(b[1], &ui.play, map[bool]string{true: "PAUSE", false: "PLAY"}[playing], playing) {
			a.session.Go("nowPlaying", "togglePlay")
		}
		if p.Button(b[2], &ui.next, "NEXT", false) {
			a.session.Go("nowPlaying", "next")
		}
	default: // a type sunlight does not draw: a crossed placeholder, like OCTAVE's
		p.Line([]f32.Point{pt(in.Min), pt(in.Max)}, float32(p.Rule()), pal.Dim)
		p.Line([]f32.Point{f32.Pt(float32(in.Min.X), float32(in.Max.Y)), f32.Pt(float32(in.Max.X), float32(in.Min.Y))}, float32(p.Rule()), pal.Dim)
		p.Text(in.Inset(in.Dy()/3), upper(c.Type), sansBold, 0, Center, pal.Ink)
	}
}

func pt(q image.Point) f32.Point { return f32.Pt(float32(q.X), float32(q.Y)) }

func splitTitle(r image.Rectangle, title bool) [2]image.Rectangle {
	if !title {
		return [2]image.Rectangle{{}, r}
	}
	h := max(r.Dy()/5, 12)
	return [2]image.Rectangle{image.Rect(r.Min.X, r.Min.Y, r.Max.X, r.Min.Y+h), image.Rect(r.Min.X, r.Min.Y+h, r.Max.X, r.Max.Y)}
}

func cutRight(r image.Rectangle, w int) (image.Rectangle, image.Rectangle) {
	return image.Rect(r.Min.X, r.Min.Y, r.Max.X-w, r.Max.Y), image.Rect(r.Max.X-w, r.Min.Y, r.Max.X, r.Max.Y)
}

// dial draws CircularGauge (full) and ArcGauge (half, readout under the chord).
func (a *App) dial(p *Painter, r image.Rectangle, c dash.Cell, start, sweep float32, half bool) {
	pal := p.pal
	rd := readCell(a.session.State, c)
	start = float32(c.Num("startAngle", float64(start)))
	sweep = float32(c.Num("sweepAngle", float64(sweep)))
	thick := float32(max(p.Rule()*2, min(r.Dx(), r.Dy())/12))
	var center f32.Point
	var radius float32
	var readout image.Rectangle
	if half {
		needle := c.Bool("showNeedle", false)
		k := float32(0.62)
		if needle {
			k = 0.55 // leave room under the chord: the needle sweeps the inside
		}
		radius = min(float32(r.Dx())/2, float32(r.Dy())*k) - thick/2
		center = f32.Pt(float32(r.Min.X+r.Dx()/2), float32(r.Min.Y)+radius+thick/2)
		readout = image.Rect(r.Min.X, int(center.Y)-int(radius*0.45), r.Max.X, r.Max.Y)
		if needle {
			readout.Min.Y = int(center.Y) + p.Rule()*3
		}
		if wideArc(p, r, radius, thick) { // the readout goes beside it: centre the arc vertically
			center.Y = float32(r.Min.Y) + (float32(r.Dy())+radius)/2
		}
	} else {
		radius = float32(min(r.Dx(), r.Dy()))/2 - thick/2
		center = f32.Pt(float32(r.Min.X+r.Dx()/2), float32(r.Min.Y+r.Dy()/2))
		w, h := int(radius*1.3), int(radius*0.95)
		readout = image.Rect(int(center.X)-w/2, int(center.Y)-h/2, int(center.X)+w/2, int(center.Y)+h/2)
	}
	redline := c.Num("redlineStart", math.NaN())
	hot := !math.IsNaN(redline) && rd.ok && rd.value >= redline
	p.Arc(center, radius, start, sweep, thick, pal.Dim)
	if !math.IsNaN(redline) && redline < rd.max {
		f := float32(clamp01((redline - rd.min) / (rd.max - rd.min)))
		p.Arc(center, radius+thick/2+float32(p.Rule()), start+sweep*f, sweep*(1-f), float32(p.Rule()), pal.Accent)
	}
	fill := pal.Ink
	if hot {
		fill = pal.Accent
	}
	if rd.ok {
		p.Arc(center, radius, start, sweep*float32(rd.frac()), thick, fill)
	}
	if c.Bool("showTicks", true) {
		n := max(2, int(c.Num("majorTickCount", map[bool]float64{true: 5, false: 9}[half])))
		for i := 0; i < n; i++ {
			ang := start + sweep*float32(i)/float32(n-1)
			p.Line([]f32.Point{polar(center, radius-thick/2-float32(p.Rule()), ang), polar(center, radius-thick*1.3, ang)}, float32(p.Rule()), pal.Ink)
		}
	}
	if c.Bool("showNeedle", false) && rd.ok {
		ang := start + sweep*float32(rd.frac())
		p.Line([]f32.Point{center, polar(center, radius-thick/2, ang)}, float32(p.Rule()), fill)
		p.Disc(center, float32(p.Rule()*2), pal.Ink)
	}
	if half && !c.Bool("showReadout", true) || !half && !c.Bool("showCenterReadout", true) {
		return
	}
	if half && wideArc(p, r, radius, thick) {
		side := r.Dx()/2 - int(radius+thick) - p.Rule()*2
		// a wide cell: title left of the arc, number and unit right of it, big
		cy := int(center.Y)
		top := max(r.Min.Y, cy-int(radius))
		left := image.Rect(r.Min.X, top, r.Min.X+side, cy)
		right := image.Rect(r.Max.X-side, top, r.Max.X, cy)
		p.Text(left, upper(rd.title), sansBold, 0, Left, pal.Ink)
		col := pal.Ink
		if hot {
			col = pal.Accent
		}
		num, unitR := cutRight(right, right.Dx()/4)
		p.Text(num, rd.text(), monoBold, 0, Right, col)
		p.Text(image.Rect(unitR.Min.X+p.Rule()*2, unitR.Min.Y+unitR.Dy()/2, unitR.Max.X, unitR.Max.Y), rd.unit, sansBold, 0, Left, pal.Ink)
		return
	}
	if !half && c.Bool("showNeedle", false) {
		// keep the hub clear: title above it, number and unit below
		w := int(radius * 1.1)
		cx, cy := int(center.X), int(center.Y)
		p.Text(image.Rect(cx-w/2, cy-int(radius*0.55), cx+w/2, cy-int(radius*0.3)), upper(rd.title), sansBold, 0, Center, pal.Ink)
		readout = image.Rect(cx-w/2, cy+int(radius*0.15), cx+w/2, cy+int(radius*0.8))
		rows := split(readout, 3, 0, true)
		big := image.Rect(readout.Min.X, rows[0].Min.Y, readout.Max.X, rows[1].Max.Y)
		col := pal.Ink
		if hot {
			col = pal.Accent
		}
		p.Text(big, rd.text(), monoBold, 0, Center, col)
		p.Text(rows[2], rd.unit, sansBold, 0, Center, pal.Ink)
		return
	}
	rows := split(readout, 4, 0, true)
	p.Text(rows[0], upper(rd.title), sansBold, 0, Center, pal.Ink)
	big := image.Rect(readout.Min.X, rows[1].Min.Y, readout.Max.X, rows[2].Max.Y)
	col := pal.Ink
	if hot {
		col = pal.Accent
	}
	p.Text(big, rd.text(), monoBold, 0, Center, col)
	p.Text(rows[3], rd.unit, sansBold, 0, Center, pal.Ink)
}

// wideArc: is there room for the readout on both sides of a half dial?
func wideArc(p *Painter, r image.Rectangle, radius, thick float32) bool {
	return r.Dx()/2-int(radius+thick)-p.Rule()*2 > int(radius)
}

func (a *App) bar(p *Painter, r image.Rectangle, c dash.Cell) {
	pal := p.pal
	rd := readCell(a.session.State, c)
	vertical := c.Str("orientation", "horizontal") == "vertical"
	warn := c.Num("warnAbove", math.NaN())
	fill := pal.Ink
	if !math.IsNaN(warn) && rd.ok && rd.value >= warn {
		fill = pal.Accent
	}
	head := image.Rect(r.Min.X, r.Min.Y, r.Max.X, r.Min.Y+max(r.Dy()/4, 16))
	if vertical {
		head.Max.Y = r.Min.Y + max(r.Dy()/8, 16)
	}
	halves := split(head, 2, p.Rule(), false)
	if c.Bool("showLabel", true) {
		p.Text(halves[0], upper(rd.title), sansBold, 0, Left, pal.Ink)
	}
	if c.Bool("showValue", true) {
		p.Text(halves[1], rd.text()+" "+rd.unit, monoBold, 0, Right, pal.Ink)
	}
	track := image.Rect(r.Min.X, head.Max.Y+p.Rule(), r.Max.X, r.Max.Y)
	if t := int(c.Num("maxThickness", 0)); t > 0 {
		t = p.px(float32(t))
		if vertical && track.Dx() > t {
			track = image.Rect(track.Min.X+(track.Dx()-t)/2, track.Min.Y, track.Min.X+(track.Dx()+t)/2, track.Max.Y)
		} else if !vertical && track.Dy() > t {
			track = image.Rect(track.Min.X, track.Min.Y+(track.Dy()-t)/2, track.Max.X, track.Min.Y+(track.Dy()+t)/2)
		}
	}
	p.Box(track, pal.Ink)
	inner := track.Inset(p.Rule() * 2)
	if rd.ok {
		f := rd.frac()
		if vertical {
			p.Fill(image.Rect(inner.Min.X, inner.Max.Y-int(float64(inner.Dy())*f), inner.Max.X, inner.Max.Y), fill)
		} else {
			p.Fill(image.Rect(inner.Min.X, inner.Min.Y, inner.Min.X+int(float64(inner.Dx())*f), inner.Max.Y), fill)
		}
	}
	if !math.IsNaN(warn) && warn > rd.min && warn < rd.max {
		f := (warn - rd.min) / (rd.max - rd.min)
		if vertical {
			y := inner.Max.Y - int(float64(inner.Dy())*f)
			p.Fill(image.Rect(track.Min.X, y-p.Rule()/2, track.Max.X, y+p.Rule()/2+1), pal.Accent)
		} else {
			x := inner.Min.X + int(float64(inner.Dx())*f)
			p.Fill(image.Rect(x-p.Rule()/2, track.Min.Y, x+p.Rule()/2+1, track.Max.Y), pal.Accent)
		}
	}
}

func (a *App) linear(p *Painter, r image.Rectangle, c dash.Cell) {
	pal := p.pal
	rd := readCell(a.session.State, c)
	rows := split(r, 3, p.Rule(), true)
	halves := split(rows[0], 2, p.Rule(), false)
	p.Text(halves[0], upper(rd.title), sansBold, 0, Left, pal.Ink)
	p.Text(halves[1], rd.text()+" "+rd.unit, monoBold, 0, Right, pal.Ink)
	scale := image.Rect(r.Min.X, rows[1].Min.Y, r.Max.X, rows[2].Max.Y)
	mid := scale.Min.Y + scale.Dy()/2
	p.Fill(image.Rect(scale.Min.X, mid-p.Rule()/2, scale.Max.X, mid+p.Rule()/2+1), pal.Ink)
	if c.Bool("showTicks", true) {
		n := max(2, int(c.Num("majorTickCount", 5)))
		for i := 0; i < n; i++ {
			x := scale.Min.X + (scale.Dx()-p.Rule())*i/(n-1)
			p.Fill(image.Rect(x, mid-scale.Dy()/3, x+p.Rule(), mid+scale.Dy()/3), pal.Ink)
		}
	}
	if !rd.ok {
		return
	}
	xOf := func(v float64) int { return scale.Min.X + int(float64(scale.Dx())*clamp01((v-rd.min)/(rd.max-rd.min))) }
	origin := scale.Min.X
	if c.Bool("centerOriginAtZero", rd.min < 0 && rd.max > 0) {
		origin = xOf(0)
	}
	x := xOf(rd.value)
	band := scale.Dy() / 5
	p.Fill(image.Rect(min(origin, x), mid-band, max(origin, x), mid+band), pal.Accent)
	p.Fill(image.Rect(x-p.Rule(), scale.Min.Y, x+p.Rule(), scale.Max.Y), pal.Ink)
}

func (a *App) sparkline(p *Painter, r image.Rectangle, c dash.Cell) {
	pal := p.pal
	st := a.session.State
	rd := readCell(st, c)
	plot := r
	if c.Bool("showHeader", true) {
		head := image.Rect(r.Min.X, r.Min.Y, r.Max.X, r.Min.Y+max(r.Dy()/4, 16))
		halves := split(head, 2, p.Rule(), false)
		p.Text(halves[0], upper(rd.title), sansBold, 0, Left, pal.Ink)
		p.Text(halves[1], rd.text()+" "+rd.unit, monoBold, 0, Right, pal.Ink)
		plot.Min.Y = head.Max.Y + p.Rule()
	}
	p.Box(plot, pal.Ink)
	plot = plot.Inset(p.Rule() * 2)
	h := st.History(c.ParamID)
	if n := int(c.Num("maxSamples", 60)) * 4; len(h) > n { // State samples at 50 ms batches, OCTAVE at 500 ms: keep a similar window
		h = h[len(h)-n:]
	}
	if len(h) < 2 {
		return
	}
	lo, hi := rd.min, rd.max
	if c.Bool("autoScale", false) {
		lo, hi = h[0], h[0]
		for _, v := range h {
			lo, hi = min(lo, v), max(hi, v)
		}
		if hi-lo < 1e-9 {
			lo, hi = lo-1, hi+1
		}
	}
	pts := make([]f32.Point, len(h))
	for i, v := range h {
		x := float32(plot.Min.X) + float32(plot.Dx())*float32(i)/float32(len(h)-1)
		y := float32(plot.Max.Y) - float32(plot.Dy())*float32(clamp01((v-lo)/(hi-lo)))
		pts[i] = f32.Pt(x, y)
	}
	if c.Bool("fillBelow", true) {
		area := append([]f32.Point{f32.Pt(pts[0].X, float32(plot.Max.Y))}, pts...)
		area = append(area, f32.Pt(pts[len(pts)-1].X, float32(plot.Max.Y)))
		p.Polygon(area, pal.Dim)
	}
	p.Line(pts, float32(p.Rule()), pal.Ink)
}

func (a *App) warning(p *Painter, r image.Rectangle, c dash.Cell) {
	pal := p.pal
	rd := readCell(a.session.State, c)
	above, below := c.Num("triggerAbove", math.NaN()), c.Num("triggerBelow", math.NaN())
	lit := rd.ok && ((!math.IsNaN(above) && rd.value >= above) || (!math.IsNaN(below) && rd.value <= below))
	if lit && c.Bool("pulse", false) && p.gtx.Now.UnixMilli()/500%2 == 1 {
		lit = false
	}
	if lit {
		p.Fill(r.Inset(p.Rule()), pal.Accent)
	}
	label := c.Str("label", rd.title)
	col := pal.Ink
	if lit {
		col = pal.Paper
	}
	p.Text(r.Inset(max(p.Rule()*3, r.Dy()/5)), upper(label), sansBold, 0, Center, col)
}

func (a *App) gforce(p *Painter, r image.Rectangle, c dash.Cell) {
	pal := p.pal
	st := a.session.State
	maxG := c.Num("maxG", 1.5)
	radius := float32(min(r.Dx(), r.Dy()))/2 - float32(p.Rule())
	center := f32.Pt(float32(r.Min.X+r.Dx()/2), float32(r.Min.Y+r.Dy()/2))
	for i := 1; i <= 3; i++ {
		p.Arc(center, radius*float32(i)/3, 0, 360, float32(p.Rule())/2+1, pal.Ink)
	}
	p.Line([]f32.Point{f32.Pt(center.X-radius, center.Y), f32.Pt(center.X+radius, center.Y)}, 2, pal.Ink)
	p.Line([]f32.Point{f32.Pt(center.X, center.Y-radius), f32.Pt(center.X, center.Y+radius)}, 2, pal.Ink)
	x, okx := st.Value("LATERAL_G")
	y, oky := st.Value("LONGITUDINAL_G")
	if !okx || !oky {
		p.Text(image.Rect(r.Min.X, r.Max.Y-r.Dy()/6, r.Max.X, r.Max.Y), "NO IMU", sansBold, 0, Center, pal.Ink)
		return
	}
	k := float64(radius) / maxG
	dot := f32.Pt(center.X+float32(clampAbs(x, maxG)*k), center.Y-float32(clampAbs(y, maxG)*k))
	p.Disc(dot, float32(p.Rule()*3), pal.Accent)
	if c.Bool("showValue", true) {
		p.Text(image.Rect(r.Min.X, r.Max.Y-r.Dy()/6, r.Max.X, r.Max.Y), Format(math.Hypot(x, y), true, 2)+" G", monoBold, 0, Right, pal.Ink)
	}
}

func clampAbs(v, lim float64) float64 { return max(-lim, min(lim, v)) }

func (a *App) compass(p *Painter, r image.Rectangle, c dash.Cell) {
	pal := p.pal
	heading, ok := a.session.State.Value("HEADING")
	radius := float32(min(r.Dx(), r.Dy()))/2 - float32(p.Rule())
	center := f32.Pt(float32(r.Min.X+r.Dx()/2), float32(r.Min.Y+r.Dy()/2))
	p.Arc(center, radius, 0, 360, float32(p.Rule()), pal.Ink)
	// fixed lubber line at the top; the card turns under it
	p.Fill(image.Rect(int(center.X)-p.Rule()/2, int(center.Y-radius)-p.Rule(), int(center.X)+p.Rule()/2+1, int(center.Y-radius*0.75)), pal.Accent)
	for i, name := range []string{"N", "E", "S", "W"} {
		ang := float32(float64(i)*90-heading) - 90
		lp := polar(center, radius*0.8, ang)
		s := int(radius * 0.25)
		col := pal.Ink
		if name == "N" {
			col = pal.Accent
		}
		p.Text(image.Rect(int(lp.X)-s, int(lp.Y)-s, int(lp.X)+s, int(lp.Y)+s), name, sansBold, 0, Center, col)
	}
	if c.Bool("showDegrees", true) {
		s := int(radius * 0.45)
		txt := "--"
		if ok {
			txt = Format(math.Mod(heading+360, 360), true, 0) + "°"
		}
		p.Text(image.Rect(int(center.X)-s, int(center.Y)-s/2, int(center.X)+s, int(center.Y)+s/2), txt, monoBold, 0, Center, pal.Ink)
	}
}

func (a *App) nowPlayingCell(p *Painter, r image.Rectangle, c dash.Cell) {
	pal := p.pal
	st := a.session.State
	rows := split(r, 4, p.Rule(), true)
	title := st.Str("nowPlaying", "title")
	if title == "" {
		title = "NOTHING PLAYING"
	}
	p.Text(image.Rect(r.Min.X, rows[0].Min.Y, r.Max.X, rows[1].Max.Y), title, sansBold, 0, Left, pal.Ink)
	if c.Bool("showArtist", true) {
		p.Text(rows[2], st.Str("nowPlaying", "artist"), sansBold, 0, Left, pal.Ink)
	}
	if c.Bool("showProgress", true) {
		bar := rows[3]
		bar.Min.Y = bar.Max.Y - max(p.Rule()*3, bar.Dy()/2)
		p.Box(bar, pal.Ink)
		if d := st.Num("nowPlaying", "durationMs"); d > 0 {
			in := bar.Inset(p.Rule() * 2)
			p.Fill(image.Rect(in.Min.X, in.Min.Y, in.Min.X+int(float64(in.Dx())*clamp01(st.Num("nowPlaying", "positionMs")/d)), in.Max.Y), pal.Ink)
		}
	}
}
