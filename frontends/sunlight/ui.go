package main

import (
	"fmt"
	"image"
	"image/color"
	"math"
	"unicode/utf8"

	"gioui.org/f32"
	"gioui.org/font"
	"gioui.org/font/gofont"
	"gioui.org/gesture"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/text"
	"gioui.org/unit"
	"gioui.org/widget"
	"gioui.org/widget/material"
)

type C = layout.Context

// Palette: two inks and one accent. Night swaps the inks; the accent stays.
type Palette struct {
	Paper, Ink, Accent, Dim color.NRGBA
}

var (
	day   = Palette{Paper: rgb(0xFFFFFF), Ink: rgb(0x000000), Accent: rgb(0xFF3B00), Dim: rgb(0xBDBDBD)}
	night = Palette{Paper: rgb(0x000000), Ink: rgb(0xFFFFFF), Accent: rgb(0xFF3B00), Dim: rgb(0x4A4A4A)}
)

func rgb(c uint32) color.NRGBA {
	return color.NRGBA{R: uint8(c >> 16), G: uint8(c >> 8), B: uint8(c), A: 0xFF}
}

var (
	monoBold = font.Font{Typeface: "Go Mono", Weight: font.Bold}
	sansBold = font.Font{Typeface: "Go", Weight: font.Bold}
)

// Painter draws the primitives everything else is built from: filled and
// outlined rectangles, thick lines, arcs, fitted text and touch buttons.
type Painter struct {
	th  *material.Theme
	pal Palette
	gtx C
	px  func(dp float32) int
}

func newTheme() *material.Theme {
	th := material.NewTheme()
	th.Shaper = text.NewShaper(text.NoSystemFonts(), text.WithCollection(gofont.Collection()))
	return th
}

func (p *Painter) begin(gtx C, pal Palette) {
	p.gtx, p.pal = gtx, pal
	p.px = func(dp float32) int { return gtx.Dp(unit.Dp(dp)) }
}

// Rule is the thickness of every line in the UI.
func (p *Painter) Rule() int { return max(3, p.px(5)) }

func (p *Painter) Fill(r image.Rectangle, c color.NRGBA) {
	paint.FillShape(p.gtx.Ops, c, clip.Rect(r).Op())
}

// Box outlines r with the rule thickness, inside r.
func (p *Painter) Box(r image.Rectangle, c color.NRGBA) {
	t := p.Rule()
	p.Fill(image.Rect(r.Min.X, r.Min.Y, r.Max.X, r.Min.Y+t), c)
	p.Fill(image.Rect(r.Min.X, r.Max.Y-t, r.Max.X, r.Max.Y), c)
	p.Fill(image.Rect(r.Min.X, r.Min.Y, r.Min.X+t, r.Max.Y), c)
	p.Fill(image.Rect(r.Max.X-t, r.Min.Y, r.Max.X, r.Max.Y), c)
}

// Line strokes a polyline.
func (p *Painter) Line(pts []f32.Point, width float32, c color.NRGBA) {
	if len(pts) < 2 {
		return
	}
	var path clip.Path
	path.Begin(p.gtx.Ops)
	path.MoveTo(pts[0])
	for _, pt := range pts[1:] {
		path.LineTo(pt)
	}
	paint.FillShape(p.gtx.Ops, c, clip.Stroke{Path: path.End(), Width: width}.Op())
}

// Polygon fills a closed shape.
func (p *Painter) Polygon(pts []f32.Point, c color.NRGBA) {
	if len(pts) < 3 {
		return
	}
	var path clip.Path
	path.Begin(p.gtx.Ops)
	path.MoveTo(pts[0])
	for _, pt := range pts[1:] {
		path.LineTo(pt)
	}
	path.Close()
	paint.FillShape(p.gtx.Ops, c, clip.Outline{Path: path.End()}.Op())
}

// Arc strokes a circular arc; angles in degrees, 0 = 3 o'clock, clockwise (as in QML).
func (p *Painter) Arc(center f32.Point, radius, startDeg, sweepDeg, width float32, c color.NRGBA) {
	if sweepDeg == 0 {
		return
	}
	steps := max(2, int(math.Abs(float64(sweepDeg))/3))
	pts := make([]f32.Point, 0, steps+1)
	for i := 0; i <= steps; i++ {
		pts = append(pts, polar(center, radius, startDeg+sweepDeg*float32(i)/float32(steps)))
	}
	p.Line(pts, width, c)
}

func polar(c f32.Point, r, deg float32) f32.Point {
	rad := float64(deg) * math.Pi / 180
	return f32.Pt(c.X+r*float32(math.Cos(rad)), c.Y+r*float32(math.Sin(rad)))
}

// Disc fills a circle.
func (p *Painter) Disc(center f32.Point, radius float32, c color.NRGBA) {
	r := image.Rect(int(center.X-radius), int(center.Y-radius), int(center.X+radius), int(center.Y+radius))
	paint.FillShape(p.gtx.Ops, c, clip.Ellipse(r).Op(p.gtx.Ops))
}

