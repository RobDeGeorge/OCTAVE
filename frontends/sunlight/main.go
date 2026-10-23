// Sunlight: a brutalist, high-contrast OCTAVE frontend in Go + Gio, built
// to be read in direct sunlight. It talks to OCTAVE over the service API.
package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"image"
	_ "image/jpeg"
	_ "image/png"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"strconv"
	"sync"
	"time"

	"gioui.org/app"
	"gioui.org/gesture"
	"gioui.org/io/key"
	"gioui.org/op"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gioui.org/widget"

	"octave/sunlight/dash"
	"octave/sunlight/link"
)

type page int

const (
	pageDrive page = iota
	pageDash
	pageMedia
)

var pageNames = []string{"DRIVE", "DASH", "MEDIA"}

// The parameters the DRIVE page shows.
var driveIDs = []string{"SPEED", "RPM", "COOLANT_TEMP", "FUEL_LEVEL", "CONTROL_MODULE_VOLTAGE"}

type dashInfo struct {
	ID    string `json:"id"`
	Label string `json:"label"`
}

// App is the whole frontend.
type App struct {
	win     *app.Window
	session *link.Session
	addr    string
	painter Painter

	page       page
	nightMode  bool
	fullscreen bool

	tabs                               [3]widget.Clickable
	nightBtn                           widget.Clickable
	dashPrev, dashNext                 widget.Clickable
	prevBtn, playBtn, nextBtn, shufBtn widget.Clickable
	volDown, volUp, muteBtn            widget.Clickable
	seekTap, volTap                    gesture.Click

	dashIndex int
	dashWant  string // --dash, until the list arrives
	mu        sync.Mutex
	specs     map[string]*dash.Spec // loaded on demand
	loading   map[string]bool
	cells     map[int]*cellUI

	art    paint.ImageOp
	artKey string
	hasArt bool
	artMu  sync.Mutex
}

func main() {
	host := flag.String("host", "127.0.0.1", "OCTAVE service host")
	port := flag.Int("port", 9876, "OCTAVE service port")
	full := flag.Bool("fullscreen", false, "start fullscreen")
	nightMode := flag.Bool("night", false, "start in night mode (white on black)")
	start := flag.String("page", "drive", "drive, dash or media")
	dashID := flag.String("dash", "", "dashboard id to open on the DASH page")
	flag.Parse()

	a := &App{addr: net.JoinHostPort(*host, strconv.Itoa(*port)), nightMode: *nightMode, fullscreen: *full,
		dashWant: *dashID, specs: map[string]*dash.Spec{}, loading: map[string]bool{}, cells: map[int]*cellUI{}}
	for i, n := range pageNames {
		if n == map[string]string{"drive": "DRIVE", "dash": "DASH", "media": "MEDIA"}[*start] {
			a.page = page(i)
		}
	}
	a.win = new(app.Window)
	a.win.Option(app.Title("OCTAVE Sunlight"), app.Size(unit.Dp(1280), unit.Dp(800)), app.MinSize(unit.Dp(640), unit.Dp(400)))
	if a.fullscreen {
		a.win.Option(app.Fullscreen.Option())
	}
	a.painter.th = newTheme()
	a.session = link.NewSession(a.addr, a.win.Invalidate)
	stop := make(chan struct{})
	go a.session.Run(stop)
	go func() {
		err := a.loop()
		close(stop)
		a.session.Close()
		if err != nil {
			log.Fatal(err)
		}
		os.Exit(0)
	}()
	app.Main()
}

func (a *App) loop() error {
	var ops op.Ops
	for {
		switch e := a.win.Event().(type) {
		case app.DestroyEvent:
			return e.Err
		case app.FrameEvent:
			gtx := app.NewContext(&ops, e)
			a.keys(gtx)
			a.frame(gtx)
			e.Frame(gtx.Ops)
		}
	}
}

