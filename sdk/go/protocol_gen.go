// Code generated from docs/api/protocol.json by internal/gen; DO NOT EDIT.
// Protocol version 3. Regenerate with `go generate ./...`.

package octave

import "encoding/json"

// ProtocolVersion is the version protocol.json describes (docs/api/README.md, Versioning).
const ProtocolVersion = 3

// Annotated is $defs/annotated of protocol.json.
type Annotated struct {
	// Still works; what to use instead and when it goes. Kept at least one protocol version.
	Deprecated string `json:"deprecated,omitempty"`
	// Not covered by the version: may change or go in any release. Absent = stable.
	// One of: experimental
	Stability string `json:"stability,omitempty"`
}

// ErrorReply is $defs/errorReply of protocol.json.
type ErrorReply struct {
	// One of: unauthorised, forbidden, rateLimited
	Code  string `json:"code,omitempty"`
	Error string `json:"error"`
	Id    any    `json:"id,omitempty"`
	Ok    bool   `json:"ok"`
	// With rateLimited: wait this long before sending again.
	RetryAfterMs *int `json:"retryAfterMs,omitempty"`
}

// MethodDescription is $defs/methodDescription of protocol.json.
type MethodDescription struct {
	// Still works; what to use instead and when it goes. Kept at least one protocol version.
	Deprecated string `json:"deprecated,omitempty"`
	// One of: slot, invokable
	Kind   string             `json:"kind"`
	Name   string             `json:"name"`
	Params []ParamDescription `json:"params"`
	// The Qt return type; "" for void.
	Returns string `json:"returns"`
	// What `call` needs.
	Scope string `json:"scope"`
	// Not covered by the version: may change or go in any release. Absent = stable.
	// One of: experimental
	Stability string `json:"stability,omitempty"`
}

// ModelSummary: What `get` returns for a property holding a list model.
type ModelSummary struct {
	Count int      `json:"count"`
	Model bool     `json:"model"`
	Roles []string `json:"roles"`
}

// ObjectDescription: One service object, as `describe` answers and as
// docs/api/<object>.json holds (the file adds `object`, its name).
type ObjectDescription struct {
	Class      string                `json:"class"`
	Enums      map[string][]string   `json:"enums,omitempty"`
	Methods    []MethodDescription   `json:"methods"`
	Object     string                `json:"object,omitempty"`
	Properties []PropertyDescription `json:"properties"`
	Signals    []SignalDescription   `json:"signals"`
	// One of: experimental
	Stability string `json:"stability,omitempty"`
}

// OkReply is $defs/okReply of protocol.json.
type OkReply struct {
	Id     any  `json:"id,omitempty"`
	Ok     bool `json:"ok"`
	Result any  `json:"result"`
}

// ParamDescription is $defs/paramDescription of protocol.json.
type ParamDescription struct {
	Name string `json:"name"`
	// The Qt type name (QString, int, bool, double, QStringList, QVariantMap, QVariantList, QVariant, an enum...).
	Type string `json:"type"`
}

// PropertyDescription is $defs/propertyDescription of protocol.json.
type PropertyDescription struct {
	Constant bool `json:"constant"`
	// Still works; what to use instead and when it goes. Kept at least one protocol version.
	Deprecated string `json:"deprecated,omitempty"`
	// The keys, for an enum property (`set` takes a key or a number).
	Enum []string `json:"enum,omitempty"`
	Name string   `json:"name"`
	// The signal that fires on change.
	Notify   string `json:"notify,omitempty"`
	Readable bool   `json:"readable"`
	// Writable properties only: what `set` needs.
	Scope string `json:"scope,omitempty"`
	// Not covered by the version: may change or go in any release. Absent = stable.
	// One of: experimental
	Stability string `json:"stability,omitempty"`
	Type      string `json:"type"`
	Writable  bool   `json:"writable"`
}

// Row: One list-model row: {role name: value}.
type Row = map[string]any

