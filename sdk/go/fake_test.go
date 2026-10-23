package octave

// The client against tools/api/fake_server.py, the conformance kit for SDK
// authors: no OCTAVE build needed, only python3. Skipped without one.

import (
	"bufio"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func startFake(t *testing.T, args ...string) string {
	t.Helper()
	py, err := exec.LookPath("python3")
	if err != nil {
		if py, err = exec.LookPath("python"); err != nil {
			t.Skip("no python3 for tools/api/fake_server.py")
		}
	}
	script := filepath.Join("..", "..", "tools", "api", "fake_server.py")
	cmd := exec.Command(py, append([]string{script, "--port", "0"}, args...)...)
	cmd.Stderr = os.Stderr
	out, err := cmd.StdoutPipe()
	if err != nil {
		t.Fatal(err)
	}
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { cmd.Process.Kill(); cmd.Wait() })
	line, err := bufio.NewReader(out).ReadString('\n')
	if err != nil || !strings.HasPrefix(line, "listening on ") {
		t.Fatalf("fake server did not start: %q %v", line, err)
	}
	return "127.0.0.1:" + strings.TrimSpace(strings.TrimPrefix(line, "listening on "))
}

func TestAgainstTheFakeServer(t *testing.T) {
	addr := startFake(t)
	c, err := Dial(addr, WithTimeout(10*time.Second))
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if c.Hello.ProtocolVersion != ProtocolVersion || !c.Trusted() || c.Scopes()[0] != "full" {
		t.Fatalf("hello %+v", c.Hello)
	}
	all, err := c.DescribeAll()
	if err != nil || len(all.Ops) != len(Ops) {
		t.Fatalf("describe: %v, ops %v", err, all.Ops)
	}
	vol := NewVolumeController(c)
	if err := vol.ApplyVolume(44); err != nil {
		t.Fatal(err)
	}
	if p, err := vol.GetPercent(); err != nil || p != 44 {
		t.Errorf("percent %v %v", p, err)
	}
	if err := c.Set("volumeController", "percent", 1, nil); err == nil { // read-only in docs/api
		t.Error("set of a read-only property accepted")
	}
	sub, err := c.Subscribe(SubscribeArgs{Object: "paramStore", Properties: []string{"RPM"}})
	if err != nil {
		t.Fatal(err)
	}
	var p ParamsEvent
	ev := nextEvent(t, c, EventParams)
	if err := ev.Decode(&p); err != nil || p.Sub != sub.Sub || p.Values["RPM"] == nil {
		t.Errorf("params %s %v", ev.Raw, err)
	}
	c.Unsubscribe(sub.Sub)

	info, err := c.Stream("phoneMirrorManager", "video")
	if err != nil || info.Format != "annexb" || info.Codec == "" {
		t.Fatalf("stream %+v %v", info, err)
	}
	select {
	case r := <-c.Records:
		if r.Stream != uint8(info.Stream) || !r.Key() || !r.Config() || !r.Discontinuity() {
			t.Errorf("first record %+v", r)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("no record")
	}
	c.Unstream(info.Stream)

	// Pairing with scopes, and permissions enforced from docs/api's `scope`
	if _, err := c.Pair("000000", "go test"); err == nil {
		t.Error("a wrong code paired")
	}
	token, err := c.Pair("123456", "go test", "media")
	if err != nil || len(token) != 64 || strings.Join(c.Scopes(), ",") != "media,read" {
		t.Fatalf("pair %v %v %v", token, c.Scopes(), err)
	}
	again, err := Dial(addr, WithToken(token))
	if err != nil {
		t.Fatal(err)
	}
	defer again.Close()
	if err := NewNowPlaying(again).TogglePlay(); err != nil {
		t.Error("media op refused:", err)
	}
	err = NewNowPlaying(again).ExtractArtColors()
	var e *Error
	if !errors.As(err, &e) || e.Code != CodeForbidden {
		t.Errorf("want forbidden, got %v", err)
	}

	// Faults on demand
	if err := c.Call("fake", "rateLimitNext", nil, 1); err != nil { // the fake's own control object
		t.Fatal(err)
	}
	if _, err := vol.GetPercent(); !errors.As(err, &e) || e.Code != CodeRateLimited {
		t.Errorf("want rateLimited, got %v", err)
	}
	if _, err := vol.GetPercent(); err != nil {
		t.Error(err)
	}
}

func TestUntrustedFakeServer(t *testing.T) {
	addr := startFake(t, "--untrusted", "--code", "654321")
	c, err := Dial(addr)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if c.Trusted() {
		t.Fatal("untrusted server reported trusted")
	}
	var e *Error
	if _, err := NewVolumeController(c).GetPercent(); !errors.As(err, &e) || e.Code != CodeUnauthorised {
		t.Errorf("want unauthorised, got %v", err)
	}
	if _, err := c.Pair("654321", "go test"); err != nil {
		t.Fatal(err)
	}
	if _, err := NewVolumeController(c).GetPercent(); err != nil {
		t.Error(err)
	}
}