func (a *App) palette() Palette {
	if a.nightMode {
		return night
	}
	return day
}

// keys handles the keyboard: every control also has a touch target.
func (a *App) keys(gtx C) {
	for {
		ev, ok := gtx.Event(key.Filter{Name: ""}, key.Filter{Name: key.NameLeftArrow}, key.Filter{Name: key.NameRightArrow},
			key.Filter{Name: key.NameUpArrow}, key.Filter{Name: key.NameDownArrow}, key.Filter{Name: key.NameF11},
			key.Filter{Name: "Q", Required: key.ModCtrl})
		if !ok {
			return
		}
		e, ok := ev.(key.Event)
		if !ok || e.State != key.Press {
			continue
		}
		st := a.session.State
		switch {
		case e.Name == "Q" && e.Modifiers.Contain(key.ModCtrl):
			a.session.Close()
			os.Exit(0)
		case e.Name == key.NameSpace:
			a.session.Go("nowPlaying", "togglePlay")
		case e.Name == key.NameLeftArrow:
			a.session.Go("nowPlaying", "previous")
		case e.Name == key.NameRightArrow:
			a.session.Go("nowPlaying", "next")
		case e.Name == key.NameUpArrow || e.Name == "+" || e.Name == "=":
			a.session.Go("volumeController", "applyVolume", min(100, int(st.Num("volumeController", "percent"))+5))
		case e.Name == key.NameDownArrow || e.Name == "-":
			a.session.Go("volumeController", "applyVolume", max(0, int(st.Num("volumeController", "percent"))-5))
		case e.Name == "M":
			a.session.GoSet("volumeController", "muted", !st.Bool("volumeController", "muted"))
		case e.Name == "N":
			a.nightMode = !a.nightMode
		case e.Name == "1" || e.Name == "2" || e.Name == "3":
			a.page = page(e.Name[0] - '1')
		case e.Name == "[":
			a.dashIndex--
		case e.Name == "]":
			a.dashIndex++
		case e.Name == key.NameF11:
			a.fullscreen = !a.fullscreen
			if a.fullscreen {
				a.win.Option(app.Fullscreen.Option())
			} else {
				a.win.Option(app.Windowed.Option())
			}
		}
	}
}

func (a *App) frame(gtx C) {
	p := &a.painter
	p.begin(gtx, a.palette())
	pal := p.pal
	full := image.Rectangle{Max: gtx.Constraints.Max}
	p.Fill(full, pal.Paper)
	gap := p.Rule() * 2
	bar := image.Rect(0, 0, full.Dx(), max(p.px(76), full.Dy()/10))
	a.topBar(p, bar.Inset(gap))
	p.Fill(image.Rect(0, bar.Max.Y, full.Dx(), bar.Max.Y+p.Rule()), pal.Ink)
	body := image.Rect(gap, bar.Max.Y+p.Rule()+gap, full.Dx()-gap, full.Dy()-gap)
	switch a.page {
	case pageDrive:
		a.session.Demand(driveIDs)
		a.drive(p, body)
	case pageDash:
		a.dashPage(p, body)
	case pageMedia:
		a.session.Demand(nil)
		a.media(p, body)
	}
	// the flashing shift light needs frames even without new data
	if a.page != pageMedia && a.session.State.Bool("shiftLightManager", "lit") {
		gtx.Execute(op.InvalidateCmd{At: gtx.Now.Add(100 * time.Millisecond)})
	}
}

