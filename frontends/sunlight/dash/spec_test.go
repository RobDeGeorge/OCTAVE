package dash

import (
	"encoding/json"
	"image"
	"os"
	"path/filepath"
	"testing"
)

func TestParseDefaults(t *testing.T) {
	s, err := Parse([]byte(`{"label":"  My   dash ","cells":[{"type":"DigitalReadout","paramId":"RPM","col":0,"row":0}]}`))
	if err != nil {
		t.Fatal(err)
	}
	if s.Schema != 1 || s.GridColumns != 12 || s.GridRows != 6 || s.Label != "My dash" {
		t.Fatalf("defaults: %+v", s)
	}
	if c := s.Cells[0]; c.ColSpan != 1 || c.RowSpan != 1 {
		t.Fatalf("spans default to 1: %+v", c)
	}
}

func TestParseRefusesNewerSchema(t *testing.T) {
	if _, err := Parse([]byte(`{"schema":2,"label":"x","cells":[]}`)); err == nil {
		t.Fatal("schema 2 accepted")
	}
	if _, err := Parse([]byte(`not json`)); err == nil {
		t.Fatal("garbage accepted")
	}
}

func TestClampAndMoveIn(t *testing.T) {
	s, _ := Parse([]byte(`{"label":"x","gridColumns":4,"gridRows":4,"cells":[
		{"type":"BarGauge","col":3,"row":3,"colSpan":3,"rowSpan":2},
		{"type":"BarGauge","col":-2,"row":0,"colSpan":99}]}`))
	a := s.Cells[0]
	if a.Col != 1 || a.Row != 2 || a.ColSpan != 3 || a.RowSpan != 2 {
		t.Fatalf("off-grid cell not moved in: %+v", a)
	}
	b := s.Cells[1]
	if b.Col != 0 || b.ColSpan != 4 {
		t.Fatalf("wide cell not clamped: %+v", b)
	}
}

func TestOverlapShrinksOrDrops(t *testing.T) {
	s, _ := Parse([]byte(`{"label":"x","gridColumns":4,"gridRows":2,"cells":[
		{"type":"A","col":0,"row":0,"colSpan":2,"rowSpan":2},
		{"type":"D","col":3,"row":1},
		{"type":"B","col":2,"row":0,"colSpan":2,"rowSpan":2},
		{"type":"C","col":1,"row":1},
		{"type":"","col":3,"row":0}]}`))
	// A and D stay; B (2x2 over D) shrinks to 1x2 at col 2; C lies inside A and goes; the typeless cell goes.
	if len(s.Cells) != 3 || s.Cells[0].Type != "A" || s.Cells[1].Type != "D" || s.Cells[2].Type != "B" {
		t.Fatalf("kept: %+v", s.Cells)
	}
	if b := s.Cells[2]; b.Col != 2 || b.ColSpan != 1 || b.RowSpan != 2 {
		t.Fatalf("B not shrunk to 1x2: %+v", b)
	}
}

func TestLayout(t *testing.T) {
	s := Spec{GridColumns: 4, GridRows: 2, Cells: []Cell{
		{Type: "A", Col: 0, Row: 0, ColSpan: 2, RowSpan: 2},
		{Type: "B", Col: 2, Row: 0, ColSpan: 2, RowSpan: 1},
		{Type: "C", Col: 3, Row: 1, ColSpan: 1, RowSpan: 1},
	}}
	r := s.Layout(image.Rect(0, 0, 410, 210), 5, 10)
	// inner (5,5)-(405,205); column edges every (400+10)/4, row edges every (200+10)/2, minus the gap
	if r[0] != image.Rect(5, 5, 200, 205) {
		t.Fatalf("A: %v", r[0])
	}
	if r[1] != image.Rect(210, 5, 405, 100) {
		t.Fatalf("B: %v", r[1])
	}
	if r[2] != image.Rect(312, 110, 405, 205) {
		t.Fatalf("C: %v", r[2])
	}
	// nothing overlaps
	for i := range r {
		for j := i + 1; j < len(r); j++ {
			if r[i].Overlaps(r[j]) {
				t.Fatalf("%d overlaps %d", i, j)
			}
		}
	}
}

func TestProps(t *testing.T) {
	c := Cell{Props: map[string]any{"max": 7000.0, "showNeedle": true, "label": "TEMP"}}
	if c.Num("max", 1) != 7000 || c.Num("min", -3) != -3 || !c.Bool("showNeedle", false) || c.Str("label", "") != "TEMP" {
		t.Fatalf("props: %+v", c)
	}
}

// Every built-in preset parses, keeps all its cells and lays out without overlaps.
func TestPresets(t *testing.T) {
	files, _ := filepath.Glob("../../../frontend/dashboards/presets/*.json")
	if len(files) == 0 {
		t.Skip("presets not found")
	}
	for _, f := range files {
		raw, err := os.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		var count struct{ Cells []any }
		_ = json.Unmarshal(raw, &count)
		s, err := Parse(raw)
		if err != nil {
			t.Fatalf("%s: %v", f, err)
		}
		if len(s.Cells) != len(count.Cells) {
			t.Errorf("%s: %d of %d cells kept", f, len(s.Cells), len(count.Cells))
		}
		r := s.Layout(image.Rect(0, 0, 1280, 700), 8, 6)
		for i := range r {
			if r[i].Empty() {
				t.Errorf("%s: cell %d empty", f, i)
			}
			for j := i + 1; j < len(r); j++ {
				if r[i].Overlaps(r[j]) {
					t.Errorf("%s: cells %d and %d overlap", f, i, j)
				}
			}
		}
		if len(s.ParamIDs()) == 0 {
			t.Errorf("%s: no parameters", f)
		}
	}
}
