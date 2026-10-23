// Generated from docs/api/protocol.json by scripts/generate-protocol.mjs; do not edit.
// Protocol version 2. Regenerate with `npm run generate`.

export type Json = null | boolean | number | string | Json[] | { [k: string]: Json }

/** The protocol version protocol.json describes (docs/api/README.md, Versioning). */
export const PROTOCOL_VERSION = 2

/** One list-model row: {role name: value}. */
export type Row = Record<string, Json>

export interface OkReply {
  id?: Json
  ok: true
  result: Json
}

export interface ErrorReply {
  id?: Json
  ok: false
  error: string
  code?: "unauthorised" | "forbidden"
}

/** What `get` returns for a property holding a list model. */
export interface ModelSummary {
  model: true
  count: number
  roles: string[]
}

/** The `stream` reply. `format`, `codec`, `width` and `height` are present once known: a JPEG stream knows its format from the start, an H.264 stream only after its first parameter sets (a `streamConfig` event then carries them all). */
export interface StreamInfo {
  stream: number
  format?: "annexb" | "jpeg"
  /** RFC 6381, from the SPS (H.264 only). */
  codec?: string
  width?: number
  height?: number
}

export interface Annotated {
  /** Not covered by the version: may change or go in any release. Absent = stable. */
  stability?: "experimental"
  /** Still works; what to use instead and when it goes. Kept at least one protocol version. */
  deprecated?: string
}

export interface ParamDescription {
  name: string
  /** The Qt type name (QString, int, bool, double, QStringList, QVariantMap, QVariantList, QVariant, an enum...). */
  type: string
}

export type PropertyDescription = Annotated & {
  name: string
  type: string
  readable: boolean
  writable: boolean
  constant: boolean
  /** The signal that fires on change. */
  notify?: string
  /** The keys, for an enum property (`set` takes a key or a number). */
  enum?: string[]
  /** Writable properties only: what `set` needs. */
  scope?: "read" | "media" | "vehicle" | "phone" | "settings" | "files" | "system" | "full"
}

export type MethodDescription = Annotated & {
  name: string
  kind: "slot" | "invokable"
  params: ParamDescription[]
  /** The Qt return type; "" for void. */
  returns: string
  /** What `call` needs. */
  scope: "read" | "media" | "vehicle" | "phone" | "settings" | "files" | "system" | "full"
}

export type SignalDescription = Annotated & {
  name: string
  params: ParamDescription[]
}

/** One service object, as `describe` answers and as docs/api/<object>.json holds (the file adds `object`, its name). */
export interface ObjectDescription {
  object?: string
  class: string
  stability?: "experimental"
  properties: PropertyDescription[]
  methods: MethodDescription[]
  signals: SignalDescription[]
  enums?: Record<string, string[]>
}

/** Every op the protocol has. The server's own list is `describe` (no object) → `ops`. */
export const OPS = ["hello","describe","get","set","call","rows","subscribe","unsubscribe","stream","unstream","pair","unpair","mediaTicket"] as const
export type Op = typeof OPS[number]
/** The ops an untrusted connection may use (protocol 2). */
export const OPEN_OPS = ["hello","pair"] as const

/**
 * Who you are talking to. The first request on a connection. With a paired device's `token` the connection becomes trusted (protocol 2); an unknown or revoked token is not an error: `auth` is untrusted and `tokenRejected` is true, so the client shows its pairing screen. An untrusted connection learns only `protocolVersion`, `auth` and `loopbackOpen`.
 * @since protocol 1
 */