func (a *App) topBar(p *Painter, r image.Rectangle) {
	pal := p.pal
	st := a.session.State
	gap := p.Rule() * 2
	tabW := min(r.Dx()/7, p.px(170))
	for i, name := range pageNames {
		tr := image.Rect(r.Min.X+i*(tabW+gap), r.Min.Y, r.Min.X+i*(tabW+gap)+tabW, r.Max.Y)
		if p.Button(tr, &a.tabs[i], name, a.page == page(i)) {
			a.page = page(i)
		}
	}
	nightR := image.Rect(r.Max.X-tabW, r.Min.Y, r.Max.X, r.Max.Y)
	if p.Button(nightR, &a.nightBtn, map[bool]string{true: "DAY", false: "NIGHT"}[a.nightMode], false) {
		a.nightMode = !a.nightMode
	}
	clockR := image.Rect(nightR.Min.X-gap-tabW, r.Min.Y, nightR.Min.X-gap, r.Max.Y)
	p.Text(clockR.Inset(p.Rule()*2), st.Clock(), monoBold, 0, Right, pal.Ink)
	mid := image.Rect(r.Min.X+3*(tabW+gap)+gap, r.Min.Y, clockR.Min.X-gap, r.Max.Y)
	connected, status, version := st.Link()
	rows := split(mid, 2, 0, true)
	if !connected {
		p.Fill(mid, pal.Accent)
		p.Text(mid.Inset(p.Rule()*2), status, sansBold, 0, Center, pal.Paper)
		return
	}
	car := st.Str("obdManager", "vehicleMake")
	if y := st.Num("obdManager", "vehicleModelYear"); y > 0 {
		car += fmt.Sprintf(" %d", int(y))
	}
	obd := st.Str("obdManager", "connectionState")
	if obd == "" {
		obd = "OBD ?"
	}
	p.Text(rows[0], upper(fmt.Sprintf("%s  %s", obd, car)), sansBold, 0, Left, pal.Ink)
	line := "OCTAVE " + version
	if st.Bool("tripRecorder", "recording") {
		s := int(st.Num("tripRecorder", "currentTripElapsedS"))
		line = fmt.Sprintf("REC %d:%02d   %s", s/60, s%60, line)
	}
	p.Text(rows[1], line, monoBold, 0, Left, pal.Ink)
}

// ── DRIVE ───────────────────────────────────────────────────────────────

func (a *App) drive(p *Painter, r image.Rectangle) {
	gap := p.Rule() * 2
	rows := []image.Rectangle{
		image.Rect(r.Min.X, r.Min.Y, r.Max.X, r.Min.Y+r.Dy()*55/100),
		image.Rect(r.Min.X, r.Min.Y+r.Dy()*55/100+gap, r.Max.X, r.Min.Y+r.Dy()*68/100),
		image.Rect(r.Min.X, r.Min.Y+r.Dy()*68/100+gap, r.Max.X, r.Max.Y),
	}
	top := split(rows[0], 2, gap, false)
	a.bigTile(p, top[0], "SPEED")
	a.bigTile(p, top[1], "RPM")
	a.shiftBar(p, rows[1])
	volt := "CONTROL_MODULE_VOLTAGE"
	if _, ok := a.session.State.Value(volt); !ok {
		if _, ok := a.session.State.Value("ELM_VOLTAGE"); ok {
			volt = "ELM_VOLTAGE"
		}
	}
	for i, id := range []string{"COOLANT_TEMP", "FUEL_LEVEL", volt} {
		a.smallTile(p, split(rows[2], 3, gap, false)[i], id)
	}
}

// bigTile: a title, giant numerals and the unit, in a thick box.
func (a *App) bigTile(p *Painter, r image.Rectangle, id string) {
	pal := p.pal
	st := a.session.State
	prm, _ := st.Param(id)
	v, ok := st.Value(id)
	p.Box(r, pal.Ink)
	in := r.Inset(p.Rule() * 3)
	head := image.Rect(in.Min.X, in.Min.Y, in.Max.X, in.Min.Y+in.Dy()/6)
	title := prm.Title
	if title == "" {
		title = id
	}
	p.Text(head, upper(title), sansBold, 0, Left, pal.Ink)
	p.Text(head, prm.Unit, sansBold, 0, Right, pal.Ink)
	digits := 4
	if id == "SPEED" {
		digits = 3
	}
	txt := Format(v, ok, 0)
	for len(txt) < digits {
		txt = " " + txt
	}
	p.Text(image.Rect(in.Min.X, head.Max.Y, in.Max.X, in.Max.Y), txt, monoBold, 0, Center, pal.Ink)
}

