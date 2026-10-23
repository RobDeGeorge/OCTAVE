package link

import (
	"encoding/json"
	"sync"
)

// HistoryLen is how many samples of each parameter State keeps (sparklines).
const HistoryLen = 240

// Param is one row of parameterCatalog.displayParameters (the user's units).
type Param struct {
	ID            string  `json:"id"`
	Title         string  `json:"title"`
	ShortTitle    string  `json:"shortTitle"`
	Unit          string  `json:"unit"`
	Kind          string  `json:"kind"`
	Min           float64 `json:"min"`
	Max           float64 `json:"max"`
	Decimals      int     `json:"decimals"`
	DisplayScale  float64 `json:"displayScale"`
	DisplayOffset float64 `json:"displayOffset"`
}

// Display converts a native value to the user's unit.
func (p Param) Display(native float64) float64 {
	scale := p.DisplayScale
	if scale == 0 {
		scale = 1
	}
	return native*scale + p.DisplayOffset
}

// State is everything the UI draws, folded from events. Safe for concurrent use.
type State struct {
	mu        sync.RWMutex
	connected bool
	status    string
	version   string
	params    map[string]float64
	history   map[string][]float64
	catalog   map[string]Param
	props     map[string]map[string]json.RawMessage
	clock     string
	dtcs      []DTC
	dtcKnown  bool
	mil       bool
	clearMsg  string
	details   map[int64]json.RawMessage // trip id -> detailReady's detail
}

// DTC is one stored trouble code.
type DTC struct {
	Code        string `json:"code"`
	Description string `json:"description"`
}

// NewState returns an empty state.
func NewState() *State {
	return &State{params: map[string]float64{}, history: map[string][]float64{}, catalog: map[string]Param{},
		props: map[string]map[string]json.RawMessage{}, details: map[int64]json.RawMessage{}}
}

// Apply folds one event in.
func (s *State) Apply(m Message) {
	s.mu.Lock()
	defer s.mu.Unlock()
	switch m.Event {
	case "params":
		for id, raw := range m.Values {
			var v float64
			if json.Unmarshal(raw, &v) != nil {
				continue
			}
			s.params[id] = v
			h := append(s.history[id], v)
			if len(h) > HistoryLen {
				h = h[len(h)-HistoryLen:]
			}
			s.history[id] = h
		}
	case "property":
		if s.props[m.Object] == nil {
			s.props[m.Object] = map[string]json.RawMessage{}
		}
		s.props[m.Object][m.Name] = m.Value
		if m.Object == "clock" && m.Name == "time" {
			_ = json.Unmarshal(m.Value, &s.clock)
		}
		if m.Object == "parameterCatalog" && m.Name == "displayParameters" {
			var rows []Param
			if json.Unmarshal(m.Value, &rows) == nil {
				s.catalog = make(map[string]Param, len(rows))
				for _, r := range rows {
					s.catalog[r.ID] = r
				}
			}
		}
	case "signal":
		if m.Object == "clock" && m.Name == "timeChanged" && len(m.Args) > 0 {
			_ = json.Unmarshal(m.Args[0], &s.clock)
		}
		if len(m.Args) == 0 {
			break
		}
		switch m.Name {
		case "dtcCodesChanged":
			var codes []DTC
			if json.Unmarshal(m.Args[0], &codes) == nil {
				s.dtcs, s.dtcKnown = codes, true
			}
		case "milStatusChanged":
			_ = json.Unmarshal(m.Args[0], &s.mil)
		case "dtcClearResult":
			var ok bool
			_ = json.Unmarshal(m.Args[0], &ok)
			msg := ""
			if len(m.Args) > 1 {
				_ = json.Unmarshal(m.Args[1], &msg)
			}
			if msg == "" {
				msg = map[bool]string{true: "CODES CLEARED", false: "CLEAR FAILED"}[ok]
			}
			s.clearMsg = msg
		case "detailReady":
			var id int64
			if len(m.Args) > 1 && json.Unmarshal(m.Args[0], &id) == nil {
				s.details[id] = m.Args[1]
			}
		}
	}
}

// SetLink records the connection state.
func (s *State) SetLink(connected bool, status, version string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.connected, s.status = connected, status
	if version != "" {
		s.version = version
	}
	if !connected {
		s.props = map[string]map[string]json.RawMessage{}
	}
}

// Link returns the connection state.
func (s *State) Link() (connected bool, status, version string) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.connected, s.status, s.version
}

// Clock is the app's clock text.
func (s *State) Clock() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.clock
}

// Param returns a parameter's catalog row.
func (s *State) Param(id string) (Param, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	p, ok := s.catalog[id]
	return p, ok
}

// Value is a parameter's latest value in the user's unit.
func (s *State) Value(id string) (float64, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	v, ok := s.params[id]
	if !ok {
		return 0, false
	}
	if p, known := s.catalog[id]; known {
		return p.Display(v), true
	}
	return v, true
}

// History is a parameter's recent values in the user's unit, oldest first.
func (s *State) History(id string) []float64 {
	s.mu.RLock()
	defer s.mu.RUnlock()
	h := s.history[id]
	out := make([]float64, len(h))
	p, known := s.catalog[id]
	for i, v := range h {
		if known {
			v = p.Display(v)
		}
		out[i] = v
	}
	return out
}

// Prop decodes a property value into out; false if absent.
func (s *State) Prop(object, name string, out any) bool {
	s.mu.RLock()
	raw, ok := s.props[object][name]
	s.mu.RUnlock()
	return ok && json.Unmarshal(raw, out) == nil
}

// Str, Num and Bool read scalar properties with a zero default.
func (s *State) Str(object, name string) string {
	var v string
	s.Prop(object, name, &v)
	return v
}

func (s *State) Num(object, name string) float64 {
	var v float64
	s.Prop(object, name, &v)
	return v
}

func (s *State) Bool(object, name string) bool {
	var v bool
	s.Prop(object, name, &v)
	return v
}

// DTCs returns the stored trouble codes, whether they are known yet, and the MIL.
func (s *State) DTCs() ([]DTC, bool, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return append([]DTC(nil), s.dtcs...), s.dtcKnown, s.mil
}

// SetMIL records the lamp read with get_mil_status.
func (s *State) SetMIL(on bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.mil = on
}

// ClearMessage is the last dtcClearResult, in words.
func (s *State) ClearMessage() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.clearMsg
}

// Detail decodes a trip's detail (from detailReady) into out; false if none yet.
func (s *State) Detail(id int64, out any) bool {
	s.mu.RLock()
	raw, ok := s.details[id]
	s.mu.RUnlock()
	return ok && json.Unmarshal(raw, out) == nil
}