// SignalDescription is $defs/signalDescription of protocol.json.
type SignalDescription struct {
	// Still works; what to use instead and when it goes. Kept at least one protocol version.
	Deprecated string             `json:"deprecated,omitempty"`
	Name       string             `json:"name"`
	Params     []ParamDescription `json:"params"`
	// Not covered by the version: may change or go in any release. Absent = stable.
	// One of: experimental
	Stability string `json:"stability,omitempty"`
}

// StreamInfo: The `stream` reply. `format`, `codec`, `width` and `height` are
// present once known: a JPEG stream knows its format from the start, an H.264
// stream only after its first parameter sets (a `streamConfig` event then
// carries them all).
type StreamInfo struct {
	// RFC 6381, from the SPS (H.264 only).
	Codec string `json:"codec,omitempty"`
	// One of: annexb, jpeg
	Format string `json:"format,omitempty"`
	Height *int   `json:"height,omitempty"`
	Stream int    `json:"stream"`
	Width  *int   `json:"width,omitempty"`
}

// Ops is every op the protocol has, in the spec's order. The server's own list is DescribeAll().Ops.
var Ops = []string{"hello", "describe", "get", "set", "call", "rows", "subscribe", "unsubscribe", "stream", "unstream", "pair", "unpair", "mediaTicket"}

// OpenOps are the ops an untrusted connection may use (protocol 2).
var OpenOps = []string{"hello", "pair"}

// HelloArgs are the arguments of the "hello" op. Who you are talking to. The
// first request on a connection. With a paired device's `token` the connection
// becomes trusted (protocol 2); an unknown or revoked token is not an error:
// `auth` is untrusted and `tokenRejected` is true, so the client shows its
// pairing screen. An untrusted connection learns only `protocolVersion`,
// `auth` and `loopbackOpen`.
//
// Since protocol 1.
type HelloArgs struct {
	// A paired device's token from `pair` (64 hex characters). Protocol 2.
	Token string `json:"token,omitempty"`
}

// HelloReply is the reply to "hello".
type HelloReply struct {
	// Trusted connections only.
	App struct {
		// One of: debug, release
		Build string `json:"build"`
		// True when OCTAVE runs with no QML UI (`--headless`): your frontend is the screen.
		Headless bool   `json:"headless"`
		Version  string `json:"version"`
	} `json:"app,omitempty"`
	// Whether this connection may use the service. Protocol 2 (absent in 1: everything was trusted).
	// One of: trusted, untrusted
	Auth string `json:"auth"`
	// Whether a connection from this device is trusted without pairing (the setting serviceLoopbackOpen). Protocol 2.
	LoopbackOpen bool `json:"loopbackOpen"`
	// Every service object's name. Trusted connections only.
	Objects []string `json:"objects,omitempty"`
	// Check it against the versions you know.
	ProtocolVersion int `json:"protocolVersion"`
	// Trusted connections only: what this connection may change (`full` on the device itself).
	Scopes []string `json:"scopes,omitempty"`
	// Present (true) when the `token` sent was unknown or revoked; the connection keeps no token.
	TokenRejected bool `json:"tokenRejected,omitempty"`
}

// DescribeArgs are the arguments of the "describe" op. What an object offers:
// its properties (type, readable / writable / constant, notify signal, enum
// keys), methods (parameters, return type, slot or invokable) and signals,
// with `stability` / `deprecated` annotations, exactly as
// docs/api/<object>.json. Without `object`, every object's description plus
// the op names this server has (`ops`, additive, since protocol 2).
//
// Since protocol 1.
type DescribeArgs struct {
	// One object's name; omit for all of them.
	Object string `json:"object,omitempty"`
}

// DescribeReply is the reply to "describe".
type DescribeReply = json.RawMessage

// GetArgs are the arguments of the "get" op. A property's current value. On a
// key-value object (paramStore) `property` is a key (a parameter id) and the
// reply its value, or null before a reading; without `property` every key that
// has a value, as {id: value}. A property holding a list model answers a
// summary ({model: true, count, roles}); read the rows with `rows`.
//
// Since protocol 1.
type GetArgs struct {
	Object string `json:"object"`
	// Required except on paramStore.
	Property  string `json:"property,omitempty"`
	TimeoutMs *int   `json:"timeoutMs,omitempty"`
}

