// Package link is sunlight's OCTAVE client: JSON lines over TCP (net +
// encoding/json only), request / reply matching by id, subscription events
// folded into a State the UI reads, and a session that reconnects.
package link

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"sync"
	"time"
)

// ProtocolVersion is the service protocol this client was written against.
const ProtocolVersion = 1

// Message is any line the service sends: a reply (ID set) or an event.
type Message struct {
	ID     *int64          `json:"id,omitempty"`
	OK     bool            `json:"ok"`
	Result json.RawMessage `json:"result,omitempty"`
	Error  string          `json:"error,omitempty"`

	Event  string                     `json:"event,omitempty"` // property, signal, params, model, dropped
	Sub    int                        `json:"sub,omitempty"`
	Object string                     `json:"object,omitempty"`
	Name   string                     `json:"name,omitempty"`
	Value  json.RawMessage            `json:"value,omitempty"`
	Values map[string]json.RawMessage `json:"values,omitempty"`
	Args   []json.RawMessage          `json:"args,omitempty"`
	Count  int                        `json:"count,omitempty"`
}

// ParseLine decodes one protocol line.
func ParseLine(line []byte) (Message, error) {
	var m Message
	if err := json.Unmarshal(line, &m); err != nil {
		return m, fmt.Errorf("bad line: %w", err)
	}
	if m.ID == nil && m.Event == "" {
		return m, errors.New("line is neither a reply nor an event")
	}
	return m, nil
}

// Client is one connection. Request may be called from any goroutine.
type Client struct {
	conn    net.Conn
	mu      sync.Mutex
	nextID  int64
	pending map[int64]chan Message
	closed  bool
	events  chan Message
	Timeout time.Duration
}

// Dial connects and starts reading. Events arrive on Events().
func Dial(addr string, timeout time.Duration) (*Client, error) {
	conn, err := net.DialTimeout("tcp", addr, timeout)
	if err != nil {
		return nil, err
	}
	c := &Client{conn: conn, pending: map[int64]chan Message{}, events: make(chan Message, 1024), Timeout: 10 * time.Second}
	go c.read()
	return c, nil
}

// Events yields subscription events; it is closed when the connection goes.
func (c *Client) Events() <-chan Message { return c.events }

func (c *Client) read() {
	sc := bufio.NewScanner(c.conn)
	sc.Buffer(make([]byte, 0, 1<<16), 16<<20)
	for sc.Scan() {
		m, err := ParseLine(sc.Bytes())
		if err != nil {
			continue
		}
		if m.ID != nil && m.Event == "" {
			c.mu.Lock()
			ch := c.pending[*m.ID]
			delete(c.pending, *m.ID)
			c.mu.Unlock()
			if ch != nil {
				ch <- m
			}
			continue
		}
		select {
		case c.events <- m:
		default: // the UI is behind: drop, the next batch carries the latest values
		}
	}
	c.mu.Lock()
	c.closed = true
	for id, ch := range c.pending {
		close(ch)
		delete(c.pending, id)
	}
	c.mu.Unlock()
	close(c.events)
}

// Close ends the connection.
func (c *Client) Close() { c.conn.Close() }

// Request sends any op and waits for its reply's result.
func (c *Client) Request(op string, args map[string]any) (json.RawMessage, error) {
	c.mu.Lock()
	if c.closed {
		c.mu.Unlock()
		return nil, errors.New("connection closed")
	}
	c.nextID++
	id := c.nextID
	ch := make(chan Message, 1)
	c.pending[id] = ch
	msg := map[string]any{"op": op, "id": id}
	for k, v := range args {
		msg[k] = v
	}
	line, err := json.Marshal(msg)
	if err == nil {
		_, err = c.conn.Write(append(line, '\n'))
	}
	c.mu.Unlock()
	if err != nil {
		return nil, err
	}
	select {
	case m, ok := <-ch:
		if !ok {
			return nil, errors.New("connection closed")
		}
		if !m.OK {
			return nil, fmt.Errorf("%s: %s", op, m.Error)
		}
		return m.Result, nil
	case <-time.After(c.Timeout):
		c.mu.Lock()
		delete(c.pending, id)
		c.mu.Unlock()
		return nil, fmt.Errorf("%s: no reply within %s", op, c.Timeout)
	}
}

// Call calls a slot or invokable.
func (c *Client) Call(object, method string, args ...any) (json.RawMessage, error) {
	if args == nil {
		args = []any{}
	}
	return c.Request("call", map[string]any{"object": object, "method": method, "args": args})
}

// Get reads a property.
func (c *Client) Get(object, property string) (json.RawMessage, error) {
	return c.Request("get", map[string]any{"object": object, "property": property})
}

// Set writes a writable property.
func (c *Client) Set(object, property string, value any) (json.RawMessage, error) {
	return c.Request("set", map[string]any{"object": object, "property": property, "value": value})
}

// Subscribe follows properties (or paramStore ids) and signals.
func (c *Client) Subscribe(object string, properties, signals []string, throttleMs int) error {
	args := map[string]any{"object": object}
	if properties != nil {
		args["properties"] = properties
	}
	if signals != nil {
		args["signals"] = signals
	}
	if throttleMs > 0 {
		args["throttleMs"] = throttleMs
	}
	_, err := c.Request("subscribe", args)
	return err
}

// Hello checks the protocol version and returns the app version.
func (c *Client) Hello() (string, error) {
	raw, err := c.Request("hello", nil)
	if err != nil {
		return "", err
	}
	var h struct {
		ProtocolVersion int `json:"protocolVersion"`
		App             struct {
			Version string `json:"version"`
		} `json:"app"`
	}
	if err := json.Unmarshal(raw, &h); err != nil {
		return "", err
	}
	if h.ProtocolVersion != ProtocolVersion {
		return "", fmt.Errorf("the app speaks protocol %d, sunlight %d", h.ProtocolVersion, ProtocolVersion)
	}
	return h.App.Version, nil
}
