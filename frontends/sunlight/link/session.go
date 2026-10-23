package link

import (
	"encoding/json"
	"log"
	"slices"
	"sync"
	"time"
)

// Consumer is sunlight's name for obdManager.setParameterDemand.
const Consumer = "sunlight"

type subscription struct {
	object     string
	properties []string
	signals    []string
	throttleMs int
}

var subscriptions = []subscription{
	{"paramStore", nil, nil, 50},
	{"parameterCatalog", []string{"displayParameters"}, nil, 0},
	{"obdManager", []string{"connected", "connectionState", "vehicleMake", "vehicleModelYear", "vehicleSupportedParameters"},
		[]string{"dtcCodesChanged", "milStatusChanged", "dtcClearResult"}, 0},
	{"nowPlaying", []string{"source", "playing", "hasTrack", "title", "artist", "album", "artUrl", "artRevision", "durationMs", "positionMs",
		"canSeek", "canShuffle", "shuffled"}, nil, 200},
	{"volumeController", []string{"percent", "muted", "effectivelyMuted"}, nil, 0},
	{"shiftLightManager", []string{"enabled", "maxRpm", "flags", "active", "activeFlag", "lit"}, nil, 0},
	{"tripRecorder", []string{"recording", "currentTripId", "currentTripElapsedS", "currentSampleCount", "trips"},
		[]string{"detailReady"}, 0},
	{"dashboardManager", []string{"dashboards"}, nil, 0},
	{"settingsManager", []string{"gaugeSpeedUnit"}, nil, 0},
	{"clock", []string{"time"}, nil, 0},
}

// Session keeps a Client connected to Addr, folding its events into State.
type Session struct {
	Addr     string
	State    *State
	OnChange func() // called after each batch of events, from the session goroutine

	mu     sync.Mutex
	client *Client
	demand []string
}

// NewSession makes a session; call Run in a goroutine.
func NewSession(addr string, onChange func()) *Session {
	return &Session{Addr: addr, State: NewState(), OnChange: onChange}
}

func (s *Session) changed() {
	if s.OnChange != nil {
		s.OnChange()
	}
}

// Run connects, subscribes, reads, and reconnects every second until stop closes.
func (s *Session) Run(stop <-chan struct{}) {
	for {
		select {
		case <-stop:
			s.Close()
			return
		default:
		}
		c, err := Dial(s.Addr, 3*time.Second)
		if err != nil {
			s.State.SetLink(false, "NO SIGNAL "+s.Addr, "")
			s.changed()
			time.Sleep(time.Second)
			continue
		}
		version, err := c.Hello()
		if err == nil {
			// only the live values are essential; an older app may lack a property
			for _, sub := range subscriptions {
				if serr := c.Subscribe(sub.object, sub.properties, sub.signals, sub.throttleMs); serr != nil {
					log.Printf("subscribe %s: %v", sub.object, serr)
					if sub.object == "paramStore" {
						err = serr
						break
					}
				}
			}
		}
		if err != nil {
			s.State.SetLink(false, err.Error(), "")
			c.Close()
			s.changed()
			time.Sleep(time.Second)
			continue
		}
		s.mu.Lock()
		s.client = c
		demand := s.demand
		s.mu.Unlock()
		s.State.SetLink(true, "LINK "+s.Addr, version)
		if demand != nil {
			go s.sendDemand(demand)
		}
		s.changed()
		s.pump(c)
		s.mu.Lock()
		s.client = nil
		s.mu.Unlock()
		s.State.SetLink(false, "LOST "+s.Addr, "")
		s.changed()
		time.Sleep(time.Second)
	}
}

// pump applies events, redrawing at most every 16 ms.
func (s *Session) pump(c *Client) {
	tick := time.NewTicker(16 * time.Millisecond)
	defer tick.Stop()
	dirty := false
	for {
		select {
		case m, ok := <-c.Events():
			if !ok {
				return
			}
			s.State.Apply(m)
			dirty = true
		case <-tick.C:
			if dirty {
				dirty = false
				s.changed()
			}
		}
	}
}

// Client returns the live client, or nil.
func (s *Session) Client() *Client {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.client
}

// Close ends the connection, dropping our OBD demand first.
func (s *Session) Close() {
	if c := s.Client(); c != nil {
		_, _ = c.Call("obdManager", "setParameterDemand", Consumer, []string{})
		c.Close()
	}
}

// Go calls a method in the background (UI taps never wait on the network).
func (s *Session) Go(object, method string, args ...any) {
	go func() {
		if c := s.Client(); c != nil {
			if _, err := c.Call(object, method, args...); err != nil {
				log.Printf("%s.%s: %v", object, method, err)
			}
		}
	}()
}

// GoSet writes a property in the background.
func (s *Session) GoSet(object, property string, value any) {
	go func() {
		if c := s.Client(); c != nil {
			if _, err := c.Set(object, property, value); err != nil {
				log.Printf("set %s.%s: %v", object, property, err)
			}
		}
	}()
}

// CallSync calls a method and decodes its result into out (may be nil).
func (s *Session) CallSync(out any, object, method string, args ...any) error {
	c := s.Client()
	if c == nil {
		return errNotConnected
	}
	raw, err := c.Call(object, method, args...)
	if err != nil || out == nil {
		return err
	}
	return json.Unmarshal(raw, out)
}

// Demand registers the parameters on screen; repeated calls with the same
// set send nothing. It is re-sent after a reconnect.
func (s *Session) Demand(ids []string) {
	ids = slices.Clone(ids)
	slices.Sort(ids)
	ids = slices.Compact(ids)
	if ids == nil {
		ids = []string{} // JSON [], not null
	}
	s.mu.Lock()
	same := s.demand != nil && slices.Equal(ids, s.demand)
	s.demand = ids
	s.mu.Unlock()
	if !same {
		go s.sendDemand(ids)
	}
}

func (s *Session) sendDemand(ids []string) {
	if c := s.Client(); c != nil {
		if _, err := c.Call("obdManager", "setParameterDemand", Consumer, ids); err != nil {
			log.Printf("demand: %v", err)
		}
	}
}

type linkError string

func (e linkError) Error() string { return string(e) }

const errNotConnected = linkError("not connected")
