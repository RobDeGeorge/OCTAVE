// Package octave is a client for OCTAVE's service API (docs/api/) over the
// line protocol: a TCP connection carrying one JSON object per line, and
// media records framed with 0xFF + length. Standard library only.
//
// Everything that is not connection plumbing is generated from docs/api:
// protocol_gen.go from protocol.json (the ops, their arguments and replies,
// the events, the error codes, the record constants) and objects_gen.go
// from the object files (one Go type per service object with a method per
// API method and Get/Set per property). This file is the hand-written part.
//
//	c, err := octave.Dial("127.0.0.1:9876")
//	vol := octave.NewVolumeController(c)
//	err = vol.ApplyVolume(40)
//	percent, err := vol.GetPercent()
//	sub, _ := c.Subscribe(octave.SubscribeArgs{Object: "nowPlaying", Properties: []string{"title"}})
//	for ev := range c.Events { var p octave.PropertyEvent; if ev.Decode(&p) == nil && p.Sub == sub.Sub { ... } }
package octave

import (
	"bufio"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"sync"
	"time"
)

// ProtocolVersions are the versions this client can talk to: 1 (no pairing)
// and 2, which only adds to it.
var ProtocolVersions = []int{1, 2}

// Error is a failure reply: Message is the server's `error`, Code its stable
// code when it has one (CodeUnauthorised), else "".
type Error struct {
	Op      string
	Message string
	Code    string
}

func (e *Error) Error() string {
	if e.Code != "" {
		return fmt.Sprintf("%s: %s (%s)", e.Op, e.Message, e.Code)
	}
	return fmt.Sprintf("%s: %s", e.Op, e.Message)
}

// ErrClosed is returned once the connection is gone.
var ErrClosed = errors.New("octave: connection closed")

// Event is a message with an `event` key, as the server sent it. Kind is the
// event name (EventProperty, ...); Decode fills the matching generated
// struct (PropertyEvent, ParamsEvent, ModelEvent, SignalEvent, ...).
type Event struct {
	Kind string
	Raw  json.RawMessage
}

// Decode unmarshals the event into v.
func (e Event) Decode(v any) error { return json.Unmarshal(e.Raw, v) }

// Record is one media stream record (streams in docs/api/protocol.json).
type Record struct {
	Stream  uint8
	Flags   uint8
	PTS     uint64 // microseconds
	Payload []byte
}

func (r Record) Key() bool           { return r.Flags&FlagKey != 0 }
func (r Record) Config() bool        { return r.Flags&FlagConfig != 0 }
func (r Record) JPEG() bool          { return r.Flags&FlagJpeg != 0 }
func (r Record) Discontinuity() bool { return r.Flags&FlagDiscontinuity != 0 }

// ParseRecord reads a record (header and payload, without the TCP framing).
func ParseRecord(b []byte) (Record, error) {
	if len(b) < RecordHeader {
		return Record{}, fmt.Errorf("octave: record of %d bytes is shorter than its header", len(b))
	}
	if b[0] != 1 {
		return Record{}, fmt.Errorf("octave: record version %d, want 1", b[0])
	}
	return Record{Stream: b[1], Flags: b[2], PTS: binary.BigEndian.Uint64(b[4:12]), Payload: b[RecordHeader:]}, nil
}

// Option configures Dial.
type Option func(*Client)

// WithToken sends a paired device's token with hello (protocol 2).
func WithToken(token string) Option { return func(c *Client) { c.token = token } }

// WithTimeout bounds every request (default 30 s).
func WithTimeout(d time.Duration) Option { return func(c *Client) { c.timeout = d } }

// WithoutVersionCheck connects even to a protocol version this client does
// not know.
func WithoutVersionCheck() Option { return func(c *Client) { c.anyVersion = true } }

// Client is one connection. Requests may come from any goroutine; replies
// are matched by id, so events (on Events) and records (on Records) can
// arrive in between. Read Events and Records, or they fill up and the
// reader blocks.
type Client struct {
	conn       net.Conn
	timeout    time.Duration
	token      string
	anyVersion bool

	// Events carries every subscription and stream event; Records every
	// media record. Both close when the connection does.
	Events  chan Event
	Records chan Record
	// Hello is the server's hello reply, from Dial.
	Hello HelloReply

	mu      sync.Mutex
	writeMu sync.Mutex
	nextID  int
	pending map[int]chan rawReply
	closed  bool
}