export interface HelloArgs {
  /** A paired device's token from `pair` (64 hex characters). Protocol 2. */
  token?: string
}
export interface HelloReply {
  /** Check it against the versions you know. */
  protocolVersion: number
  /** Whether this connection may use the service. Protocol 2 (absent in 1: everything was trusted). */
  auth: "trusted" | "untrusted"
  /** Whether a connection from this device is trusted without pairing (the setting serviceLoopbackOpen). Protocol 2. */
  loopbackOpen: boolean
  /** Present (true) when the `token` sent was unknown or revoked; the connection keeps no token. */
  tokenRejected?: true
  /** Trusted connections only: what this connection may change (`full` on the device itself). */
  scopes?: ("read" | "media" | "vehicle" | "phone" | "settings" | "files" | "system" | "full")[]
  /** Trusted connections only. */
  app?: {
    version: string
    build: "debug" | "release"
    /** True when OCTAVE runs with no QML UI (`--headless`): your frontend is the screen. */
    headless: boolean
  }
  /** Every service object's name. Trusted connections only. */
  objects?: string[]
}

/**
 * What an object offers: its properties (type, readable / writable / constant, notify signal, enum keys), methods (parameters, return type, slot or invokable) and signals, with `stability` / `deprecated` annotations, exactly as docs/api/<object>.json. Without `object`, every object's description plus the op names this server has (`ops`, additive, since protocol 2).
 * @since protocol 1
 */
export interface DescribeArgs {
  /** One object's name; omit for all of them. */
  object?: string
}
export type DescribeReply = ObjectDescription | {
  protocolVersion: number
  objects: Record<string, ObjectDescription>
  /** The op names this server answers. Everything a client can send; compare with this file's `ops`. */
  ops: string[]
}

/**
 * A property's current value. On a key-value object (paramStore) `property` is a key (a parameter id) and the reply its value, or null before a reading; without `property` every key that has a value, as {id: value}. A property holding a list model answers a summary ({model: true, count, roles}); read the rows with `rows`.
 * @since protocol 1
 */
export interface GetArgs {
  object: string
  /** Required except on paramStore. */
  property?: string
  timeoutMs?: number
}
/** The value as JSON (a Qt type converted: strings, numbers, booleans, lists, maps; an enum as its integer; a QUrl / QColor / QDateTime as a string), or a model summary. */
export type GetReply = ModelSummary | Json

/**
 * Write a writable property. The value is converted to the property's type (an enum by key or number); a value that does not convert is an error, as is a read-only property. Most state changes through a method instead (settings through their save_* slots). Replies with the value read back.
 * @since protocol 1
 */
export interface SetArgs {
  object: string
  property: string
  /** Any JSON value the property's type accepts. */
  value: Json
  timeoutMs?: number
}
/** The property's value after the write. */
export type SetReply = Json

/**
 * Call a public slot or invokable. Arguments are converted to the declared parameter types; an overload is chosen by argument count. The reply is the return value (null for void). A method that takes (consumer, value) and is registered for release on close (obdManager.setParameterDemand, obdManager.setBackgroundDemand, berryIMU.setDemand, audioAnalyzer.setDemand) is called again with its release value ([] or false) for every consumer this connection still holds when it closes.
 * @since protocol 1
 */
export interface CallArgs {
  object: string
  method: string
  /** Positional arguments; default none. */
  args?: Json[]
  timeoutMs?: number
}
/** The method's return value as JSON, or null. */
export type CallReply = Json

/**
 * The rows of a list-model property (the download queue, search results), each as {role: value}. Without `count`, every row from `from` on.
 * @since protocol 1
 */
export interface RowsArgs {
  object: string
  property: string
  /** @default 0 */
  from?: number
  count?: number
  timeoutMs?: number
}
export type RowsReply = Row[]

/**
 * Follow an object: `property` events for each named property (the current value at once, then on every notify; a list-model property sends `model` events instead, starting with a reset of every row), `signal` events for each named signal, and on paramStore `params` events for the ids in `properties` (none = all). The reply, with your `sub` id, always arrives before the first event of that subscription. `throttleMs` > 0 coalesces: at most one event per name per interval, the latest value winning (model events are never coalesced). At least one property or signal is required except on paramStore.
 * @since protocol 1
 */
export interface SubscribeArgs {
  object: string
  /** Property names, or parameter ids on paramStore. */
  properties?: string[]
  signals?: string[]
  /** @default 0 */
  throttleMs?: number
}
export interface SubscribeReply {
  /** The subscription id carried by its events; unique per connection. */
  sub: number
}

