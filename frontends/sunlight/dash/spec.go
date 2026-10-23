// Package dash reads OCTAVE dashboard specs (schema 1,
// docs/api/dashboard-spec.schema.json) and lays their cells out on a grid.
// It follows the reading rules the schema documents: missing sizes default,
// numbers clamp, cells off the grid move back in, and an overlapping cell
// shrinks toward 1x1 or is dropped (an earlier cell wins).
package dash

import (
	"encoding/json"
	"fmt"
	"image"
	"strings"
)

// SupportedSchema is the spec version this package reads.
const SupportedSchema = 1

// Cell is one widget on the grid.
type Cell struct {
	Type    string         `json:"type"`
	ParamID string         `json:"paramId"`
	Col     int            `json:"col"`
	Row     int            `json:"row"`
	ColSpan int            `json:"colSpan"`
	RowSpan int            `json:"rowSpan"`
	Props   map[string]any `json:"props"`
}

// Spec is a whole dashboard.
type Spec struct {
	Schema      int    `json:"schema"`
	ID          string `json:"id"`
	Label       string `json:"label"`
	GridColumns int    `json:"gridColumns"`
	GridRows    int    `json:"gridRows"`
	Margins     *int   `json:"margins"`
	Spacing     *int   `json:"spacing"`
	Cells       []Cell `json:"cells"`
}

func clamp(v, lo, hi int) int {
	return max(lo, min(hi, v))
}

// Parse decodes and normalises a spec.
func Parse(raw []byte) (Spec, error) {
	var s Spec
	if err := json.Unmarshal(raw, &s); err != nil {
		return s, fmt.Errorf("not a dashboard: %w", err)
	}
	if s.Schema == 0 {
		s.Schema = 1
	}
	if s.Schema > SupportedSchema {
		return s, fmt.Errorf("dashboard needs schema %d, sunlight reads %d", s.Schema, SupportedSchema)
	}
	s.Normalize()
	return s, nil
}

// Normalize applies the defaults and placement rules in place.
func (s *Spec) Normalize() {
	s.Label = strings.Join(strings.Fields(s.Label), " ")
	if s.GridColumns < 1 {
		s.GridColumns = 12
	}
	if s.GridRows < 1 {
		s.GridRows = 6
	}
	s.GridColumns = min(s.GridColumns, 48)
	s.GridRows = min(s.GridRows, 48)
	if len(s.Cells) > 256 {
		s.Cells = s.Cells[:256]
	}
	taken := make([][]bool, s.GridRows)
	for i := range taken {
		taken[i] = make([]bool, s.GridColumns)
	}
	free := func(c Cell) bool {
		for r := c.Row; r < c.Row+c.RowSpan; r++ {
			for col := c.Col; col < c.Col+c.ColSpan; col++ {
				if taken[r][col] {
					return false
				}
			}
		}
		return true
	}
	var kept []Cell
	for _, c := range s.Cells {
		if c.Type == "" {
			continue
		}
		c.ColSpan = clamp(c.ColSpan, 1, s.GridColumns)
		c.RowSpan = clamp(c.RowSpan, 1, s.GridRows)
		c.Col = clamp(c.Col, 0, s.GridColumns-c.ColSpan)
		c.Row = clamp(c.Row, 0, s.GridRows-c.RowSpan)
		// shrink toward 1x1 until it fits, else drop it
		for !free(c) && (c.ColSpan > 1 || c.RowSpan > 1) {
			if c.ColSpan >= c.RowSpan && c.ColSpan > 1 {
				c.ColSpan--
			} else {
				c.RowSpan--
			}
		}
		if !free(c) {
			continue
		}
		for r := c.Row; r < c.Row+c.RowSpan; r++ {
			for col := c.Col; col < c.Col+c.ColSpan; col++ {
				taken[r][col] = true
			}
		}
		kept = append(kept, c)
	}
	s.Cells = kept
}

// Layout returns each cell's rectangle inside bounds, with margin around the
// grid and spacing between cells (both in pixels). Indices match s.Cells.
func (s Spec) Layout(bounds image.Rectangle, margin, spacing int) []image.Rectangle {
	inner := bounds.Inset(margin)
	w, h := inner.Dx(), inner.Dy()
	// cell edges: column i starts at x(i); spacing comes out of each gap
	x := func(i int) int { return inner.Min.X + (w+spacing)*i/s.GridColumns }
	y := func(i int) int { return inner.Min.Y + (h+spacing)*i/s.GridRows }
	out := make([]image.Rectangle, len(s.Cells))
	for i, c := range s.Cells {
		out[i] = image.Rect(x(c.Col), y(c.Row), x(c.Col+c.ColSpan)-spacing, y(c.Row+c.RowSpan)-spacing)
	}
	return out
}

// Num reads a numeric prop, or def.
func (c Cell) Num(name string, def float64) float64 {
	if v, ok := c.Props[name].(float64); ok {
		return v
	}
	return def
}

// Has tells whether a prop is set.
func (c Cell) Has(name string) bool {
	_, ok := c.Props[name]
	return ok
}

// Bool reads a boolean prop, or def.
func (c Cell) Bool(name string, def bool) bool {
	if v, ok := c.Props[name].(bool); ok {
		return v
	}
	return def
}

// Str reads a string prop, or def.
func (c Cell) Str(name, def string) string {
	if v, ok := c.Props[name].(string); ok && v != "" {
		return v
	}
	return def
}

// ParamIDs lists the parameters the dashboard shows (its OBD demand). The
// self-binding G-force and compass widgets read IMU ids.
func (s Spec) ParamIDs() []string {
	var ids []string
	for _, c := range s.Cells {
		switch c.Type {
		case "GForceGauge":
			ids = append(ids, "LATERAL_G", "LONGITUDINAL_G")
		case "CompassGauge":
			ids = append(ids, "HEADING")
		default:
			if c.ParamID != "" {
				ids = append(ids, c.ParamID)
			}
		}
	}
	return ids
}
