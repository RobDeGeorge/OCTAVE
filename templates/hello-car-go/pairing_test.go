package main

// connectWith against a scripted line server in this process: a pairing
// screen left open for longer than OCTAVE keeps an unpaired connection must
// dial again with the code just typed, and input that ends must not spin.

import (
	"bufio"
	"encoding/json"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
)

// unpairedServer says hello untrusted. Its first connection is dropped at the
// first pair, as one that sat idle for a minute would already be; later ones
// pair with 123456.
func unpairedServer(t *testing.T) (addr string, connections *atomic.Int32, pairs *atomic.Int32) {
	t.Helper()
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { l.Close() })
	connections, pairs = &atomic.Int32{}, &atomic.Int32{}
	go func() {
		for {
			conn, err := l.Accept()
			if err != nil {
				return
			}
			n := connections.Add(1)
			go func() {
				defer conn.Close()
				trusted := false
				send := func(id any, ok bool, result any, errText string) {
					m := map[string]any{"id": id, "ok": ok}
					if ok {
						m["result"] = result
					} else {
						m["error"] = errText
					}
					b, _ := json.Marshal(m)
					conn.Write(append(b, '\n'))
				}
				for sc := bufio.NewScanner(conn); sc.Scan(); {
					var req map[string]any
					json.Unmarshal(sc.Bytes(), &req)
					switch req["op"] {
					case "hello":
						if trusted {
							send(req["id"], true, map[string]any{"protocolVersion": 3, "auth": "trusted", "scopes": []string{"read", "media"}, "app": map[string]any{"version": "fake"}}, "")
						} else {
							send(req["id"], true, map[string]any{"protocolVersion": 3, "auth": "untrusted", "loopbackOpen": false}, "")
						}
					case "pair":
						pairs.Add(1)
						if n == 1 {
							return // the idle close
						}
						if req["code"] != "123456" {
							send(req["id"], false, nil, "wrong code")
							continue
						}
						trusted = true
						send(req["id"], true, map[string]any{"token": strings.Repeat("t", 64), "scopes": []string{"read", "media"}}, "")
					}
				}
			}()
		}
	}()
	return l.Addr().String(), connections, pairs
}

func TestPairingDialsAgainWhenTheConnectionWentWhileWaitingForTheCode(t *testing.T) {
	addr, connections, _ := unpairedServer(t)
	tokenFile := filepath.Join(t.TempDir(), ".token")
	var out strings.Builder
	c, err := connectWith(addr, tokenFile, strings.NewReader("123 456\n"), &out)
	if err != nil {
		t.Fatalf("connect: %v\n%s", err, out.String())
	}
	defer c.Close()
	if !c.Trusted() || connections.Load() != 2 {
		t.Errorf("trusted %v after %d connections, want a second dial", c.Trusted(), connections.Load())
	}
	if c.Hello.App.Version != "fake" {
		t.Errorf("hello was not asked again after pairing: %+v", c.Hello)
	}
	saved, err := os.ReadFile(tokenFile)
	if err != nil || len(saved) != 64 {
		t.Fatalf("token file: %q %v", saved, err)
	}
	if st, _ := os.Stat(tokenFile); st.Mode().Perm() != 0o600 {
		t.Errorf("token file mode %v, want 0600", st.Mode().Perm())
	}
}

func TestPairingStopsWhenTheInputEnds(t *testing.T) {
	addr, _, pairs := unpairedServer(t)
	c, err := connectWith(addr, filepath.Join(t.TempDir(), ".token"), strings.NewReader(""), io.Discard)
	if err == nil {
		c.Close()
		t.Fatal("connected without a code")
	}
	if !strings.Contains(err.Error(), "input ended") || pairs.Load() != 0 {
		t.Errorf("err %v after %d pair attempts, want none", err, pairs.Load())
	}
}

func TestAWrongCodeAsksAgain(t *testing.T) {
	addr, _, pairs := unpairedServer(t)
	var out strings.Builder
	// The first code meets the dropped connection and is retried on the new one (wrong); the second is right
	c, err := connectWith(addr, filepath.Join(t.TempDir(), ".token"), strings.NewReader("000000\n123456\n"), &out)
	if err != nil {
		t.Fatalf("connect: %v\n%s", err, out.String())
	}
	defer c.Close()
	if !strings.Contains(out.String(), "wrong code") || pairs.Load() != 3 {
		t.Errorf("%d pair attempts, output:\n%s", pairs.Load(), out.String())
	}
}