type rawReply struct {
	ID     json.RawMessage `json:"id"`
	OK     bool            `json:"ok"`
	Result json.RawMessage `json:"result"`
	Error  string          `json:"error"`
	Code   string          `json:"code"`
	Event  string          `json:"event"`
}

// Dial connects to addr ("127.0.0.1:9876"), says hello, and checks the
// protocol version unless WithoutVersionCheck.
func Dial(addr string, opts ...Option) (*Client, error) {
	c := &Client{timeout: 30 * time.Second, Events: make(chan Event, 256), Records: make(chan Record, 64), pending: map[int]chan rawReply{}}
	for _, o := range opts {
		o(c)
	}
	conn, err := net.DialTimeout("tcp", addr, c.timeout)
	if err != nil {
		return nil, err
	}
	c.conn = conn
	go c.read()
	args := HelloArgs{}
	if c.token != "" {
		args.Token = c.token
	}
	if err := c.Request("hello", args, &c.Hello); err != nil {
		c.Close()
		return nil, err
	}
	if !c.anyVersion {
		known := false
		for _, v := range ProtocolVersions {
			known = known || v == c.Hello.ProtocolVersion
		}
		if !known {
			c.Close()
			return nil, fmt.Errorf("octave: the app speaks protocol %d, this client knows %v (docs/api/README.md)", c.Hello.ProtocolVersion, ProtocolVersions)
		}
	}
	return c, nil
}

// Trusted says whether ops will be accepted (protocol 1: always; protocol
// 2: on this device with loopback open, or with a valid token). If not,
// Pair.
func (c *Client) Trusted() bool { return c.Hello.Auth == "" || c.Hello.Auth == "trusted" }

// Close ends the connection; Events and Records close, pending requests fail.
func (c *Client) Close() error {
	c.mu.Lock()
	if c.closed {
		c.mu.Unlock()
		return nil
	}
	c.closed = true
	c.mu.Unlock()
	return c.conn.Close()
}

// Request sends any op with args (a generated <Op>Args, or a map) and
// unmarshals the result into out (nil to ignore it). A failure reply is a
// *Error.
func (c *Client) Request(op string, args any, out any) error {
	body := map[string]any{}
	if args != nil {
		b, err := json.Marshal(args)
		if err != nil {
			return err
		}
		if err := json.Unmarshal(b, &body); err != nil {
			return fmt.Errorf("octave: %s args must be an object: %w", op, err)
		}
	}
	c.mu.Lock()
	if c.closed {
		c.mu.Unlock()
		return ErrClosed
	}
	c.nextID++
	id := c.nextID
	ch := make(chan rawReply, 1)
	c.pending[id] = ch
	c.mu.Unlock()
	body["op"] = op
	body["id"] = id
	line, err := json.Marshal(body)
	if err != nil {
		return err
	}
	c.writeMu.Lock()
	_, err = c.conn.Write(append(line, '\n'))
	c.writeMu.Unlock()
	if err != nil {
		c.mu.Lock()
		delete(c.pending, id)
		c.mu.Unlock()
		return err
	}
	select {
	case r, ok := <-ch:
		if !ok {
			return ErrClosed
		}
		if !r.OK {
			return &Error{Op: op, Message: r.Error, Code: r.Code}
		}
		if out == nil || len(r.Result) == 0 || string(r.Result) == "null" {
			return nil
		}
		return json.Unmarshal(r.Result, out)
	case <-time.After(c.timeout):
		c.mu.Lock()
		delete(c.pending, id)
		c.mu.Unlock()
		return fmt.Errorf("octave: %s: no reply within %s", op, c.timeout)
	}
}

// ── the ops, typed ────────────────────────────────────────────────────────

// Describe is one object's properties, methods and signals.
func (c *Client) Describe(object string) (ObjectDescription, error) {
	var d ObjectDescription
	err := c.Request("describe", DescribeArgs{Object: object}, &d)
	return d, err
}

// DescribeAll is every object's description plus the ops this server has.
func (c *Client) DescribeAll() (DescribeAll, error) {
	var d DescribeAll
	err := c.Request("describe", DescribeArgs{}, &d)
	return d, err
}

// DescribeAll is the reply to describe with no object.
type DescribeAll struct {
	ProtocolVersion int                          `json:"protocolVersion"`
	Objects         map[string]ObjectDescription `json:"objects"`
	Ops             []string                     `json:"ops"`
}

// Get reads a property into out (on paramStore, a parameter id; "" for all).
func (c *Client) Get(object, property string, out any) error {
	return c.Request("get", GetArgs{Object: object, Property: property}, out)
}

