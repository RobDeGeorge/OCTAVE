package link

import (
	"bufio"
	"encoding/json"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestParseLine(t *testing.T) {
	m, err := ParseLine([]byte(`{"id":3,"ok":true,"result":32}`))
	if err != nil || m.ID == nil || *m.ID != 3 || !m.OK || string(m.Result) != "32" {
		t.Fatalf("reply: %+v %v", m, err)
	}
	m, err = ParseLine([]byte(`{"event":"params","sub":1,"object":"paramStore","values":{"RPM":2210,"SPEED":41.5},"t":1}`))
	if err != nil || m.Event != "params" || string(m.Values["SPEED"]) != "41.5" {
		t.Fatalf("params: %+v %v", m, err)
	}
	m, err = ParseLine([]byte(`{"id":4,"ok":false,"error":"no such object"}`))
	if err != nil || m.OK || m.Error != "no such object" {
		t.Fatalf("error reply: %+v %v", m, err)
	}
	if _, err = ParseLine([]byte(`{"hello":1}`)); err == nil {
		t.Fatal("accepted a line that is neither reply nor event")
	}
	if _, err = ParseLine([]byte(`{`)); err == nil {
		t.Fatal("accepted broken JSON")
	}
}

func apply(t *testing.T, s *State, line string) {
	t.Helper()
	m, err := ParseLine([]byte(line))
	if err != nil {
		t.Fatal(err)
	}
	s.Apply(m)
}

func TestStateFoldsEvents(t *testing.T) {
	s := NewState()
	apply(t, s, `{"event":"params","values":{"COOLANT_TEMP":90,"RPM":800}}`)
	if v, _ := s.Value("COOLANT_TEMP"); v != 90 {
		t.Fatalf("native before catalog: %v", v)
	}
	// the user picked °F: displayParameters carries the conversion
	apply(t, s, `{"event":"property","object":"parameterCatalog","name":"displayParameters","value":[
		{"id":"COOLANT_TEMP","title":"Coolant Temp","unit":"°F","min":32,"max":248,"decimals":0,"displayScale":1.8,"displayOffset":32}]}`)
	if v, _ := s.Value("COOLANT_TEMP"); v != 194 {
		t.Fatalf("display value: %v", v)
	}
	if p, ok := s.Param("COOLANT_TEMP"); !ok || p.Unit != "°F" || p.Max != 248 {
		t.Fatalf("catalog: %+v", p)
	}
	apply(t, s, `{"event":"params","values":{"COOLANT_TEMP":100}}`)
	if h := s.History("COOLANT_TEMP"); len(h) != 2 || h[0] != 194 || h[1] != 212 {
		t.Fatalf("history: %v", h)
	}
	if v, _ := s.Value("RPM"); v != 800 {
		t.Fatalf("an id the catalog lacks stays native: %v", v)
	}
	apply(t, s, `{"event":"property","object":"nowPlaying","name":"title","value":"Dream On"}`)
	apply(t, s, `{"event":"property","object":"volumeController","name":"muted","value":true}`)
	apply(t, s, `{"event":"property","object":"volumeController","name":"percent","value":35}`)
	if s.Str("nowPlaying", "title") != "Dream On" || !s.Bool("volumeController", "muted") || s.Num("volumeController", "percent") != 35 {
		t.Fatal("properties not folded")
	}
	apply(t, s, `{"event":"signal","object":"clock","name":"timeChanged","args":["14:05"]}`)
	if s.Clock() != "14:05" {
		t.Fatalf("clock: %q", s.Clock())
	}
	for i := 0; i < HistoryLen+10; i++ {
		apply(t, s, `{"event":"params","values":{"RPM":1000}}`)
	}
	if len(s.History("RPM")) != HistoryLen {
		t.Fatal("history not capped")
	}
	s.SetLink(false, "LOST", "")
	if s.Str("nowPlaying", "title") != "" {
		t.Fatal("properties survive a lost link")
	}
}

// A fake service: replies to each request, with an event pushed in front of every reply.
func fakeService(t *testing.T) string { return fakeServiceVersion(t, 1) }

func fakeServiceVersion(t *testing.T, version int) string {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { ln.Close() })
	go func() {
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		defer conn.Close()
		sc := bufio.NewScanner(conn)
		for sc.Scan() {
			var req map[string]any
			if json.Unmarshal(sc.Bytes(), &req) != nil {
				return
			}
			conn.Write([]byte(`{"event":"params","sub":1,"object":"paramStore","values":{"RPM":3000}}` + "\n"))
			var reply map[string]any
			switch req["op"] {
			case "hello":
				reply = map[string]any{"ok": true, "result": map[string]any{"protocolVersion": version, "app": map[string]any{"version": "9.9"}}}
			case "call":
				reply = map[string]any{"ok": true, "result": req["args"]}
			default:
				reply = map[string]any{"ok": false, "error": "unknown op"}
			}
			reply["id"] = req["id"]
			line, _ := json.Marshal(reply)
			conn.Write(append(line, '\n'))
		}
	}()
	return ln.Addr().String()
}

