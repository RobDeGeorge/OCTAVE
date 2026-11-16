package octave

// The connection plumbing against a scripted line server in this process:
// what a consumer that stops reading, a cancelled context, a refusal with no
// id and a rate limit do to the client. No OCTAVE and no python needed.

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"testing"
	"time"
)

// lineServer answers hello, then hands every other request to `handle`,
// which writes whatever lines it likes.
func lineServer(t *testing.T, handle func(req map[string]any, send func(any))) string {
	t.Helper()
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { l.Close() })
	go func() {
		for {
			conn, err := l.Accept()
			if err != nil {
				return
			}
			go func() {
				defer conn.Close()
				send := func(v any) {
					b, _ := json.Marshal(v)
					conn.Write(append(b, '\n'))
				}
				sc := bufio.NewScanner(conn)
				for sc.Scan() {
					var req map[string]any
					if json.Unmarshal(sc.Bytes(), &req) != nil {
						continue
					}
					if req["op"] == "hello" {
						send(map[string]any{"id": req["id"], "ok": true, "result": map[string]any{"protocolVersion": ProtocolVersion, "auth": "trusted", "scopes": []string{"full"}}})
						continue
					}
					handle(req, send)
				}
			}()
		}
	}()
	return l.Addr().String()
}

func TestAConsumerThatStopsReadingDoesNotWedgeTheClient(t *testing.T) {
	const flood = 2000 // far more than Events holds
	addr := lineServer(t, func(req map[string]any, send func(any)) {
		if req["op"] == "flood" {
			for i := 0; i < flood; i++ {
				send(map[string]any{"event": "property", "sub": 1, "object": "o", "name": "n", "value": i})
			}
		}
		send(map[string]any{"id": req["id"], "ok": true, "result": 7})
	})
	c, err := Dial(addr, WithTimeout(5*time.Second))
	if err != nil {
		t.Fatal(err)
	}
	// Nobody reads c.Events: the reply behind 2000 events must still arrive
	var n int
	if err := c.Request("flood", nil, &n); err != nil || n != 7 {
		t.Fatalf("request behind a flood of events: %v %v", n, err)
	}
	if err := c.Request("again", nil, &n); err != nil {
		t.Fatalf("the client is wedged after the flood: %v", err)
	}
	if got := c.DroppedEvents(); got != uint64(flood-cap(c.Events)) {
		t.Errorf("dropped %d events, want %d", got, flood-cap(c.Events))
	}
	// What is kept is the newest: the last event is the last one sent
	var last PropertyEvent
	for len(c.Events) > 0 {
		ev := <-c.Events
		ev.Decode(&last)
	}
	if v, _ := last.Value.(float64); int(v) != flood-1 {
		t.Errorf("the newest event kept is %v, want %d", last.Value, flood-1)
	}

	closed := make(chan error, 1)
	go func() { closed <- c.Close() }()
	select {
	case <-closed:
	case <-time.After(5 * time.Second):
		t.Fatal("Close did not return")
	}
	if _, open := <-c.Events; open {
		t.Error("Events is still open after Close")
	}
	if _, open := <-c.Records; open {
		t.Error("Records is still open after Close")
	}
	if err := c.Request("after", nil, nil); !errors.Is(err, ErrClosed) {
		t.Errorf("a request after Close: %v", err)
	}
	if err := c.Close(); err != nil {
		t.Errorf("a second Close: %v", err)
	}
}

