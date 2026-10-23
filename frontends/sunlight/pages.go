package main

import (
	"fmt"
	"image"
	"strings"
	"time"

	"gioui.org/widget"
)

// clearWindow is how long the CLEAR button stays armed after the first tap.
const clearWindow = 4 * time.Second

// dtcUI and tripsUI keep the two pages' touch state across frames.
type dtcUI struct {
	read, clear  widget.Clickable
	armedUntil   time.Time
	loaded       bool
	lastClearMsg string
}

type trip struct {
	ID          int64   `json:"id"`
	StartIso    string  `json:"startIso"`
	DurationS   float64 `json:"durationS"`
	SampleCount int     `json:"sampleCount"`
	Open        bool    `json:"open"`
	Clean       bool    `json:"clean"`
	Make        string  `json:"make"`
	ModelYear   int     `json:"modelYear"`
}

type tripDetail struct {
	DistanceMiles float64 `json:"distanceMiles"`
	DurationS     float64 `json:"durationS"`
	MaxSpeed      float64 `json:"maxSpeed"`
	MaxRpm        float64 `json:"maxRpm"`
	MaxCoolant    float64 `json:"maxCoolant"`
	MaxOilTemp    float64 `json:"maxOilTemp"`
	MinVoltage    float64 `json:"minVoltage"`
	MaxVoltage    float64 `json:"maxVoltage"`
	FuelUsedPct   float64 `json:"fuelUsedPct"`
	SampleCount   int     `json:"sampleCount"`
	EventCount    int     `json:"eventCount"`
	ParamCount    int     `json:"paramCount"`
	StartIso      string  `json:"startIso"`
	EndIso        string  `json:"endIso"`
	Clean         bool    `json:"clean"`
	Open          bool    `json:"open"`
}

type tripsUI struct {
	rows      [8]widget.Clickable
	prev, nxt widget.Clickable
	page      int
	selected  int64
	requested map[int64]time.Time
}

// ── DTC ─────────────────────────────────────────────────────────────────

func (a *App) dtcPage(p *Painter, r image.Rectangle) {
	pal := p.pal
	st := a.session.State
	ui := &a.dtc
	gap := p.Rule() * 2
	codes, known, mil := st.DTCs()
	// opening the page reads the car (get_dtc_codes is only a cache, empty until a read)
	if !ui.loaded && a.session.Client() != nil {
		ui.loaded = true
		a.session.Go("obdManager", "read_dtc")
		go func() {
			var lamp bool
			if a.session.CallSync(&lamp, "obdManager", "get_mil_status") == nil {
				st.SetMIL(lamp)
				a.win.Invalidate()
			}
		}()
	}

	top := image.Rect(r.Min.X, r.Min.Y, r.Max.X, r.Min.Y+r.Dy()*24/100)
	halves := split(top, 2, gap, false)
	// the check-engine lamp: the one place the accent fills a whole box
	if mil {
		p.Fill(halves[0], pal.Accent)
		p.Text(halves[0].Inset(halves[0].Dy()/5), "CHECK ENGINE", sansBold, 0, Center, pal.Paper)
	} else {
		p.Box(halves[0], pal.Ink)
		p.Text(halves[0].Inset(halves[0].Dy()/5), "MIL OFF", sansBold, 0, Center, pal.Ink)
	}
	p.Box(halves[1], pal.Ink)
	count := "--"
	if known {
		count = fmt.Sprintf("%d", len(codes))
	}
	numR, labelR := cutRight(halves[1].Inset(p.Rule()*3), halves[1].Dx()/2)
	p.Text(numR, count, monoBold, 0, Right, pal.Ink)
	p.Text(labelR.Inset(labelR.Dy()/5), map[bool]string{true: " CODE", false: " CODES"}[len(codes) == 1], sansBold, 0, Left, pal.Ink)

	bottomH := max(p.px(80), r.Dy()/7)
	grid := image.Rect(r.Min.X, top.Max.Y+gap, r.Max.X, r.Max.Y-bottomH-gap)
	switch {
	case !known:
		p.Text(grid.Inset(grid.Dy()/3), "NOT READ YET", sansBold, 0, Center, pal.Ink)
	case len(codes) == 0:
		p.Text(grid.Inset(grid.Dy()/3), "NO STORED CODES", sansBold, 0, Center, pal.Ink)
	default:
		cols := min(4, len(codes))
		rows := (min(len(codes), 8) + cols - 1) / cols
		rowR := split(grid, rows, gap, true)
		for i, c := range codes {
			if i >= 8 {
				break
			}
			tile := split(rowR[i/cols], cols, gap, false)[i%cols]
			p.Box(tile, pal.Ink)
			in := tile.Inset(p.Rule() * 3)
			parts := split(in, 4, 0, true)
			p.Text(image.Rect(in.Min.X, parts[0].Min.Y, in.Max.X, parts[2].Max.Y), c.Code, monoBold, 0, Center, pal.Ink)
			desc := c.Description
			if desc == "" {
				desc = "NO DESCRIPTION"
			}
			p.Text(parts[3], upper(desc), sansBold, 0, Center, pal.Ink)
		}
		if len(codes) > 8 {
			p.Text(image.Rect(grid.Min.X, grid.Max.Y-gap*3, grid.Max.X, grid.Max.Y), fmt.Sprintf("+%d MORE", len(codes)-8), sansBold, 0, Right, pal.Ink)
		}
	}

	bottom := image.Rect(r.Min.X, r.Max.Y-bottomH, r.Max.X, r.Max.Y)
	b := split(bottom, 3, gap, false)
	if p.Button(b[0], &ui.read, "READ", false) {
		a.session.Go("obdManager", "read_dtc")
		a.session.Go("obdManager", "get_mil_status")
	}
	// CLEAR wants two taps within clearWindow: the first arms it (orange), the second clears
	now := p.gtx.Now
	armed := now.Before(ui.armedUntil)
	label := "CLEAR"
	if armed {
		label = "TAP AGAIN: CLEAR"
	}
	clearR := b[1]
	if armed {
		p.Fill(clearR, pal.Accent)
	}
	if p.ButtonColors(clearR, &ui.clear, label, armed) {
		if armed {
			ui.armedUntil = time.Time{}
			a.session.Go("obdManager", "clear_dtc")
		} else {
			ui.armedUntil = now.Add(clearWindow)
		}
	}
	if armed {
		p.gtx.Execute(invalidateAt(ui.armedUntil))
	}
	msg := st.ClearMessage()
	if !known {
		msg = ""
	}
	p.Text(b[2].Inset(p.Rule()*3), upper(msg), sansBold, 0, Center, pal.Ink)
}