func TestClientMatchesRepliesAroundEvents(t *testing.T) {
	c, err := Dial(fakeService(t), time.Second)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if v, err := c.Hello(); err != nil || v != "9.9" {
		t.Fatalf("hello: %q %v", v, err)
	}
	raw, err := c.Call("obdManager", "setParameterDemand", "sunlight", []string{"RPM"})
	if err != nil || string(raw) != `["sunlight",["RPM"]]` {
		t.Fatalf("call: %s %v", raw, err)
	}
	if _, err := c.Request("frob", nil); err == nil || err.Error() != "frob: unknown op" {
		t.Fatalf("error reply: %v", err)
	}
	select {
	case m := <-c.Events():
		if m.Event != "params" {
			t.Fatalf("event: %+v", m)
		}
	case <-time.After(time.Second):
		t.Fatal("no event")
	}
}

func TestClientFailsPendingOnClose(t *testing.T) {
	ln, _ := net.Listen("tcp", "127.0.0.1:0")
	defer ln.Close()
	go func() {
		conn, _ := ln.Accept()
		time.Sleep(50 * time.Millisecond)
		conn.Close() // never answers
	}()
	c, err := Dial(ln.Addr().String(), time.Second)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := c.Request("hello", nil); err == nil {
		t.Fatal("request on a dropped connection succeeded")
	}
	if _, ok := <-c.Events(); ok {
		t.Fatal("events channel left open")
	}
}

func TestStateDTCsAndTripDetails(t *testing.T) {
	s := NewState()
	if _, known, _ := s.DTCs(); known {
		t.Fatal("codes known before any read")
	}
	apply(t, s, `{"event":"signal","object":"obdManager","name":"dtcCodesChanged","args":[[{"code":"P0300","description":"Misfire"},{"code":"P0420","description":""}]]}`)
	apply(t, s, `{"event":"signal","object":"obdManager","name":"milStatusChanged","args":[true]}`)
	codes, known, mil := s.DTCs()
	if !known || !mil || len(codes) != 2 || codes[0].Code != "P0300" || codes[0].Description != "Misfire" {
		t.Fatalf("dtcs: %+v %v %v", codes, known, mil)
	}
	apply(t, s, `{"event":"signal","object":"obdManager","name":"dtcClearResult","args":[true,""]}`)
	if s.ClearMessage() != "CODES CLEARED" {
		t.Fatalf("clear message: %q", s.ClearMessage())
	}
	apply(t, s, `{"event":"signal","object":"tripRecorder","name":"detailReady","args":[1792743146937,{"distanceMiles":1.5,"maxSpeed":41}]}`)
	var d struct {
		DistanceMiles float64 `json:"distanceMiles"`
		MaxSpeed      float64 `json:"maxSpeed"`
	}
	if !s.Detail(1792743146937, &d) || d.DistanceMiles != 1.5 || d.MaxSpeed != 41 {
		t.Fatalf("detail: %+v", d)
	}
	if s.Detail(42, &d) {
		t.Fatal("detail for an unknown trip")
	}
}

func TestHelloAcceptsKnownVersionsOnly(t *testing.T) {
	for version, ok := range map[int]bool{1: true, 2: true, 3: false} {
		c, err := Dial(fakeServiceVersion(t, version), time.Second)
		if err != nil {
			t.Fatal(err)
		}
		_, err = c.Hello()
		c.Close()
		if (err == nil) != ok {
			t.Fatalf("protocol %d: err %v", version, err)
		}
	}
}

func TestTokensFile(t *testing.T) {
	tok := Tokens{Path: filepath.Join(t.TempDir(), "sub", "tokens.json")}
	if tok.Get("a:1") != "" {
		t.Fatal("token before any was stored")
	}
	if err := tok.Put("a:1", "aa"); err != nil {
		t.Fatal(err)
	}
	_ = tok.Put("b:2", "bb")
	if tok.Get("a:1") != "aa" || tok.Get("b:2") != "bb" {
		t.Fatal("tokens not kept per address")
	}
	for path, want := range map[string]os.FileMode{tok.Path: 0o600, filepath.Dir(tok.Path): 0o700} {
		if fi, err := os.Stat(path); err != nil || fi.Mode().Perm() != want {
			t.Fatalf("%s: mode %v, want %v", path, fi.Mode().Perm(), want)
		}
	}
	_ = tok.Put("a:1", "")
	if tok.Get("a:1") != "" || tok.Get("b:2") != "bb" {
		t.Fatal("forgetting one token touched another")
	}
}

func TestHelloTrustAndPair(t *testing.T) {
	ln, _ := net.Listen("tcp", "127.0.0.1:0")
	defer ln.Close()
	go func() {
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		defer conn.Close()
		sc := bufio.NewScanner(conn)
		for sc.Scan() {
			var req map[string]any
			_ = json.Unmarshal(sc.Bytes(), &req)
			reply := map[string]any{"id": req["id"], "ok": true}
			switch req["op"] {
			case "hello":
				reply["result"] = map[string]any{"protocolVersion": 2, "auth": "untrusted", "app": map[string]any{"version": "1"}}
			case "pair":
				if req["code"] == "123456" {
					reply["result"] = map[string]any{"token": "ff00"}
				} else {
					reply["ok"], reply["error"] = false, "wrong code"
				}
			}
			line, _ := json.Marshal(reply)
			conn.Write(append(line, '\n'))
		}
	}()
	c, err := Dial(ln.Addr().String(), time.Second)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	h, err := c.HelloToken("")
	if err != nil || h.Trusted {
		t.Fatalf("hello: %+v %v", h, err)
	}
	if _, err := c.Pair("000000", "x"); err == nil {
		t.Fatal("a wrong code paired")
	}
	if tok, err := c.Pair("123456", "x"); err != nil || tok != "ff00" {
		t.Fatalf("pair: %q %v", tok, err)
	}
}