// GetReply is the reply to "get". The value as JSON (a Qt type converted:
// strings, numbers, booleans, lists, maps; an enum as its integer; a QUrl /
// QColor / QDateTime as a string), or a model summary.
type GetReply = json.RawMessage

// SetArgs are the arguments of the "set" op. Write a writable property. The
// value is converted to the property's type (an enum by key or number); a
// value that does not convert is an error, as is a read-only property. Most
// state changes through a method instead (settings through their save_*
// slots). Replies with the value read back.
//
// Since protocol 1.
type SetArgs struct {
	Object    string `json:"object"`
	Property  string `json:"property"`
	TimeoutMs *int   `json:"timeoutMs,omitempty"`
	// Any JSON value the property's type accepts.
	Value any `json:"value"`
}

// SetReply is the reply to "set". The property's value after the write.
type SetReply = any

// CallArgs are the arguments of the "call" op. Call a public slot or
// invokable. Arguments are converted to the declared parameter types; an
// overload is chosen by argument count. The reply is the return value (null
// for void). A method that takes (consumer, value) and is registered for
// release on close (obdManager.setParameterDemand,
// obdManager.setBackgroundDemand, berryIMU.setDemand, audioAnalyzer.setDemand)
// is called again with its release value ([] or false) for every consumer this
// connection still holds when it closes.
//
// Since protocol 1.
type CallArgs struct {
	// Positional arguments; default none.
	Args      []any  `json:"args,omitempty"`
	Method    string `json:"method"`
	Object    string `json:"object"`
	TimeoutMs *int   `json:"timeoutMs,omitempty"`
}

// CallReply is the reply to "call". The method's return value as JSON, or
// null.
type CallReply = any

// RowsArgs are the arguments of the "rows" op. The rows of a list-model
// property, each as {role: value}. Without `count`, every row from `from` on.
//
// Since protocol 1.
type RowsArgs struct {
	Count     *int   `json:"count,omitempty"`
	From      *int   `json:"from,omitempty"`
	Object    string `json:"object"`
	Property  string `json:"property"`
	TimeoutMs *int   `json:"timeoutMs,omitempty"`
}

// RowsReply is the reply to "rows".
type RowsReply = []Row

// SubscribeArgs are the arguments of the "subscribe" op. Follow an object:
// `property` events for each named property (the current value at once, then
// on every notify; a list-model property sends `model` events instead,
// starting with a reset of every row), `signal` events for each named signal,
// and on paramStore `params` events for the ids in `properties` (none = all).
// The reply, with your `sub` id, always arrives before the first event of that
// subscription. `throttleMs` > 0 coalesces: at most one event per name per
// interval, the latest value winning (model events are never coalesced). At
// least one property or signal is required except on paramStore.
//
// Since protocol 1.
type SubscribeArgs struct {
	Object string `json:"object"`
	// Property names, or parameter ids on paramStore.
	Properties []string `json:"properties,omitempty"`
	Signals    []string `json:"signals,omitempty"`
	ThrottleMs *int     `json:"throttleMs,omitempty"`
}

// SubscribeReply is the reply to "subscribe".
type SubscribeReply struct {
	// The subscription id carried by its events; unique per connection.
	Sub int `json:"sub"`
}

// UnsubscribeArgs are the arguments of the "unsubscribe" op. End a
// subscription. Unknown ids are an error. A connection's subscriptions end
// with it.
//
// Since protocol 1.
type UnsubscribeArgs struct {
	Sub int `json:"sub"`
}

// UnsubscribeReply is the reply to "unsubscribe".
type UnsubscribeReply = bool

