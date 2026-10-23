// The service-protocol client: one WebSocket to OCTAVE carrying JSON text
// frames (wiki/utilities.html#service-protocol, docs/api/) and binary media
// records. Reconnects with backoff and re-issues every subscription; requests
// are matched to replies by id; subscriptions deliver through callbacks, so
// any UI layer (Svelte stores, React state, a terminal) sits on top.
import { parseRecord, type StreamHandlers } from './stream.js'
import {
  PROTOCOL_VERSION as SPEC_VERSION, type Json, type Op, type Ops, type HelloReply, type ObjectDescription, type ServiceEvent, type Reply, type StreamInfo,
} from './protocol.js'

/** The protocol version this client was written against: the one docs/api/protocol.json describes. */
export const PROTOCOL_VERSION = SPEC_VERSION
/** The versions this client accepts: 1 (no pairing) and 2 (everything else is additive). */
export const PROTOCOL_VERSIONS = [1, 2]

export type { Json, ServiceEvent } from './protocol.js'
export type Status = 'connecting' | 'online' | 'offline' | 'mismatch'

/** The `hello` reply (generated: protocol.ts). `app` and `objects` are present on a trusted connection. */
export type Hello = HelloReply
/** One object's properties, methods and signals, as `describe` answers (generated: protocol.ts). */
export type Description = ObjectDescription
export type { PropertyDescription as PropertyMeta, MethodDescription as MethodMeta, SignalDescription as SignalMeta, ParamDescription as ParamMeta, ModelEvent as ModelChange } from './protocol.js'

/** A failure reply: `message` is the server's `error`, `code` its stable code when it has one ("unauthorised"). */
export class OctaveError extends Error {
  constructor(message: string, readonly code?: string) { super(message) }
}

export interface SubscribeOptions { properties?: string[]; signals?: string[]; throttleMs?: number }
export interface Subscription { object: string; options: SubscribeOptions; handler: (e: ServiceEvent) => void; id: number }
interface Pending { resolve: (v: Json) => void; reject: (e: Error) => void; sub?: Subscription; stream?: StreamHandlers }

export interface ClientOptions {
  /** ws://host:port/ ; default: this page's host in a browser, 127.0.0.1:9876 in Node. */
  url?: string
  /** Refuse (status 'mismatch') when the app speaks another protocol version. Default true. */
  checkVersion?: boolean
  /** Reconnect backoff bounds in ms. */
  minBackoffMs?: number
  maxBackoffMs?: number
  /** A paired device's token (protocol 2), sent with hello on every connection. */
  token?: string
}

export function defaultUrl(): string {
  const loc = (globalThis as { location?: { protocol: string; host: string } }).location
  if (loc && loc.host) return `${loc.protocol === 'https:' ? 'wss:' : 'ws:'}//${loc.host}/`
  return 'ws://127.0.0.1:9876/'
}

/** One connection to OCTAVE. */
export class OctaveClient {
  readonly url: string
  private opts: Required<Omit<ClientOptions, 'url' | 'token'>>
  private token: string | undefined
  private ws: WebSocket | null = null
  private _status: Status = 'offline'
  private _hello: Hello | null = null
  private nextId = 1
  private pending = new Map<number, Pending>()
  private subs = new Set<Subscription>()
  private streams = new Map<number, StreamHandlers>()
  private describeCache = new Map<string, Promise<Description>>()
  private backoff: number
  private retryTimer: ReturnType<typeof setTimeout> | null = null
  private closed = true
  private listeners = { status: new Set<(s: Status) => void>(), hello: new Set<(h: Hello) => void>() }

  constructor(options: ClientOptions = {}) {
    this.url = options.url ?? defaultUrl()
    this.opts = { checkVersion: options.checkVersion ?? true, minBackoffMs: options.minBackoffMs ?? 500, maxBackoffMs: options.maxBackoffMs ?? 8000 }
    this.token = options.token
    this.backoff = this.opts.minBackoffMs
  }

  get status(): Status { return this._status }
  /** True when the current connection may use the service (protocol 1, or protocol 2 trusted). */
  get trusted(): boolean { return this._status === 'online' && (this._hello?.auth ?? 'trusted') === 'trusted' }

  /** Use this token from the next connection on (protocol 2). Pass undefined to forget it. */
  setToken(token: string | undefined): void { this.token = token }

  /** Pair this device with a code from the head unit's Settings; the token is
   *  kept for later connections and returned so the caller can store it. */
  async pair(code: string, name: string): Promise<string> {
    const r = (await this.send0('pair', { code, name })) as { token: string }
    this.token = r.token
    if (this._hello) this._hello.auth = 'trusted'
    return r.token
  }
  /** Forget this device on the head unit, and the token here. */
  async unpair(): Promise<void> { await this.request('unpair', {}); this.token = undefined }
  /** A short-lived ticket for /art and /export URLs (never put the token in a URL). */
  mediaTicket(): Promise<Ops['mediaTicket']['reply']> { return this.request('mediaTicket', {}) }
  /** The last `hello` reply, once online. */
  get hello(): Hello | null { return this._hello }