/**
 * End a subscription. Unknown ids are an error. A connection's subscriptions end with it.
 * @since protocol 1
 */
export interface UnsubscribeArgs {
  sub: number
}
export type UnsubscribeReply = true

/**
 * Open a media stream (see streams): binary records follow on this connection. The reply gives the stream id in the records and what is known of the format so far (an H.264 stream with no session yet knows only its id); a `streamConfig` event follows every change. A new subscriber receives nothing until the next keyframe. At most 255 streams per connection; a stream ends with `unstream` or the connection.
 * @since protocol 2
 */
export interface StreamArgs {
  /** The object that owns the stream (phoneMirrorManager, androidAutoManager). */
  object: string
  /** The stream's name on it (video, frames). */
  name: string
}
export type StreamReply = StreamInfo

/**
 * Stop one stream by the id `stream` returned. Not an error for an unknown id.
 * @since protocol 2
 */
export interface UnstreamArgs {
  stream: number
}
export type UnstreamReply = true

/**
 * Become a paired device with the 6-digit code OCTAVE shows (Settings > Service > Pair a device, valid 2 minutes, single use). The reply is the device's token; store it and send it with `hello` on later connections. This connection is trusted at once. A refusal (no code showing, wrong code, too many attempts, attempts faster than one a second, an address with 10 wrong codes) is a message-only error.
 * @since protocol 2
 */
export interface PairArgs {
  code: string
  /** What the user sees in the device list ("Kitchen tablet"). */
  name: string
  /** The permissions this device asks for; absent or empty: full. An unknown one fails `pair` without using the code. */
  scopes?: ("read" | "media" | "vehicle" | "phone" | "settings" | "files" | "system" | "full")[]
}
export interface PairReply {
  token: string
  /** What was granted: `read` plus what was asked for, or `full`. */
  scopes: ("read" | "media" | "vehicle" | "phone" | "settings" | "files" | "system" | "full")[]
}

/**
 * Forget the caller's own device: its token stops working everywhere. An error when this connection presented no token (a loopback connection trusted by serviceLoopbackOpen has no device to forget).
 * @since protocol 2
 */
export type UnpairArgs = Record<string, Json>
export type UnpairReply = true

/**
 * A short-lived ticket for the HTTP routes (`/art?ticket=...`, `/export/...?ticket=...`), so no token goes in a URL. Bound to the caller's token (revoked with it); a loopback connection gets one too. At most 256 exist at once (the oldest goes).
 * @since protocol 2
 */
export type MediaTicketArgs = Record<string, Json>
export interface MediaTicketReply {
  ticket: string
  /** Seconds the ticket is valid (600). */
  expiresIn: number
}

/** Each op's arguments and reply, for a typed `request`. */
export interface Ops {
  hello: { args: HelloArgs; reply: HelloReply }
  describe: { args: DescribeArgs; reply: DescribeReply }
  get: { args: GetArgs; reply: GetReply }
  set: { args: SetArgs; reply: SetReply }
  call: { args: CallArgs; reply: CallReply }
  rows: { args: RowsArgs; reply: RowsReply }
  subscribe: { args: SubscribeArgs; reply: SubscribeReply }
  unsubscribe: { args: UnsubscribeArgs; reply: UnsubscribeReply }
  stream: { args: StreamArgs; reply: StreamReply }
  unstream: { args: UnstreamArgs; reply: UnstreamReply }
  pair: { args: PairArgs; reply: PairReply }
  unpair: { args: UnpairArgs; reply: UnpairReply }
  mediaTicket: { args: MediaTicketArgs; reply: MediaTicketReply }
}