// Set writes a writable property; the value read back goes into out.
func (c *Client) Set(object, property string, value any, out any) error {
	return c.Request("set", CallValue{Object: object, Property: property, Value: value}, out)
}

// CallValue is SetArgs with any value (SetArgs's Value is the Json the schema allows).
type CallValue struct {
	Object   string `json:"object"`
	Property string `json:"property"`
	Value    any    `json:"value"`
}

// Call invokes a method with positional args; the return value goes into out.
func (c *Client) Call(object, method string, out any, args ...any) error {
	if args == nil {
		args = []any{}
	}
	return c.Request("call", map[string]any{"object": object, "method": method, "args": args}, out)
}

// Rows reads a list-model property's rows; count < 0 means all from `from`.
func (c *Client) Rows(object, property string, from, count int) ([]Row, error) {
	args := map[string]any{"object": object, "property": property, "from": from}
	if count >= 0 {
		args["count"] = count
	}
	var rows []Row
	err := c.Request("rows", args, &rows)
	return rows, err
}

// Subscribe follows properties, signals or paramStore ids; events arrive on
// Events with the returned Sub id.
func (c *Client) Subscribe(args SubscribeArgs) (SubscribeReply, error) {
	var r SubscribeReply
	err := c.Request("subscribe", args, &r)
	return r, err
}

// Unsubscribe ends a subscription.
func (c *Client) Unsubscribe(sub int) error {
	return c.Request("unsubscribe", UnsubscribeArgs{Sub: sub}, nil)
}

// Stream opens a media stream ("phoneMirrorManager", "video"); records
// arrive on Records with the returned Stream id.
func (c *Client) Stream(object, name string) (StreamInfo, error) {
	var r StreamInfo
	err := c.Request("stream", StreamArgs{Object: object, Name: name}, &r)
	return r, err
}

// Unstream stops a stream.
func (c *Client) Unstream(stream int) error {
	return c.Request("unstream", UnstreamArgs{Stream: stream}, nil)
}

// Pair becomes a paired device with the code the head unit shows and
// returns the token to keep (WithToken next time).
func (c *Client) Pair(code, name string) (string, error) {
	var r PairReply
	if err := c.Request("pair", PairArgs{Code: code, Name: name}, &r); err != nil {
		return "", err
	}
	c.Hello.Auth = "trusted"
	return r.Token, nil
}

// Unpair forgets this connection's own device.
func (c *Client) Unpair() error { return c.Request("unpair", nil, nil) }

// MediaTicket is a short-lived ticket for /art and /export URLs.
func (c *Client) MediaTicket() (MediaTicketReply, error) {
	var r MediaTicketReply
	err := c.Request("mediaTicket", nil, &r)
	return r, err
}

// ── plumbing ──────────────────────────────────────────────────────────────

func (c *Client) read() {
	defer c.finish()
	r := bufio.NewReaderSize(c.conn, 1<<16)
	for {
		first, err := r.Peek(1)
		if err != nil {
			return
		}
		if first[0] == 0xFF {
			// A stream record: 0xFF, big-endian u32 length, the record
			var head [5]byte
			if _, err := io.ReadFull(r, head[:]); err != nil {
				return
			}
			n := binary.BigEndian.Uint32(head[1:])
			if n > 64<<20 {
				return
			}
			buf := make([]byte, n)
			if _, err := io.ReadFull(r, buf); err != nil {
				return
			}
			if rec, err := ParseRecord(buf); err == nil {
				c.Records <- rec
			}
			continue
		}
		line, err := r.ReadBytes('\n')
		if err != nil {
			return
		}
		var reply rawReply
		if err := json.Unmarshal(line, &reply); err != nil {
			return // not the protocol
		}
		if reply.Event != "" {
			c.Events <- Event{Kind: reply.Event, Raw: json.RawMessage(append([]byte(nil), line...))}
			continue
		}
		var id int
		if json.Unmarshal(reply.ID, &id) != nil {
			continue
		}
		c.mu.Lock()
		ch := c.pending[id]
		delete(c.pending, id)
		c.mu.Unlock()
		if ch != nil {
			ch <- reply
		}
	}
}

func (c *Client) finish() {
	c.mu.Lock()
	c.closed = true
	pending := c.pending
	c.pending = map[int]chan rawReply{}
	c.mu.Unlock()
	c.conn.Close()
	for _, ch := range pending {
		close(ch)
	}
	close(c.Events)
	close(c.Records)
}
