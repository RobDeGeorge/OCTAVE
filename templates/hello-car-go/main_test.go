package main

// The template against a real `octave --headless --sim` on a free port with a
// throwaway config (OCTAVE_BIN names the binary; skipped without one): built,
// then run with -check, it must see readings and a playing track. ctest runs
// this as template_go.

import (
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	octave "github.com/RobDeGeorge/OCTAVE/sdk/go"
)

func TestHelloCarAgainstTheSimulator(t *testing.T) {
	bin := os.Getenv("OCTAVE_BIN")
	if bin == "" {
		if abs, err := filepath.Abs("../../build/octave"); err == nil {
			if _, err := os.Stat(abs); err == nil {
				bin = abs
			}
		}
	}
	if bin == "" {
		t.Skip("no OCTAVE_BIN")
	}
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	port := l.Addr().(*net.TCPAddr).Port
	l.Close()
	home := t.TempDir()
	app := exec.Command(bin, "--headless", "--sim", "--service", fmt.Sprint(port))
	app.Env = append(os.Environ(), "HOME="+home, "XDG_CONFIG_HOME="+home, "XDG_DATA_HOME="+home, "XDG_CACHE_HOME="+home, "QT_QPA_PLATFORM=offscreen")
	if err := app.Start(); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { app.Process.Signal(os.Interrupt); app.Wait() })
	addr := fmt.Sprintf("127.0.0.1:%d", port)
	for deadline := time.Now().Add(60 * time.Second); time.Now().Before(deadline); time.Sleep(200 * time.Millisecond) {
		if c, err := net.DialTimeout("tcp", addr, 500*time.Millisecond); err == nil {
			c.Close()
			break
		}
	}

	// The simulated car idles with the engine off: start it and press the pedal
	c, err := octave.Dial(addr)
	if err != nil {
		t.Fatal(err)
	}
	sim := octave.NewSim(c)
	if on, err := sim.SetEngineOn(true); err != nil || !on {
		t.Fatalf("setEngineOn: %v %v", on, err)
	}
	sim.SetThrottle(40)
	c.Close()

	exe := filepath.Join(home, "hello-car")
	if out, err := exec.Command("go", "build", "-o", exe, ".").CombinedOutput(); err != nil {
		t.Fatalf("go build: %v\n%s", err, out)
	}
	run := exec.Command(exe, "-port", fmt.Sprint(port), "-check", "4s")
	run.Dir = home // its token and cover files land here, not in the tree
	out, err := run.CombinedOutput()
	if err != nil || !strings.Contains(string(out), "all checks passed") || !strings.Contains(string(out), "RPM") {
		t.Fatalf("hello-car -check: %v\n%s", err, out)
	}
}