// smallTile: title, number and a bar against the PID's range.
func (a *App) smallTile(p *Painter, r image.Rectangle, id string) {
	pal := p.pal
	st := a.session.State
	prm, _ := st.Param(id)
	v, ok := st.Value(id)
	p.Box(r, pal.Ink)
	in := r.Inset(p.Rule() * 3)
	rows := split(in, 5, 0, true)
	title := prm.ShortTitle
	if prm.Title != "" && len(prm.Title) <= 12 {
		title = prm.Title
	}
	if title == "" {
		title = id
	}
	p.Text(rows[0], upper(title), sansBold, 0, Left, pal.Ink)
	num := image.Rect(in.Min.X, rows[1].Min.Y, in.Max.X, rows[3].Max.Y)
	numR, unitR := cutRight(num, num.Dx()/4)
	p.Text(numR, Format(v, ok, prm.Decimals), monoBold, 0, Right, pal.Ink)
	p.Text(image.Rect(unitR.Min.X+p.Rule()*2, unitR.Min.Y+unitR.Dy()/2, unitR.Max.X, unitR.Max.Y), prm.Unit, sansBold, 0, Left, pal.Ink)
	barR := rows[4]
	barR.Min.Y += barR.Dy() / 4
	p.Box(barR, pal.Ink)
	if ok && prm.Max > prm.Min {
		f := clamp01((v - prm.Min) / (prm.Max - prm.Min))
		inner := barR.Inset(p.Rule() * 2)
		col := pal.Ink
		if warnTile(id, v, f) {
			col = pal.Accent
		}
		p.Fill(image.Rect(inner.Min.X, inner.Min.Y, inner.Min.X+int(float64(inner.Dx())*f), inner.Max.Y), col)
	}
}

// warnTile: the accent marks a reading worth a glance (hot coolant, low fuel, odd voltage).
func warnTile(id string, v, frac float64) bool {
	switch id {
	case "COOLANT_TEMP":
		return frac > 0.85
	case "FUEL_LEVEL":
		return v < 12
	default:
		return frac < 0.15 || frac > 0.9
	}
}

// shiftBar: segments up to the shift light's maxRpm; past the first flag
// they are accent, and the whole bar flashes while shiftLightManager is lit.
func (a *App) shiftBar(p *Painter, r image.Rectangle) {
	pal := p.pal
	st := a.session.State
	rpm, ok := st.Value("RPM")
	maxRpm := st.Num("shiftLightManager", "maxRpm")
	if maxRpm <= 0 {
		maxRpm = 7000
	}
	var flags []struct {
		RPM float64 `json:"rpm"`
	}
	st.Prop("shiftLightManager", "flags", &flags)
	warnAt := maxRpm * 0.8
	for _, f := range flags {
		if f.RPM > 0 && f.RPM < warnAt {
			warnAt = f.RPM
		}
	}
	lit := st.Bool("shiftLightManager", "lit")
	flash := lit && p.gtx.Now.UnixMilli()/100%2 == 0
	if flash {
		p.Fill(r, pal.Accent)
	}
	const n = 24
	gap := p.Rule()
	segs := split(r, n, gap, false)
	for i, s := range segs {
		at := maxRpm * float64(i+1) / n
		col := pal.Ink
		if at > warnAt {
			col = pal.Accent
		}
		if flash {
			col = pal.Paper
		}
		if ok && rpm >= at-maxRpm/n/2 {
			p.Fill(s, col)
		} else {
			p.Box(s, pal.Dim)
		}
	}
}

// ── DASH ────────────────────────────────────────────────────────────────

