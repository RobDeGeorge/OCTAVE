package octave

import (
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// The client against a real `octave --headless`, found through OCTAVE_BIN
// (ctest sets it; skipped without one). The app runs on a free port with a
// throwaway config, like sdk/python's launch_headless.
func startApp(t *testing.T) (addr string) {
	t.Helper()
	bin := os.Getenv("OCTAVE_BIN")
	if bin == "" {
		for _, candidate := range []string{"../../build/octave"} {
			if _, err := os.Stat(candidate); err == nil {
				bin, _ = filepath.Abs(candidate)
			}
		}
	}
	if bin == "" {
		t.Skip("no OCTAVE_BIN: set it to a built octave to run the client test")
	}
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	port := l.Addr().(*net.TCPAddr).Port
	l.Close()
	home := t.TempDir()
	cmd := exec.Command(bin, "--headless", "--service", fmt.Sprint(port))
	cmd.Env = append(os.Environ(), "HOME="+home, "XDG_CONFIG_HOME="+home, "XDG_DATA_HOME="+home, "XDG_CACHE_HOME="+home, "QT_QPA_PLATFORM=offscreen")
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		cmd.Process.Signal(os.Interrupt)
		done := make(chan error, 1)
		go func() { done <- cmd.Wait() }()
		select {
		case <-done:
		case <-time.After(15 * time.Second):
			cmd.Process.Kill()
			<-done
		}
	})
	addr = fmt.Sprintf("127.0.0.1:%d", port)
	deadline := time.Now().Add(60 * time.Second)
	for time.Now().Before(deadline) {
		if c, err := net.DialTimeout("tcp", addr, 500*time.Millisecond); err == nil {
			c.Close()
			return addr
		}
		time.Sleep(200 * time.Millisecond)
	}
	t.Fatal("octave did not serve in time")
	return ""
}

func TestClientAgainstHeadlessOctave(t *testing.T) {
	addr := startApp(t)
	c, err := Dial(addr, WithTimeout(15*time.Second))
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()

	t.Run("hello", func(t *testing.T) {
		if c.Hello.ProtocolVersion != ProtocolVersion {
			t.Errorf("protocol %d, generated for %d", c.Hello.ProtocolVersion, ProtocolVersion)
		}
		if !c.Trusted() || !c.Hello.App.Headless {
			t.Errorf("hello: %+v", c.Hello)
		}
	})
	t.Run("the server's ops are the generated ones", func(t *testing.T) {
		all, err := c.DescribeAll()
		if err != nil {
			t.Fatal(err)
		}
		if len(all.Ops) != len(Ops) {
			t.Fatalf("server ops %v, generated %v", all.Ops, Ops)
		}
		have := map[string]bool{}
		for _, op := range all.Ops {
			have[op] = true
		}
		for _, op := range Ops {
			if !have[op] {
				t.Errorf("server lacks op %s", op)
			}
		}
		for _, name := range ObjectNames {
			if _, ok := all.Objects[name]; !ok {
				t.Errorf("server lacks object %s", name)
			}
		}
	})
	t.Run("typed object: call, get, set", func(t *testing.T) {
		vol := NewVolumeController(c)
		if err := vol.ApplyVolume(27); err != nil {
			t.Fatal(err)
		}
		if p, err := vol.GetPercent(); err != nil || p != 27 {
			t.Errorf("percent %v %v", p, err)
		}
		if m, err := vol.SetMuted(false); err != nil || m {
			t.Errorf("muted %v %v", m, err)
		}
		info, err := NewParameterCatalog(c).Info("RPM")
		if err != nil || info["id"] != "RPM" {
			t.Errorf("info %v %v", info, err)
		}
		// The app has no list-model property today (tst_servicebridge and the fake server cover rows)
		var e *Error
		if _, err := c.Rows("volumeController", "percent", 0, -1); !asError(err, &e) || !strings.Contains(e.Message, "not a list model") {
			t.Errorf("rows of a plain property: %v", err)
		}
	})
	t.Run("errors carry the message and the code", func(t *testing.T) {
		err := c.Get("noSuchObject", "x", nil)
		var e *Error
		if !asError(err, &e) || e.Op != "get" || e.Message == "" || e.Code != "" {
			t.Errorf("want *Error, got %#v", err)
		}
	})
	t.Run("subscribe: the value at once, then on change", func(t *testing.T) {
		sub, err := c.Subscribe(SubscribeArgs{Object: "volumeController", Properties: []string{"percent"}})
		if err != nil {
			t.Fatal(err)
		}
		first := nextEvent(t, c, EventProperty)
		var p PropertyEvent
		if err := first.Decode(&p); err != nil || p.Sub != sub.Sub || p.Name != "percent" {
			t.Fatalf("first event %s %v", first.Raw, err)
		}
		if err := NewVolumeController(c).ApplyVolume(31); err != nil {
			t.Fatal(err)
		}
		for {
			ev := nextEvent(t, c, EventProperty)
			ev.Decode(&p)
			if v, _ := p.Value.(float64); v == 31 {
				break
			}
		}
		if err := c.Unsubscribe(sub.Sub); err != nil {
			t.Error(err)
		}
	})
	t.Run("stream reply", func(t *testing.T) {
		info, err := c.Stream("androidAutoManager", "frames")
		if err != nil || info.Format != Streams["androidAutoManager.frames"] {
			t.Errorf("stream %+v %v", info, err)
		}
		if err := c.Unstream(info.Stream); err != nil {
			t.Error(err)
		}
	})
	t.Run("media ticket", func(t *testing.T) {
		tk, err := c.MediaTicket()
		if err != nil || tk.ExpiresIn != 600 || tk.Ticket == "" {
			t.Errorf("ticket %+v %v", tk, err)
		}
	})
}

func nextEvent(t *testing.T, c *Client, kind string) Event {
	t.Helper()
	for {
		select {
		case ev, ok := <-c.Events:
			if !ok {
				t.Fatal("connection closed")
			}
			if ev.Kind == kind {
				return ev
			}
		case <-time.After(10 * time.Second):
			t.Fatalf("no %s event", kind)
		}
	}
}

func asError(err error, target **Error) bool {
	e, ok := err.(*Error)
	if ok {
		*target = e
	}
	return ok
}

func TestParseRecord(t *testing.T) {
	b := []byte{1, 7, FlagKey | FlagDiscontinuity, 0, 0, 0, 0, 0, 0, 0, 0x12, 0x34, 0xAA, 0xBB}
	r, err := ParseRecord(b)
	if err != nil || r.Stream != 7 || !r.Key() || !r.Discontinuity() || r.JPEG() || r.PTS != 0x1234 || len(r.Payload) != 2 {
		t.Errorf("%+v %v", r, err)
	}
	if _, err := ParseRecord(b[:5]); err == nil {
		t.Error("short record accepted")
	}
}