// StreamArgs are the arguments of the "stream" op. Open a media stream (see
// streams): binary records follow on this connection. The reply gives the
// stream id in the records and what is known of the format so far (an H.264
// stream with no session yet knows only its id); a `streamConfig` event
// follows every change. A new subscriber receives nothing until the next
// keyframe. At most 255 streams per connection; a stream ends with `unstream`
// or the connection.
//
// Since protocol 2.
type StreamArgs struct {
	// The stream's name on it (video, frames).
	Name string `json:"name"`
	// The object that owns the stream (phoneMirrorManager, androidAutoManager).
	Object string `json:"object"`
}

// StreamReply is the reply to "stream".
type StreamReply = StreamInfo

// UnstreamArgs are the arguments of the "unstream" op. Stop one stream by the
// id `stream` returned. Not an error for an unknown id.
//
// Since protocol 2.
type UnstreamArgs struct {
	Stream int `json:"stream"`
}

// UnstreamReply is the reply to "unstream".
type UnstreamReply = bool

// PairArgs are the arguments of the "pair" op. Become a paired device with the
// 6-digit code OCTAVE shows (Settings > Service > Pair a device, valid 2
// minutes, single use). The reply is the device's token; store it and send it
// with `hello` on later connections. This connection is trusted at once. A
// refusal (no code showing, wrong code, too many attempts, attempts faster
// than one a second, an address with 10 wrong codes) is a message-only error.
//
// Since protocol 2.
type PairArgs struct {
	Code string `json:"code"`
	// What the user sees in the device list ("Kitchen tablet").
	Name string `json:"name"`
	// The permissions this device asks for; absent or empty: full. An unknown one fails `pair` without using the code.
	Scopes []string `json:"scopes,omitempty"`
}

// PairReply is the reply to "pair".
type PairReply struct {
	// What was granted: `read` plus what was asked for, or `full`.
	Scopes []string `json:"scopes"`
	Token  string   `json:"token"`
}

// UnpairArgs are the arguments of the "unpair" op. Forget the caller's own
// device: its token stops working everywhere. An error when this connection
// presented no token (a loopback connection trusted by serviceLoopbackOpen has
// no device to forget).
//
// Since protocol 2.
type UnpairArgs map[string]any

// UnpairReply is the reply to "unpair".
type UnpairReply = bool

// MediaTicketArgs are the arguments of the "mediaTicket" op. A short-lived
// ticket for the HTTP routes (`/art?ticket=...`, `/export/...?ticket=...`), so
// no token goes in a URL. Bound to the caller's token (revoked with it); a
// loopback connection gets one too. At most 256 exist at once (the oldest
// goes).
//
// Since protocol 2.
type MediaTicketArgs map[string]any

// MediaTicketReply is the reply to "mediaTicket".
type MediaTicketReply struct {
	// Seconds the ticket is valid (600).
	ExpiresIn int    `json:"expiresIn"`
	Ticket    string `json:"ticket"`
}

// Error codes a failure reply can carry (Error.Code); every other failure is a message only.
const (
	// The connection is trusted but its paired device lacks the permission
	// scope this `set` / `call` / phone stream / dev action needs
	// (docs/api/README.md, Permissions). The message names the member, the
	// scope it needs and the scopes the device has. HTTP routes answer 401 for
	// the same reason (`/export/...` needs `files`).
	CodeForbidden = "forbidden"
	// The connection sent more ops than its limit (docs/api/README.md, Limits
	// per connection: 200 a second, 400 at once by default; dev actions are
	// not counted). The op did not run. `retryAfterMs` says when the next one
	// would pass; a client that keeps sending regardless (more than 4000
	// refusals in a second) has its connection closed.
	CodeRateLimited = "rateLimited"
	// The connection is not trusted (protocol 2, pairing): it is not on this
	// device with serviceLoopbackOpen on, and it has presented no valid token.
	// Every op but `hello` and `pair` answers this until the client pairs or
	// sends a valid token with `hello`. HTTP routes answer 401 for the same
	// reason.
	CodeUnauthorised = "unauthorised"
)

// Event names (Event.Kind).
const (
	EventDropped       = "dropped"
	EventModel         = "model"
	EventParams        = "params"
	EventProperty      = "property"
	EventSignal        = "signal"
	EventStreamConfig  = "streamConfig"
	EventStreamDropped = "streamDropped"
)