func (a *App) dashboards() []dashInfo {
	var list []dashInfo
	a.session.State.Prop("dashboardManager", "dashboards", &list)
	return list
}

func (a *App) spec(id string) *dash.Spec {
	a.mu.Lock()
	defer a.mu.Unlock()
	if s, ok := a.specs[id]; ok {
		return s
	}
	if !a.loading[id] && a.session.Client() != nil {
		a.loading[id] = true
		go func() {
			var raw json.RawMessage
			err := a.session.CallSync(&raw, "dashboardManager", "loadDashboard", id)
			var s *dash.Spec
			if err == nil {
				if parsed, perr := dash.Parse(raw); perr == nil {
					s = &parsed
				} else {
					err = perr
				}
			}
			if err != nil {
				log.Printf("dashboard %s: %v", id, err)
			}
			a.mu.Lock()
			a.specs[id] = s
			delete(a.loading, id)
			a.mu.Unlock()
			a.win.Invalidate()
		}()
	}
	return nil
}

func (a *App) dashPage(p *Painter, r image.Rectangle) {
	pal := p.pal
	list := a.dashboards()
	gap := p.Rule() * 2
	head := image.Rect(r.Min.X, r.Min.Y, r.Max.X, r.Min.Y+max(p.px(64), r.Dy()/10))
	btnW := min(r.Dx()/6, p.px(170))
	if p.Button(image.Rect(head.Min.X, head.Min.Y, head.Min.X+btnW, head.Max.Y), &a.dashPrev, "< PREV", false) {
		a.dashIndex--
	}
	if p.Button(image.Rect(head.Max.X-btnW, head.Min.Y, head.Max.X, head.Max.Y), &a.dashNext, "NEXT >", false) {
		a.dashIndex++
	}
	labelR := image.Rect(head.Min.X+btnW+gap, head.Min.Y, head.Max.X-btnW-gap, head.Max.Y)
	grid := image.Rect(r.Min.X, head.Max.Y+gap, r.Max.X, r.Max.Y)
	if len(list) == 0 {
		a.session.Demand(nil)
		p.Text(labelR, "NO DASHBOARDS", sansBold, 0, Center, pal.Ink)
		return
	}
	if a.dashWant != "" {
		for i, d := range list {
			if d.ID == a.dashWant {
				a.dashIndex = i
			}
		}
		a.dashWant = ""
	}
	a.dashIndex = ((a.dashIndex % len(list)) + len(list)) % len(list)
	cur := list[a.dashIndex]
	p.Text(labelR, fmt.Sprintf("%s  %d/%d", upper(cur.Label), a.dashIndex+1, len(list)), sansBold, 0, Center, pal.Ink)
	s := a.spec(cur.ID)
	if s == nil {
		a.session.Demand(nil)
		p.Text(grid.Inset(grid.Dy()/3), "LOADING", sansBold, 0, Center, pal.Ink)
		return
	}
	a.session.Demand(s.ParamIDs())
	margin, spacing := 0, gap
	if s.Margins != nil {
		margin = p.px(float32(*s.Margins))
	}
	if s.Spacing != nil {
		spacing = max(p.px(float32(*s.Spacing)), p.Rule())
	}
	for i, cr := range s.Layout(grid, margin, spacing) {
		ui := a.cells[i]
		if ui == nil {
			ui = &cellUI{}
			a.cells[i] = ui
		}
		a.drawCell(p, cr, s.Cells[i], ui)
	}
}

// ── MEDIA ───────────────────────────────────────────────────────────────

