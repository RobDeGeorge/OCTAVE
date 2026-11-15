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
/** The versions this client accepts: every one up to the version it was generated from
 *  (1 no pairing, 2 pairing, 3 no downloadManager; everything else is additive). Derived
 *  from the generated constant, so a regenerated protocol.ts moves both. */
export const PROTOCOL_VERSIONS: number[] = Array.from({ length: SPEC_VERSION }, (_, i) => i + 1)

export type { Json, ServiceEvent } from './protocol.js'
export type Status = 'connecting' | 'online' | 'offline' | 'mismatch'
/** A permission a paired device holds (docs/api/README.md, Permissions). */
export type Scope = NonNullable<Ops['pair']['args']['scopes']>[number]

/** The `hello` reply (generated: protocol.ts). `app` and `objects` are present on a trusted connection. */
export type Hello = HelloReply
/** One object's properties, methods and signals, as `describe` answers (generated: protocol.ts). */
export type Description = ObjectDescription
export type { PropertyDescription as PropertyMeta, MethodDescription as MethodMeta, SignalDescription as SignalMeta, ParamDescription as ParamMeta, ModelEvent as ModelChange } from './protocol.js'

/** A failure reply: `message` is the server's `error`, `code` its stable code when it has
 *  one ("unauthorised", "forbidden", "rateLimited"; "timeout" is this client's own, for a
 *  request nothing answered). `retryAfterMs` comes with "rateLimited": when the next op would pass. */
export class OctaveError extends Error {
  constructor(message: string, readonly code?: string, readonly retryAfterMs?: number) { super(message) }
}