func TestRecordsDropTheOldest(t *testing.T) {
	const flood = 300
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	go func() {
		conn, err := l.Accept()
		if err != nil {
			return
		}
		defer conn.Close()
		sc := bufio.NewScanner(conn)
		for sc.Scan() {
			var req map[string]any
			json.Unmarshal(sc.Bytes(), &req)
			if req["op"] == "records" {
				for i := 0; i < flood; i++ {
					rec := []byte{1, 3, 0, 0, 0, 0, 0, 0, 0, 0, byte(i >> 8), byte(i), 0xAA}
					conn.Write(append([]byte{0xFF, 0, 0, 0, byte(len(rec))}, rec...))
				}
			}
			fmt.Fprintf(conn, `{"id":%v,"ok":true,"result":{"protocolVersion":%d}}`+"\n", req["id"], ProtocolVersion)
		}
	}()
	c, err := Dial(l.Addr().String(), WithTimeout(5*time.Second))
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if err := c.Request("records", nil, nil); err != nil {
		t.Fatal(err)
	}
	if got := c.DroppedRecords(); got != uint64(flood-cap(c.Records)) {
		t.Errorf("dropped %d records, want %d", got, flood-cap(c.Records))
	}
	if first := <-c.Records; first.PTS != uint64(flood-cap(c.Records)) {
		t.Errorf("the oldest record kept has pts %d, want %d", first.PTS, flood-cap(c.Records))
	}
}

func TestRequestContextEndsWithItsContext(t *testing.T) {
	addr := lineServer(t, func(req map[string]any, send func(any)) {
		if req["op"] == "silent" {
			return // never answered
		}
		send(map[string]any{"id": req["id"], "ok": true, "result": true})
	})
	c, err := Dial(addr, WithTimeout(30*time.Second))
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()
	start := time.Now()
	if err := c.RequestContext(ctx, "silent", nil, nil); !errors.Is(err, context.DeadlineExceeded) {
		t.Fatalf("want the context's error, got %v", err)
	}
	if time.Since(start) > 5*time.Second {
		t.Error("the context did not end the request")
	}
	done, cancelled := context.WithCancel(context.Background())
	cancelled()
	if err := c.RequestContext(done, "fine", nil, nil); !errors.Is(err, context.Canceled) {
		t.Errorf("a cancelled context still sent: %v", err)
	}
	if err := c.Request("fine", nil, nil); err != nil {
		t.Errorf("the client does not work after a cancelled request: %v", err)
	}
	c.mu.Lock()
	left := len(c.pending)
	c.mu.Unlock()
	if left != 0 {
		t.Errorf("%d requests left pending", left)
	}
}

func TestRefusalsCarryRetryAfterAndAnIdlessOneFailsWhatWaits(t *testing.T) {
	addr := lineServer(t, func(req map[string]any, send func(any)) {
		switch req["op"] {
		case "limited":
			send(map[string]any{"id": req["id"], "ok": false, "error": "too many ops", "code": CodeRateLimited, "retryAfterMs": 25})
		case "unreadable":
			send(map[string]any{"ok": false, "error": "Invalid JSON: test"}) // as the app answers a line it cannot parse
		default:
			send(map[string]any{"id": req["id"], "ok": true, "result": true})
		}
	})
	c, err := Dial(addr, WithTimeout(10*time.Second))
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	var e *Error
	if err := c.Request("limited", nil, nil); !errors.As(err, &e) || e.Code != CodeRateLimited || e.RetryAfterMs != 25 {
		t.Errorf("want rateLimited with retryAfterMs 25, got %#v", err)
	}
	start := time.Now()
	if err := c.Request("unreadable", nil, nil); !errors.As(err, &e) || e.Message != "Invalid JSON: test" {
		t.Errorf("want the id-less refusal, got %v", err)
	}
	if time.Since(start) > 5*time.Second {
		t.Error("an id-less refusal waited for the timeout")
	}
	if err := c.Request("fine", nil, nil); err != nil {
		t.Error(err)
	}
}

func TestProtocolVersionsFollowTheGeneratedOne(t *testing.T) {
	if len(ProtocolVersions) != ProtocolVersion || ProtocolVersions[0] != 1 || ProtocolVersions[len(ProtocolVersions)-1] != ProtocolVersion {
		t.Errorf("ProtocolVersions %v, generated version %d", ProtocolVersions, ProtocolVersion)
	}
}