func (a *App) media(p *Painter, r image.Rectangle) {
	pal := p.pal
	st := a.session.State
	gap := p.Rule() * 2
	a.fetchArt()
	rows := []image.Rectangle{
		image.Rect(r.Min.X, r.Min.Y, r.Max.X, r.Min.Y+r.Dy()*52/100),
		image.Rect(r.Min.X, r.Min.Y+r.Dy()*52/100+gap, r.Max.X, r.Min.Y+r.Dy()*64/100),
		image.Rect(r.Min.X, r.Min.Y+r.Dy()*64/100+gap, r.Max.X, r.Min.Y+r.Dy()*82/100),
		image.Rect(r.Min.X, r.Min.Y+r.Dy()*82/100+gap, r.Max.X, r.Max.Y),
	}
	info := rows[0]
	a.artMu.Lock()
	hasArt, art := a.hasArt, a.art
	a.artMu.Unlock()
	if hasArt {
		side := info.Dy()
		artR := image.Rect(info.Min.X, info.Min.Y, info.Min.X+side, info.Max.Y)
		drawImage(p, artR.Inset(p.Rule()), art)
		p.Box(artR, pal.Ink)
		info.Min.X = artR.Max.X + gap*2
	}
	title := st.Str("nowPlaying", "title")
	if title == "" {
		title = "NOTHING PLAYING"
	}
	lines := split(info, 5, 0, true)
	p.Text(image.Rect(info.Min.X, lines[0].Min.Y, info.Max.X, lines[1].Max.Y), title, sansBold, 0, Left, pal.Ink)
	p.Text(lines[2], st.Str("nowPlaying", "artist"), sansBold, 0, Left, pal.Ink)
	p.Text(lines[3], st.Str("nowPlaying", "album"), sansBold, 0, Left, pal.Dim)
	src := upper(st.Str("nowPlaying", "source"))
	if st.Bool("nowPlaying", "shuffled") {
		src += "  SHUFFLE"
	}
	p.Text(lines[4], src, monoBold, lines[4].Dy()*2/3, Left, pal.Ink)

	// position: tap to seek
	pos, dur := st.Num("nowPlaying", "positionMs"), st.Num("nowPlaying", "durationMs")
	times := image.Rect(rows[1].Min.X, rows[1].Min.Y, rows[1].Max.X, rows[1].Min.Y+rows[1].Dy()/2)
	p.Text(times, clockText(pos), monoBold, 0, Left, pal.Ink)
	p.Text(times, clockText(dur), monoBold, 0, Right, pal.Ink)
	track := image.Rect(rows[1].Min.X, times.Max.Y+p.Rule(), rows[1].Max.X, rows[1].Max.Y)
	p.Box(track, pal.Ink)
	if dur > 0 {
		in := track.Inset(p.Rule() * 2)
		p.Fill(image.Rect(in.Min.X, in.Min.Y, in.Min.X+int(float64(in.Dx())*clamp01(pos/dur)), in.Max.Y), pal.Ink)
	}
	if at, ok := p.Tap(track, &a.seekTap); ok && dur > 0 && st.Bool("nowPlaying", "canSeek") {
		a.session.Go("nowPlaying", "seek", int(dur*clamp01(float64(at.X)/float64(track.Dx()))))
	}

	// transport
	t := split(rows[2], 4, gap, false)
	if p.Button(t[0], &a.prevBtn, "PREV", false) {
		a.session.Go("nowPlaying", "previous")
	}
	playing := st.Bool("nowPlaying", "playing")
	if p.Button(t[1], &a.playBtn, map[bool]string{true: "PAUSE", false: "PLAY"}[playing], playing) {
		a.session.Go("nowPlaying", "togglePlay")
	}
	if p.Button(t[2], &a.nextBtn, "NEXT", false) {
		a.session.Go("nowPlaying", "next")
	}
	if p.Button(t[3], &a.shufBtn, "SHUFFLE", st.Bool("nowPlaying", "shuffled")) {
		a.session.Go("nowPlaying", "toggleShuffle")
	}

	// volume: buttons and a tap-to-set bar
	pct := int(st.Num("volumeController", "percent"))
	muted := st.Bool("volumeController", "muted") || st.Bool("volumeController", "effectivelyMuted")
	v := rows[3]
	btnW := v.Dx() / 7
	if p.Button(image.Rect(v.Min.X, v.Min.Y, v.Min.X+btnW, v.Max.Y), &a.volDown, "VOL -", false) {
		a.session.Go("volumeController", "applyVolume", max(0, pct-5))
	}
	if p.Button(image.Rect(v.Min.X+btnW+gap, v.Min.Y, v.Min.X+2*btnW+gap, v.Max.Y), &a.volUp, "VOL +", false) {
		a.session.Go("volumeController", "applyVolume", min(100, pct+5))
	}
	if p.Button(image.Rect(v.Max.X-btnW, v.Min.Y, v.Max.X, v.Max.Y), &a.muteBtn, "MUTE", muted) {
		a.session.GoSet("volumeController", "muted", !st.Bool("volumeController", "muted"))
	}
	volR := image.Rect(v.Min.X+2*btnW+2*gap, v.Min.Y, v.Max.X-btnW-gap, v.Max.Y)
	p.Box(volR, pal.Ink)
	in := volR.Inset(p.Rule() * 2)
	col := pal.Ink
	if muted {
		col = pal.Dim
	}
	p.Fill(image.Rect(in.Min.X, in.Min.Y, in.Min.X+in.Dx()*pct/100, in.Max.Y), col)
	label := fmt.Sprintf("%d%%", pct)
	if muted {
		label = "MUTED " + label
	}
	labelR := in.Inset(in.Dy() / 5)
	p.Text(labelR, label, monoBold, 0, Right, pal.Accent)
	if at, ok := p.Tap(volR, &a.volTap); ok {
		a.session.Go("volumeController", "applyVolume", int(100*clamp01(float64(at.X)/float64(volR.Dx()))+0.5))
	}
}