export interface SubscribeOptions { properties?: string[]; signals?: string[]; throttleMs?: number }
export interface Subscription { object: string; options: SubscribeOptions; handler: (e: ServiceEvent) => void; id: number; onError?: (e: OctaveError) => void }
interface Pending { resolve: (v: Json) => void; reject: (e: Error) => void; sub?: Subscription; stream?: StreamHandlers; timer?: ReturnType<typeof setTimeout> }

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
  /** A request nothing answers fails with code "timeout" after this long. Default 30000; 0 waits for ever. */
  requestTimeoutMs?: number
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
  private listeners = {
    status: new Set<(s: Status) => void>(), hello: new Set<(h: Hello) => void>(),
    error: new Set<(e: OctaveError, where: { object: string }) => void>(),
  }

  constructor(options: ClientOptions = {}) {
    this.url = options.url ?? defaultUrl()
    this.opts = { checkVersion: options.checkVersion ?? true, minBackoffMs: options.minBackoffMs ?? 500, maxBackoffMs: options.maxBackoffMs ?? 8000, requestTimeoutMs: options.requestTimeoutMs ?? 30000 }
    this.token = options.token
    this.backoff = this.opts.minBackoffMs
  }

  get status(): Status { return this._status }
  /** True when the current connection may use the service (protocol 1, or protocol 2 trusted). */
  get trusted(): boolean { return this._status === 'online' && (this._hello?.auth ?? 'trusted') === 'trusted' }

  /** Use this token from the next connection on (protocol 2). Pass undefined to forget it. */
  setToken(token: string | undefined): void { this.token = token }

  /** Pair this device with a code from the head unit's Settings; the token is
   *  kept for later connections and returned so the caller can store it. The
   *  connection is trusted from here on: `hello` is asked again (an untrusted
   *  one carries no `app` or `objects`) and the subscriptions made while
   *  waiting for the code are issued. */
  async pair(code: string, name: string, scopes?: Scope[]): Promise<string> {
    const r = (await this.send0('pair', scopes ? { code, name, scopes } : { code, name })) as { token: string; scopes: Scope[] }
    this.token = r.token
    if (this._hello) { this._hello.auth = 'trusted'; this._hello.scopes = r.scopes }
    try {
      const hello = (await this.send0('hello', { token: r.token })) as unknown as Hello
      this._hello = hello
      for (const f of this.listeners.hello) f(hello)
    } catch { /* the pairing stands; the next connection says hello with the token */ }
    if (this.trusted) for (const sub of this.subs) if (!sub.id && !this.issuing.has(sub)) this.issue(sub)
    return r.token
  }
  /** Forget this device on the head unit, and the token here. The connection is untrusted afterwards. */
  async unpair(): Promise<void> {
    await this.request('unpair', {})
    this.token = undefined
    if (this._hello) { this._hello.auth = 'untrusted'; delete this._hello.scopes; delete this._hello.app; delete this._hello.objects }
    for (const sub of this.subs) sub.id = 0
  }
  /** A short-lived ticket for /art and /export URLs (never put the token in a URL). */
  mediaTicket(): Promise<Ops['mediaTicket']['reply']> { return this.request('mediaTicket', {}) }
  /** The last `hello` reply, once online. */
  get hello(): Hello | null { return this._hello }

  /** Listen for status changes (called at once with the current one), hello replies, or
   *  a subscription the service refused (unknown object, missing permission) when its
   *  subscribe() call gave no onError of its own. Returns the unsubscribe. */
  on(event: 'status', cb: (s: Status) => void): () => void
  on(event: 'hello', cb: (h: Hello) => void): () => void
  on(event: 'error', cb: (e: OctaveError, where: { object: string }) => void): () => void
  on(event: 'status' | 'hello' | 'error', cb: ((s: Status) => void) | ((h: Hello) => void) | ((e: OctaveError, where: { object: string }) => void)): () => void {
    if (event === 'status') { const f = cb as (s: Status) => void; this.listeners.status.add(f); f(this._status); return () => this.listeners.status.delete(f) }
    if (event === 'error') { const f = cb as (e: OctaveError, where: { object: string }) => void; this.listeners.error.add(f); return () => this.listeners.error.delete(f) }
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
      try {
        const hello = (await this.send0('hello', this.token ? { token: this.token } : {})) as unknown as Hello
        this._hello = hello
        for (const f of this.listeners.hello) f(hello)
        if (this.opts.checkVersion && !PROTOCOL_VERSIONS.includes(hello.protocolVersion)) { this.setStatus('mismatch'); return }
        // Only a link that answered hello counts as up: a port that accepts and
        // then says nothing must not reset the backoff
        this.backoff = this.opts.minBackoffMs
        this.setStatus('online')
        // An untrusted connection may only pair; its subscriptions wait for pair()
        if (this.trusted) for (const sub of this.subs) this.issue(sub)
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
      this.failPending(new OctaveError('connection closed'))
      for (const sub of this.subs) sub.id = 0
      this.issuing.clear()
      this.streams.clear()
      this.describeCache.clear()
      this.setStatus('offline')
      if (!this.closed) this.scheduleRetry()
    }
  }

  private failPending(e: OctaveError): void {
    const waiting = [...this.pending.values()]
    this.pending.clear()
    for (const p of waiting) { if (p.timer) clearTimeout(p.timer); p.reject(e) }
  }

  private scheduleRetry(): void {
    // Jitter (50-100 % of the step), so the clients of one head unit do not all return at once
    const delay = this.backoff * (0.5 + Math.random() * 0.5)
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
    // A refusal with no id ("Invalid JSON", "request too long") answers a line
    // the service could not read, so it cannot say which request that was:
    // every waiting one fails, rather than one of them waiting for ever
    if (reply.ok === false && typeof reply.id !== 'number') {
      this.failPending(new OctaveError(String(reply.error ?? 'unknown error'), reply.code, reply.retryAfterMs))
      return
    }
    const p = this.pending.get(reply.id as number)
    if (!p) return
    this.pending.delete(reply.id as number)
    if (p.timer) clearTimeout(p.timer)
    if (p.sub) this.issuing.delete(p.sub)
    if (!reply.ok) { p.reject(new OctaveError(String(reply.error ?? 'unknown error'), reply.code, reply.retryAfterMs)); return }
    // A subscribe reply names the id; its first events follow at once, so
    // the id is recorded here, not in a promise continuation. A subscription
    // dropped before its reply came is ended on the service now
    if (p.sub) {
      const id = (reply.result as { sub: number }).sub
      if (this.subs.has(p.sub)) p.sub.id = id
      else this.send0('unsubscribe', { sub: id }).catch(() => {})
    }
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
      const p: Pending = { resolve, reject, sub, stream }
      const ms = this.opts.requestTimeoutMs
      if (ms > 0) {
        p.timer = setTimeout(() => {
          if (this.pending.get(id) !== p) return
          this.pending.delete(id)
          if (sub) this.issuing.delete(sub)
          reject(new OctaveError(`${op} was not answered in ${ms} ms`, 'timeout'))
        }, ms)
      }
      this.pending.set(id, p)
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

  /** Subscriptions whose subscribe is on the wire and not answered yet. */
  private issuing = new Set<Subscription>()

  private issue(sub: Subscription): void {
    const args: Record<string, unknown> = { object: sub.object, ...sub.options }
    this.issuing.add(sub)
    this.send0('subscribe', args, sub).catch((e: unknown) => {
      this.issuing.delete(sub)
      // A lost link is not the subscription's fault: it is issued again on reconnect
      const err = e instanceof OctaveError ? e : new OctaveError(String(e))
      if (!this.subs.has(sub) || err.message === 'connection closed' || err.message === 'not connected') return
      if (sub.onError) sub.onError(err)
      else for (const f of this.listeners.error) f(err, { object: sub.object })
    })
  }

  /** Subscribe to properties and / or signals of one object. Every event
   *  goes to `handler`; the current values arrive at once. Kept across
   *  reconnects until the returned function is called. On an untrusted
   *  connection it waits for pair(). A refusal by the service (unknown
   *  object, a permission this device lacks) goes to `onError`, or to
   *  on('error') without one; the subscription stays registered and is tried
   *  again on the next connection. */
  subscribe(object: string, options: SubscribeOptions, handler: (e: ServiceEvent) => void, onError?: (e: OctaveError) => void): () => void {
    const sub: Subscription = { object, options, handler, id: 0, onError }
    this.subs.add(sub)
    if (this.trusted) this.issue(sub)
    return () => {
      this.subs.delete(sub)
      // Not answered yet: dispatch() ends it when the reply names its id
      if (sub.id && this.ws) this.send0('unsubscribe', { sub: sub.id }).catch(() => {})
      sub.id = 0
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
    // Stream ids start again on a new connection: only close the one this call opened
    return { info, close: () => { if (this.streams.get(info.stream) === handlers && this.streams.delete(info.stream)) this.request('unstream', { stream: info.stream }).catch(() => {}) } }
  }
}