// Align for Text.
type Align int

const (
	Left Align = iota
	Center
	Right
)

// Text draws s in one line inside r, as large as fits up to maxPx tall
// (0: as large as r allows), vertically centred.
func (p *Painter) Text(r image.Rectangle, s string, f font.Font, maxPx int, align Align, c color.NRGBA) {
	if s == "" || r.Dx() <= 0 || r.Dy() <= 0 {
		return
	}
	n := float32(max(1, utf8.RuneCountInString(s)))
	advance := float32(0.62) // Go Mono's advance per em; the sans is narrower on average
	if f.Typeface == "Go" {
		advance = 0.7 // bold capitals run wide
	}
	size := min(float32(r.Dy())/1.18, float32(r.Dx())/(n*advance))
	if maxPx > 0 {
		size = min(size, float32(maxPx))
	}
	if size < 4 {
		return
	}
	lbl := material.Label(p.th, unit.Sp(size/p.gtx.Metric.PxPerSp), s)
	lbl.Font, lbl.Color, lbl.MaxLines = f, c, 1
	gtx := p.gtx
	gtx.Constraints = layout.Constraints{Max: image.Pt(r.Dx()*2, r.Dy()*2)}
	m := op.Record(gtx.Ops)
	dims := lbl.Layout(gtx)
	call := m.Stop()
	x := r.Min.X
	switch align {
	case Center:
		x += (r.Dx() - dims.Size.X) / 2
	case Right:
		x = r.Max.X - dims.Size.X
	}
	y := r.Min.Y + (r.Dy()-dims.Size.Y)/2
	st := op.Offset(image.Pt(x, y)).Push(gtx.Ops)
	call.Add(gtx.Ops)
	st.Pop()
}

// Button draws a touch button over r and reports a tap. Active buttons are inverted.
func (p *Painter) Button(r image.Rectangle, clk *widget.Clickable, label string, active bool) bool {
	clicked := clk.Clicked(p.gtx)
	bg, fg := p.pal.Paper, p.pal.Ink
	if active || clk.Pressed() {
		bg, fg = p.pal.Ink, p.pal.Paper
	}
	p.Fill(r, bg)
	p.Box(r, p.pal.Ink)
	pad := p.Rule() * 3
	p.Text(r.Inset(pad), label, sansBold, p.px(34), Center, fg)
	gtx := p.gtx
	gtx.Constraints = layout.Exact(r.Size())
	st := op.Offset(r.Min).Push(gtx.Ops)
	clk.Layout(gtx, func(gtx C) layout.Dimensions { return layout.Dimensions{Size: r.Size()} })
	st.Pop()
	return clicked
}

// Tap reports where r was tapped (relative to r), if it was.
func (p *Painter) Tap(r image.Rectangle, g *gesture.Click) (image.Point, bool) {
	var at image.Point
	tapped := false
	for {
		e, ok := g.Update(p.gtx.Source)
		if !ok {
			break
		}
		if e.Kind == gesture.KindClick {
			at, tapped = e.Position, true
		}
	}
	st := op.Offset(r.Min).Push(p.gtx.Ops)
	area := clip.Rect(image.Rectangle{Max: r.Size()}).Push(p.gtx.Ops)
	g.Add(p.gtx.Ops)
	area.Pop()
	st.Pop()
	return at, tapped
}

// Format renders a value with its decimals; "--" when unknown.
func Format(v float64, ok bool, decimals int) string {
	if !ok || math.IsNaN(v) {
		return "--"
	}
	return fmt.Sprintf("%.*f", max(0, decimals), v)
}

func clamp01(v float64) float64 { return max(0, min(1, v)) }

// split cuts r into n columns (or rows) with gap pixels between them.
func split(r image.Rectangle, n, gap int, vertical bool) []image.Rectangle {
	out := make([]image.Rectangle, n)
	for i := 0; i < n; i++ {
		if vertical {
			h := (r.Dy() + gap) / n
			out[i] = image.Rect(r.Min.X, r.Min.Y+i*h, r.Max.X, r.Min.Y+(i+1)*h-gap)
		} else {
			w := (r.Dx() + gap) / n
			out[i] = image.Rect(r.Min.X+i*w, r.Min.Y, r.Min.X+(i+1)*w-gap, r.Max.Y)
		}
	}
	if n > 0 { // the last one takes the rounding
		if vertical {
			out[n-1].Max.Y = r.Max.Y
		} else {
			out[n-1].Max.X = r.Max.X
		}
	}
	return out
}

func clipRect(ops *op.Ops, r image.Rectangle) clip.Stack { return clip.Rect(r).Push(ops) }

func affine(origin image.Point, scale float32) f32.Affine2D {
	return f32.AffineId().Scale(f32.Point{}, f32.Pt(scale, scale)).Offset(pt(origin))
}
