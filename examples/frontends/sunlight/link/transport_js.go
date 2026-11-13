//go:build js

package link

import (
	"errors"
	"fmt"
	"sync"
	"syscall/js"
	"time"
)

// wsTransport is the service's WebSocket side, through the browser's own
// WebSocket: one JSON message per text frame. Only text frames are used, so
// it also works over the stand-in OCTAVE's browser demo puts in place of
// WebSocket (packaging/wasm/frontend-host.js), which carries nothing else.
type wsTransport struct {
	ws    js.Value
	funcs map[string]js.Func
	once  sync.Once

	mu     sync.Mutex
	queue  [][]byte
	closed bool
	wake   chan struct{} // a frame arrived or the socket closed
	opened chan struct{}
}

// URL is the service's address as seen from the page: the host that served
// it. A page without one (the sandboxed srcdoc frame of the browser demo,
// where the stand-in ignores the address) still gets a well-formed URL.
func URL(host string) string {
	scheme := "ws://"
	if loc := js.Global().Get("location"); loc.Truthy() && loc.Get("protocol").String() == "https:" {
		scheme = "wss://"
	}
	if host == "" {
		host = "127.0.0.1:9876"
	}
	return scheme + host + "/"
}

func dial(addr string, timeout time.Duration) (t transport, err error) {
	// a constructor that throws (a bad URL, a blocked socket) is an error, not a crash
	defer func() {
		if r := recover(); r != nil {
			t, err = nil, fmt.Errorf("websocket: %v", r)
		}
	}()
	ctor := js.Global().Get("WebSocket")
	if !ctor.Truthy() {
		return nil, errors.New("this browser has no WebSocket")
	}
	w := &wsTransport{funcs: map[string]js.Func{}, wake: make(chan struct{}, 1), opened: make(chan struct{})}
	w.ws = ctor.New(URL(addr))
	w.on("open", func(js.Value) { close(w.opened) })
	w.on("message", func(ev js.Value) {
		data := ev.Get("data")
		if data.Type() != js.TypeString {
			return // a binary media record: not used here
		}
		w.mu.Lock()
		w.queue = append(w.queue, []byte(data.String()))
		w.mu.Unlock()
		w.signal()
	})
	end := func(js.Value) {
		w.mu.Lock()
		w.closed = true
		w.mu.Unlock()
		w.signal()
	}
	w.on("close", end)
	w.on("error", end)
	timer := time.NewTimer(timeout)
	defer timer.Stop()
	for {
		select {
		case <-w.opened:
			return w, nil
		case <-w.wake:
			w.mu.Lock()
			closed := w.closed
			w.mu.Unlock()
			if closed {
				w.Close()
				return nil, errors.New("cannot reach the service")
			}
		case <-timer.C:
			w.Close()
			return nil, errors.New("the service did not answer")
		}
	}
}

// on listens without ever blocking the browser's event loop.
func (w *wsTransport) on(event string, fn func(ev js.Value)) {
	f := js.FuncOf(func(_ js.Value, args []js.Value) any {
		var ev js.Value
		if len(args) > 0 {
			ev = args[0]
		}
		fn(ev)
		return nil
	})
	w.funcs[event] = f
	w.ws.Call("addEventListener", event, f)
}

func (w *wsTransport) signal() {
	select {
	case w.wake <- struct{}{}:
	default:
	}
}

func (w *wsTransport) ReadMessage() ([]byte, error) {
	for {
		w.mu.Lock()
		if len(w.queue) > 0 {
			msg := w.queue[0]
			w.queue[0] = nil
			w.queue = w.queue[1:]
			w.mu.Unlock()
			return msg, nil
		}
		closed := w.closed
		w.mu.Unlock()
		if closed {
			return nil, errLinkClosed
		}
		<-w.wake
	}
}

func (w *wsTransport) WriteMessage(msg []byte) (err error) {
	defer func() {
		if r := recover(); r != nil { // send() throws unless the socket is open
			err = fmt.Errorf("websocket: %v", r)
		}
	}()
	w.ws.Call("send", string(msg))
	return nil
}

func (w *wsTransport) Close() error {
	w.once.Do(func() {
		defer func() { _ = recover() }()
		w.mu.Lock()
		w.closed = true
		w.mu.Unlock()
		w.signal()
		// stop listening before the callbacks are released
		for event, f := range w.funcs {
			w.ws.Call("removeEventListener", event, f)
			f.Release()
		}
		w.ws.Call("close")
	})
	return nil
}