  /** Listen for status changes (called at once with the current one) or hello replies. Returns the unsubscribe. */
  on(event: 'status', cb: (s: Status) => void): () => void
  on(event: 'hello', cb: (h: Hello) => void): () => void
  on(event: 'status' | 'hello', cb: ((s: Status) => void) | ((h: Hello) => void)): () => void {
    if (event === 'status') { const f = cb as (s: Status) => void; this.listeners.status.add(f); f(this._status); return () => this.listeners.status.delete(f) }
    const f = cb as (h: Hello) => void
    this.listeners.hello.add(f)
    return () => this.listeners.hello.delete(f)
  }

  // ── lifecycle ─────────────────────────────────────────────────────────

  /** Open the connection and keep it open until close(). */
  connect(): void {
    if (!this.closed) return
    this.closed = false
    this.open()
  }

  close(): void {
    this.closed = true
    if (this.retryTimer) clearTimeout(this.retryTimer)
    this.retryTimer = null
    this.ws?.close()
  }

  /** Resolves once online (hello answered), or rejects after `ms`. */
  whenOnline(ms = 10000): Promise<void> {
    if (this._status === 'online') return Promise.resolve()
    return new Promise((resolve, reject) => {
      const t = setTimeout(() => { off(); reject(new OctaveError('not connected')) }, ms)
      const off = this.on('status', (s) => { if (s === 'online') { clearTimeout(t); queueMicrotask(off); resolve() } })
    })
  }

  private setStatus(s: Status): void {
    if (s === this._status) return
    this._status = s
    for (const f of this.listeners.status) f(s)
  }

  private open(): void {
    this.setStatus('connecting')
    const ws = new WebSocket(this.url)
    ws.binaryType = 'arraybuffer'
    this.ws = ws
    ws.onopen = async () => {
      this.backoff = this.opts.minBackoffMs
      try {
        const hello = (await this.send0('hello', this.token ? { token: this.token } : {})) as unknown as Hello
        this._hello = hello
        for (const f of this.listeners.hello) f(hello)
        if (this.opts.checkVersion && !PROTOCOL_VERSIONS.includes(hello.protocolVersion)) { this.setStatus('mismatch'); return }
        this.setStatus('online')
        for (const sub of this.subs) this.issue(sub)
      } catch {
        ws.close()
      }
    }
    ws.onmessage = (m: MessageEvent) => {
      if (m.data instanceof ArrayBuffer) { const r = parseRecord(m.data); if (r) this.streams.get(r.stream)?.onRecord(r); return }
      this.dispatch(JSON.parse(String(m.data)))
    }
    ws.onerror = () => { /* onclose follows */ }
    ws.onclose = () => {
      if (this.ws !== ws) return
      this.ws = null
      for (const p of this.pending.values()) p.reject(new OctaveError('connection closed'))
      this.pending.clear()
      for (const sub of this.subs) sub.id = 0
      this.streams.clear()
      this.describeCache.clear()
      this.setStatus('offline')
      if (!this.closed) this.scheduleRetry()
    }
  }

  private scheduleRetry(): void {
    const delay = this.backoff
    this.backoff = Math.min(this.backoff * 1.7, this.opts.maxBackoffMs)
    this.retryTimer = setTimeout(() => { this.retryTimer = null; this.open() }, delay)
  }

  private dispatch(msg: Record<string, Json>): void {
    if ('event' in msg) {
      const e = msg as unknown as ServiceEvent
      if (e.event === 'dropped') return
      if (e.event === 'streamConfig') { this.streams.get(e.stream)?.onConfig?.(e as StreamInfo); return }
      if (e.event === 'streamDropped') { this.streams.get(e.stream)?.onDropped?.(e.count); return }
      for (const sub of this.subs) if (sub.id === e.sub) sub.handler(e)
      return
    }
    const reply = msg as unknown as Reply
    const p = this.pending.get(reply.id as number)
    if (!p) return
    this.pending.delete(reply.id as number)
    if (!reply.ok) { p.reject(new OctaveError(String(reply.error ?? 'unknown error'), reply.code)); return }
    // A subscribe reply names the id; its first events follow at once, so
    // the id is recorded here, not in a promise continuation
    if (p.sub) p.sub.id = (reply.result as { sub: number }).sub
    // Likewise a stream's first record can be dispatched in the same task as its reply
    if (p.stream) this.streams.set((reply.result as { stream: number }).stream, p.stream)
    p.resolve(reply.result)
  }

  // ── requests ──────────────────────────────────────────────────────────

  /** Any op, typed by protocol.json for the ones it knows (`request('get', { object, property })`).
   *  While (re)connecting the request waits for the link rather than failing. */
  request<O extends Op>(op: O, args: Ops[O]['args']): Promise<Ops[O]['reply']>
  request<O extends string>(op: O extends Op ? never : O, args?: Record<string, unknown>): Promise<Json>
  async request(op: string, args: Record<string, unknown> = {}): Promise<Json> {
    if (this._status !== 'online') await this.whenOnline()
    return this.send0(op, args)
  }