func clockText(ms float64) string {
	s := int(ms / 1000)
	return fmt.Sprintf("%d:%02d", s/60, s%60)
}

// fetchArt loads the cover from the service's GET /art when artRevision (or the track) changes.
// A 404 (no art, or an app without the endpoint) leaves the page without one.
func (a *App) fetchArt() {
	st := a.session.State
	// artRevision is the cover's cache key; older apps lack it, so the track stands in
	k := fmt.Sprintf("%v\x00%s\x00%s", st.Num("nowPlaying", "artRevision"), st.Str("nowPlaying", "title"), st.Str("nowPlaying", "artUrl"))
	a.artMu.Lock()
	same := k == a.artKey
	a.artKey = k
	a.artMu.Unlock()
	if same {
		return
	}
	go func() {
		cl := http.Client{Timeout: 5 * time.Second}
		resp, err := cl.Get("http://" + a.addr + "/art")
		var img image.Image
		if err == nil {
			defer resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				body, _ := io.ReadAll(io.LimitReader(resp.Body, 16<<20))
				img, _, err = image.Decode(bytes.NewReader(body))
			}
		}
		a.artMu.Lock()
		if k == a.artKey {
			a.hasArt = err == nil && img != nil
			if a.hasArt {
				a.art = paint.NewImageOp(img)
			}
		}
		a.artMu.Unlock()
		a.win.Invalidate()
	}()
}

// drawImage scales an image to cover r (square crop).
func drawImage(p *Painter, r image.Rectangle, img paint.ImageOp) {
	size := img.Size()
	if size.X == 0 || size.Y == 0 {
		return
	}
	scale := max(float32(r.Dx())/float32(size.X), float32(r.Dy())/float32(size.Y))
	ops := p.gtx.Ops
	cl := clipRect(ops, r)
	tr := op.Affine(affine(r.Min, scale)).Push(ops)
	img.Add(ops)
	paint.PaintOp{}.Add(ops)
	tr.Pop()
	cl.Pop()
}

func upper(s string) string {
	b := []rune(s)
	for i, c := range b {
		if c >= 'a' && c <= 'z' {
			b[i] = c - 32
		}
	}
	return string(b)
}