/** What a request looks like on the wire: `op`, an optional `id` the reply echoes, and the op's arguments. */
export type Request<O extends Op = Op> = { op: O; id?: Json } & Ops[O]['args']
export type Reply = OkReply | ErrorReply
/** The `code`s a failure reply can carry; every other failure is a message only. */
export type ErrorCode = "unauthorised" | "forbidden"
export const ERROR_CODES: Record<ErrorCode, { description: string; since?: number; http?: number }> = {
  "unauthorised": {
    "description": "The connection is not trusted (protocol 2, pairing): it is not on this device with serviceLoopbackOpen on, and it has presented no valid token. Every op but `hello` and `pair` answers this until the client pairs or sends a valid token with `hello`. HTTP routes answer 401 for the same reason.",
    "since": 2,
    "http": 401
  },
  "forbidden": {
    "description": "The connection is trusted but its paired device lacks the permission scope this `set` / `call` / phone stream / dev action needs (docs/api/README.md, Permissions). The message names the member, the scope it needs and the scopes the device has. HTTP routes answer 401 for the same reason (`/export/...` needs `files`).",
    "since": 2
  }
}

/** A subscribed property's value: at once on subscribing, then on every notify (coalesced by throttleMs). */
export interface PropertyEvent {
  event: "property"
  sub: number
  object: string
  name: string
  /** The value as JSON. */
  value: Json
  t: number
}

/** A subscribed signal fired; `args` are its arguments in order. */
export interface SignalEvent {
  event: "signal"
  sub: number
  object: string
  name: string
  args: Json[]
  t: number
}

/** paramStore: the ids that changed in one batch (33 ms coalescing in the store; merged further by throttleMs), in native units; at once on subscribing with every id that has a value. A value is a number, or null when the reading is gone. */
export interface ParamsEvent {
  event: "params"
  sub: number
  object: "paramStore"
  values: Record<string, number | null>
  t: number
}

/** A subscribed list-model property changed. Replay in order: `reset` replaces every row (sent first, with all rows), `insert` adds rows at first..last, `remove` deletes first..last, `update` replaces rows first..last. `rows` is present for reset, insert and update. Never coalesced by throttleMs. */
export interface ModelEvent {
  event: "model"
  sub: number
  object: string
  name: string
  change: "reset" | "insert" | "remove" | "update"
  first: number
  last: number
  rows?: Row[]
  t: number
}

/** The client stopped reading and the oldest `count` JSON events were discarded (the queue holds 1000). Replies are never dropped. */
export interface DroppedEvent {
  event: "dropped"
  count: number
}

/** A stream's parameter sets changed (the phone rotated, a new session): the codec and picture size of the records that follow. Reconfigure the decoder. */
export interface StreamConfigEvent {
  event: "streamConfig"
  stream: number
  format: "annexb" | "jpeg"
  /** RFC 6381, e.g. avc1.42c01e (from the SPS). */
  codec?: string
  width?: number
  height?: number
}

/** This stream's queued records overflowed (2 MB or 90 records) and were all discarded; `count` is the number of such drops so far. The stream resumes at the next keyframe, whose record has the discontinuity flag. */
export interface StreamDroppedEvent {
  event: "streamDropped"
  stream: number
  count: number
}

/** A message with an `event` key is not a reply. Subscription events carry `sub` and `object`, and `t`, the service's monotonic clock in milliseconds when the event was sent (a throttled one: at the flush). */
export type ServiceEvent = PropertyEvent | SignalEvent | ParamsEvent | ModelEvent | DroppedEvent | StreamConfigEvent | StreamDroppedEvent
export type EventName = ServiceEvent['event']

/** Media stream record flags (streams.flags). */
/** A keyframe (every JPEG record). */
export const F_KEY = 1
/** SPS / PPS are inline before the IDR. */
export const F_CONFIG = 2
/** The payload is a JPEG. */
export const F_JPEG = 4
/** The first record after joining or after a drop. */
export const F_DISCONTINUITY = 8
/** Bytes before a record's payload. */
export const RECORD_HEADER = 12
/** The streams the service has, as `object.name`, with their format. */
export const STREAMS = {"phoneMirrorManager.video":"annexb","androidAutoManager.frames":"jpeg"} as const
export type StreamName = keyof typeof STREAMS

/** The HTTP routes (GET, trusted: a media ticket or a bearer token). */
export const ROUTES = ["/art","/export/trip","/export/dashboard","/export/logs"] as const
export type Route = typeof ROUTES[number]