  private send0(op: string, args: Record<string, unknown>, sub?: Subscription, stream?: StreamHandlers): Promise<Json> {
    const ws = this.ws
    if (!ws || ws.readyState !== WebSocket.OPEN) return Promise.reject(new OctaveError('not connected'))
    const id = this.nextId++
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject, sub, stream })
      ws.send(JSON.stringify({ op, id, ...args }))
    })
  }

  get<T = Json>(object: string, property?: string): Promise<T> {
    return this.request('get', property ? { object, property } : { object }) as Promise<T>
  }
  set<T = Json>(object: string, property: string, value: unknown): Promise<T> {
    return this.request('set', { object, property, value: value as Json }) as Promise<T>
  }
  call<T = Json>(object: string, method: string, ...args: unknown[]): Promise<T> {
    return this.request('call', { object, method, args: args as Json[] }) as Promise<T>
  }
  rows<T = Record<string, Json>>(object: string, property: string, from = 0, count?: number): Promise<T[]> {
    return this.request('rows', { object, property, from, ...(count !== undefined ? { count } : {}) }) as Promise<T[]>
  }
  describe(object: string): Promise<Description> {
    let p = this.describeCache.get(object)
    if (!p) {
      p = this.request('describe', { object }) as unknown as Promise<Description>
      p.catch(() => this.describeCache.delete(object))
      this.describeCache.set(object, p)
    }
    return p
  }

  // ── subscriptions ─────────────────────────────────────────────────────

  private issue(sub: Subscription): void {
    const args: Record<string, unknown> = { object: sub.object, ...sub.options }
    this.send0('subscribe', args, sub).catch(() => { /* reported through the handler's silence; retried on reconnect */ })
  }

  /** Subscribe to properties and / or signals of one object. Every event
   *  goes to `handler`; the current values arrive at once. Kept across
   *  reconnects until the returned function is called. */
  subscribe(object: string, options: SubscribeOptions, handler: (e: ServiceEvent) => void): () => void {
    const sub: Subscription = { object, options, handler, id: 0 }
    this.subs.add(sub)
    if (this._status === 'online') this.issue(sub)
    return () => {
      this.subs.delete(sub)
      if (sub.id && this.ws) this.send0('unsubscribe', { sub: sub.id }).catch(() => {})
    }
  }

  /** One property's value, now and on every change. */
  property<T = Json>(object: string, name: string, cb: (value: T) => void, throttleMs?: number): () => void {
    return this.subscribe(object, { properties: [name], throttleMs }, (e) => { if (e.event === 'property' && e.name === name) cb(e.value as T) })
  }

  /** A signal's arguments, each time it fires. */
  signal(object: string, name: string, cb: (...args: Json[]) => void): () => void {
    return this.subscribe(object, { signals: [name] }, (e) => { if (e.event === 'signal' && e.name === name) cb(...e.args) })
  }

  /** Live paramStore values (native units) for `ids`: cb gets {id: value} with the changed ones. */
  params(ids: string[], cb: (values: Record<string, number | null>) => void, throttleMs = 50): () => void {
    return this.subscribe('paramStore', { properties: ids, throttleMs }, (e) => { if (e.event === 'params') cb(e.values) })
  }

  /** A list-model property as rows kept current from model events. */
  model<T = Record<string, Json>>(object: string, name: string, cb: (rows: T[]) => void): () => void {
    let rows: T[] = []
    return this.subscribe(object, { properties: [name] }, (e) => {
      if (e.event !== 'model' || e.name !== name) return
      const r = rows.slice()
      switch (e.change) {
        case 'reset': rows = (e.rows as T[]) ?? []; break
        case 'insert': r.splice(e.first, 0, ...(e.rows as T[])); rows = r; break
        case 'remove': r.splice(e.first, e.last - e.first + 1); rows = r; break
        case 'update': (e.rows as T[]).forEach((row, i) => { r[e.first + i] = row }); rows = r; break
      }
      cb(rows)
    })
  }

  // ── media streams ─────────────────────────────────────────────────────

  /** Open a media stream (phoneMirrorManager "video", androidAutoManager
   *  "frames"). Records arrive on handlers.onRecord. A stream ends with its
   *  connection; open it again after a reconnect. */
  async stream(object: string, name: string, handlers: StreamHandlers): Promise<{ info: StreamInfo; close: () => void }> {
    if (this._status !== 'online') await this.whenOnline()
    const info = (await this.send0('stream', { object, name }, undefined, handlers)) as unknown as StreamInfo
    handlers.onConfig?.(info)
    return { info, close: () => { if (this.streams.delete(info.stream)) this.request('unstream', { stream: info.stream }).catch(() => {}) } }
  }
}