// DroppedEvent is the "dropped" event. The client stopped reading and the
// oldest `count` JSON events were discarded (the queue holds 1000). Replies
// are never dropped. Decode one with Event.Decode.
type DroppedEvent struct {
	Count int `json:"count"`
}

// ModelEvent is the "model" event. A subscribed list-model property changed.
// Replay in order: `reset` replaces every row (sent first, with all rows),
// `insert` adds rows at first..last, `remove` deletes first..last, `update`
// replaces rows first..last. `rows` is present for reset, insert and update.
// Never coalesced by throttleMs. Decode one with Event.Decode.
type ModelEvent struct {
	// One of: reset, insert, remove, update
	Change string `json:"change"`
	First  int    `json:"first"`
	Last   int    `json:"last"`
	Name   string `json:"name"`
	Object string `json:"object"`
	Rows   []Row  `json:"rows,omitempty"`
	Sub    int    `json:"sub"`
	T      int    `json:"t"`
}

// ParamsEvent is the "params" event. paramStore: the ids that changed in one
// batch (33 ms coalescing in the store; merged further by throttleMs), in
// native units; at once on subscribing with every id that has a value. A value
// is a number, or null when the reading is gone. Decode one with Event.Decode.
type ParamsEvent struct {
	Object string              `json:"object"`
	Sub    int                 `json:"sub"`
	T      int                 `json:"t"`
	Values map[string]*float64 `json:"values"`
}

// PropertyEvent is the "property" event. A subscribed property's value: at
// once on subscribing, then on every notify (coalesced by throttleMs). Decode
// one with Event.Decode.
type PropertyEvent struct {
	Name   string `json:"name"`
	Object string `json:"object"`
	Sub    int    `json:"sub"`
	T      int    `json:"t"`
	// The value as JSON.
	Value any `json:"value"`
}

// SignalEvent is the "signal" event. A subscribed signal fired; `args` are its
// arguments in order. Decode one with Event.Decode.
type SignalEvent struct {
	Args   []any  `json:"args"`
	Name   string `json:"name"`
	Object string `json:"object"`
	Sub    int    `json:"sub"`
	T      int    `json:"t"`
}

// StreamConfigEvent is the "streamConfig" event. A stream's parameter sets
// changed (the phone rotated, a new session): the codec and picture size of
// the records that follow. Reconfigure the decoder. Decode one with
// Event.Decode.
type StreamConfigEvent struct {
	// RFC 6381, e.g. avc1.42c01e (from the SPS).
	Codec string `json:"codec,omitempty"`
	// One of: annexb, jpeg
	Format string `json:"format"`
	Height *int   `json:"height,omitempty"`
	Stream int    `json:"stream"`
	Width  *int   `json:"width,omitempty"`
}

// StreamDroppedEvent is the "streamDropped" event. This stream's queued
// records overflowed (2 MB or 90 records) and were all discarded; `count` is
// the number of such drops so far. The stream resumes at the next keyframe,
// whose record has the discontinuity flag. Decode one with Event.Decode.
type StreamDroppedEvent struct {
	Count  int `json:"count"`
	Stream int `json:"stream"`
}

// Stream record flags (Record.Flags).
const (
	// SPS / PPS are inline before the IDR.
	FlagConfig = 2
	// The first record after joining or after a drop.
	FlagDiscontinuity = 8
	// The payload is a JPEG.
	FlagJpeg = 4
	// A keyframe (every JPEG record).
	FlagKey = 1
)

// RecordHeader is the number of bytes before a record's payload.
const RecordHeader = 12

// Streams maps every stream ("object.name") to its format.
var Streams = map[string]string{"androidAutoManager.frames": "jpeg", "phoneMirrorManager.video": "annexb"}

// Routes are the HTTP routes (GET, trusted: a media ticket or a bearer token).
var Routes = []string{"/art", "/export/dashboard", "/export/logs", "/export/trip"}

var _ json.RawMessage