// ── TRIPS ───────────────────────────────────────────────────────────────

func (a *App) tripsPage(p *Painter, r image.Rectangle) {
	pal := p.pal
	st := a.session.State
	ui := &a.trips
	if ui.requested == nil {
		ui.requested = map[int64]time.Time{}
	}
	gap := p.Rule() * 2
	var list []trip
	st.Prop("tripRecorder", "trips", &list)
	sortTrips(list)
	left := image.Rect(r.Min.X, r.Min.Y, r.Min.X+r.Dx()*45/100, r.Max.Y)
	right := image.Rect(left.Max.X+gap*2, r.Min.Y, r.Max.X, r.Max.Y)
	p.Fill(image.Rect(left.Max.X+gap-p.Rule()/2, r.Min.Y, left.Max.X+gap+p.Rule()/2+1, r.Max.Y), pal.Ink)

	if len(list) == 0 {
		p.Text(left.Inset(left.Dy()/3), "NO TRIPS", sansBold, 0, Center, pal.Ink)
		return
	}
	const perPage = 6
	pages := (len(list) + perPage - 1) / perPage
	ui.page = max(0, min(ui.page, pages-1))
	navH := max(p.px(64), r.Dy()/9)
	listR := image.Rect(left.Min.X, left.Min.Y, left.Max.X, left.Max.Y-navH-gap)
	rows := split(listR, perPage, gap, true)
	if ui.selected == 0 {
		ui.selected = list[0].ID
	}
	for i := 0; i < perPage; i++ {
		k := ui.page*perPage + i
		if k >= len(list) {
			break
		}
		t := list[k]
		when := strings.Replace(t.StartIso, "T", "  ", 1)
		if len(when) > 5 {
			when = when[5:] // MM-DD  HH:MM:SS
		}
		if len(when) > 12 {
			when = when[:12]
		}
		dur := t.DurationS
		if t.Open && int64(st.Num("tripRecorder", "currentTripId")) == t.ID {
			dur = st.Num("tripRecorder", "currentTripElapsedS")
		}
		label := fmt.Sprintf("%s  %s", when, clockText(dur*1000))
		if t.Open {
			label += "  REC"
		}
		if p.Button(rows[i], &ui.rows[i], label, t.ID == ui.selected) {
			ui.selected = t.ID
			delete(ui.requested, t.ID) // fetch it fresh
		}
	}
	nav := split(image.Rect(left.Min.X, left.Max.Y-navH, left.Max.X, left.Max.Y), 3, gap, false)
	if p.Button(nav[0], &ui.prev, "< NEWER", false) && ui.page > 0 {
		ui.page--
	}
	p.Text(nav[1].Inset(nav[1].Dy()/4), fmt.Sprintf("%d/%d", ui.page+1, pages), monoBold, 0, Center, pal.Ink)
	if p.Button(nav[2], &ui.nxt, "OLDER >", false) && ui.page < pages-1 {
		ui.page++
	}

	// the summary of the selected trip, asked for when it is picked (again after 10 s for an open one)
	var d tripDetail
	have := st.Detail(ui.selected, &d)
	if at, asked := ui.requested[ui.selected]; !asked || (d.Open && p.gtx.Now.Sub(at) > 10*time.Second) {
		ui.requested[ui.selected] = p.gtx.Now
		a.session.Go("tripRecorder", "requestDetail", ui.selected)
	}
	if !have {
		p.Text(right.Inset(right.Dy()/3), "LOADING", sansBold, 0, Center, pal.Ink)
		return
	}
	head := image.Rect(right.Min.X, right.Min.Y, right.Max.X, right.Min.Y+right.Dy()/8)
	state := "CLOSED"
	if d.Open {
		state = "RECORDING"
	} else if d.Clean {
		state = "CLEAN"
	}
	p.Text(head, strings.Replace(d.StartIso, "T", " ", 1)+"  "+state, monoBold, 0, Left, pal.Ink)

	speed, _ := st.Param("SPEED")
	coolant, _ := st.Param("COOLANT_TEMP")
	oil, _ := st.Param("OIL_TEMP")
	distUnit, distScale := "MI", 1.0
	if strings.Contains(strings.ToLower(speed.Unit), "km") {
		distUnit, distScale = "KM", speed.Display(1) // miles -> km, by the same factor as mph -> km/h
	}
	type cell struct{ title, value, unit string }
	cells := []cell{
		{"DISTANCE", Format(d.DistanceMiles*distScale, true, 1), distUnit},
		{"TIME", clockText(d.DurationS * 1000), ""},
		{"TOP SPEED", Format(speed.Display(d.MaxSpeed), true, 0), upper(unitOr(speed.Unit, "MPH"))},
		{"TOP RPM", Format(d.MaxRpm, true, 0), "RPM"},
		{"MAX COOLANT", Format(coolant.Display(d.MaxCoolant), true, 0), unitOr(coolant.Unit, "°C")},
		{"MAX OIL", Format(oil.Display(d.MaxOilTemp), d.MaxOilTemp != 0, 0), unitOr(oil.Unit, "°C")},
		{"VOLTS", fmt.Sprintf("%s-%s", Format(d.MinVoltage, true, 1), Format(d.MaxVoltage, true, 1)), "V"},
		{"FUEL USED", Format(d.FuelUsedPct, true, 1), "%"},
		{"SAMPLES", fmt.Sprintf("%d", d.SampleCount), fmt.Sprintf("%d PIDS", d.ParamCount)},
	}
	grid := image.Rect(right.Min.X, head.Max.Y+gap, right.Max.X, right.Max.Y)
	gridRows := split(grid, 3, gap, true)
	for i, c := range cells {
		box := split(gridRows[i/3], 3, gap, false)[i%3]
		p.Box(box, pal.Ink)
		in := box.Inset(p.Rule() * 3)
		parts := split(in, 4, 0, true)
		p.Text(parts[0], c.title, sansBold, 0, Left, pal.Ink)
		p.Text(image.Rect(in.Min.X, parts[1].Min.Y, in.Max.X, parts[2].Max.Y), c.value, monoBold, 0, Left, pal.Ink)
		p.Text(parts[3], c.unit, sansBold, 0, Left, pal.Ink)
	}
}

func unitOr(u, def string) string {
	if u == "" {
		return def
	}
	return u
}

// sortTrips orders newest first by start time (the list's own order is not promised).
func sortTrips(list []trip) {
	for i := 1; i < len(list); i++ {
		for j := i; j > 0 && list[j].StartIso > list[j-1].StartIso; j-- {
			list[j], list[j-1] = list[j-1], list[j]
		}
	}
}
