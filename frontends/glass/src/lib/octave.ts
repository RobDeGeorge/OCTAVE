// The service-protocol client for the browser: one WebSocket to OCTAVE
// carrying JSON messages (wiki/utilities.html#service-protocol, docs/api/).
//
// - connect / reconnect with backoff; every subscription is re-issued on
//   reconnect, so a page only declares what it wants once
// - request / reply by id (hello, describe, get, set, call, rows, ...)
// - subscriptions as Svelte stores: prop(), params(), model(), signal()
// - a describe cache
//
// Written against protocol version 1 (docs/api/README.md).

import { readable, writable, get, type Readable, type Writable } from 'svelte/store'

export const PROTOCOL_VERSION = 1

export type Json = null | boolean | number | string | Json[] | { [k: string]: Json }
export type Status = 'connecting' | 'online' | 'offline' | 'mismatch'

export interface Hello {
  protocolVersion: number
  app: { version: string; build: string; headless: boolean }
  objects: string[]
}

export interface PropertyMeta {
  name: string
  type: string
  readable: boolean
  writable: boolean
  constant?: boolean
  notify?: string
  enum?: string[]
}
export interface ParamMeta { name: string; type: string }
export interface MethodMeta { name: string; kind: string; params: ParamMeta[]; returns?: string }
export interface SignalMeta { name: string; params: ParamMeta[] }
export interface Description {
  object: string
  class: string
  properties: PropertyMeta[]
  methods: MethodMeta[]
  signals: SignalMeta[]
  enums?: Record<string, string[]>
}

export type ModelChange =
  | { change: 'reset' | 'insert' | 'update'; first: number; last: number; rows: Record<string, Json>[] }
  | { change: 'remove'; first: number; last: number }

type Event =
  | { event: 'property'; sub: number; object: string; name: string; value: Json; t?: number }
  | { event: 'params'; sub: number; object: string; values: Record<string, number | null> }
  | { event: 'signal'; sub: number; object: string; name: string; args: Json[] }
  | ({ event: 'model'; sub: number; object: string; name: string } & ModelChange)
  | { event: 'dropped'; count: number }

interface Pending { resolve: (v: Json) => void; reject: (e: Error) => void; sub?: Sub }

/** What one `subscribe` to the service looks like, kept so it can be replayed. */
interface Sub {
  object: string
  properties?: string[]
  signals?: string[]
  throttleMs?: number
  /** Called for every event that arrives on this subscription. */
  onEvent: (e: Event) => void
  /** The id the service gave us this time round, or 0 while offline. */
  id: number
}

export class OctaveError extends Error {}

/** Where the service is for this page: dev-proxied under /ws, or the page's own host. */
export function defaultUrl(): string {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
  const host = location.host || '127.0.0.1:9876'
  return import.meta.env.DEV ? `${proto}//${host}/ws` : `${proto}//${host}/`
}

export class Octave {
  readonly status: Writable<Status> = writable('connecting')
  readonly hello: Writable<Hello | null> = writable(null)
  /** Seconds until the next reconnect attempt, for the offline banner. */
  readonly retryIn: Writable<number> = writable(0)

  private ws: WebSocket | null = null
  private nextId = 1
  private pending = new Map<number, Pending>()
  private subs = new Set<Sub>()
  private describeCache = new Map<string, Promise<Description>>()
  private backoff = 500
  private retryTimer: ReturnType<typeof setTimeout> | null = null
  private closed = false

  constructor(readonly url: string = defaultUrl()) {}

  // ── lifecycle ───────────────────────────────────────────────────────────

  connect(): void {
    this.closed = false
    this.open()
  }

  close(): void {
    this.closed = true
    if (this.retryTimer) clearTimeout(this.retryTimer)
    this.ws?.close()
  }

  private open(): void {
    this.status.set('connecting')
    const ws = new WebSocket(this.url)
    this.ws = ws
    ws.onopen = async () => {
      this.backoff = 500
      try {
        const hello = (await this.request('hello')) as unknown as Hello
        this.hello.set(hello)
        if (hello.protocolVersion !== PROTOCOL_VERSION) {
          this.status.set('mismatch')
        } else {
          this.status.set('online')
        }
        // Replay every live subscription on the new connection
        for (const sub of this.subs) this.send(sub)
      } catch (e) {
        console.warn('octave: hello failed', e)
        ws.close()
      }
    }
    ws.onmessage = (m) => this.dispatch(JSON.parse(m.data))
    ws.onerror = () => { /* onclose follows */ }
    ws.onclose = () => {
      if (this.ws !== ws) return
      this.ws = null
      for (const p of this.pending.values()) p.reject(new OctaveError('connection closed'))
      this.pending.clear()
      for (const sub of this.subs) sub.id = 0
      this.describeCache.clear()
      this.status.set('offline')
      if (!this.closed) this.scheduleRetry()
    }
  }

  private scheduleRetry(): void {
    const delay = this.backoff
    this.backoff = Math.min(this.backoff * 1.7, 8000)
    const started = Date.now()
    this.retryIn.set(Math.ceil(delay / 1000))
    const tick = setInterval(() => {
      this.retryIn.set(Math.max(0, Math.ceil((delay - (Date.now() - started)) / 1000)))
    }, 250)
    this.retryTimer = setTimeout(() => {
      clearInterval(tick)
      this.retryIn.set(0)
      this.open()
    }, delay)
  }

  private dispatch(msg: Record<string, Json>): void {
    if ('event' in msg) {
      const e = msg as unknown as Event
      if (e.event === 'dropped') {
        console.warn(`octave: ${e.count} events dropped`)
        return
      }
      for (const sub of this.subs) if (sub.id === e.sub) sub.onEvent(e)
      return
    }
    const p = this.pending.get(msg.id as number)
    if (!p) return
    this.pending.delete(msg.id as number)
    // A subscribe reply names its id: record it here, synchronously, since the
    // subscription's first events are already in the next frames and a
    // promise .then would run after they had been dispatched.
    if (msg.ok && p.sub) p.sub.id = (msg.result as { sub: number }).sub
    if (msg.ok) p.resolve(msg.result as Json)
    else p.reject(new OctaveError(String(msg.error ?? 'unknown error')))
  }

  // ── requests ────────────────────────────────────────────────────────────

  /** Resolves once the connection is open (hello answered), or rejects after `ms`. */
  whenOnline(ms = 10000): Promise<void> {
    if (get(this.status) === 'online') return Promise.resolve()
    return new Promise((resolve, reject) => {
      const t = setTimeout(() => { unsub(); reject(new OctaveError('not connected')) }, ms)
      const unsub = this.status.subscribe((s) => { if (s === 'online') { clearTimeout(t); queueMicrotask(unsub); resolve() } })
    })
  }

  /** Any op. A request made while (re)connecting waits for the connection
   *  rather than failing, so a page can ask for what it needs on load. */
  async request(op: string, args: Record<string, unknown> = {}): Promise<Json> {
    if (op !== 'hello' && get(this.status) !== 'online') await this.whenOnline()
    return this.send0(op, args)
  }

  private send0(op: string, args: Record<string, unknown>, sub?: Sub): Promise<Json> {
    const ws = this.ws
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      return Promise.reject(new OctaveError('not connected'))
    }
    const id = this.nextId++
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject, sub })
      ws.send(JSON.stringify({ op, id, ...args }))
    })
  }

  get<T = Json>(object: string, property?: string): Promise<T> {
    return this.request('get', property ? { object, property } : { object }) as Promise<T>
  }

  set<T = Json>(object: string, property: string, value: unknown): Promise<T> {
    return this.request('set', { object, property, value }) as Promise<T>
  }

  call<T = Json>(object: string, method: string, ...args: unknown[]): Promise<T> {
    return this.request('call', { object, method, args }) as Promise<T>
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

  // ── subscriptions ───────────────────────────────────────────────────────

  private send(sub: Sub): void {
    const args: Record<string, unknown> = { object: sub.object }
    if (sub.properties) args.properties = sub.properties
    if (sub.signals) args.signals = sub.signals
    if (sub.throttleMs) args.throttleMs = sub.throttleMs
    this.send0('subscribe', args, sub)
      .catch((e) => console.warn(`octave: subscribe ${sub.object} failed:`, e.message))
  }

  private addSub(sub: Sub): () => void {
    this.subs.add(sub)
    if (get(this.status) === 'online') this.send(sub)
    return () => {
      this.subs.delete(sub)
      if (sub.id) this.request('unsubscribe', { sub: sub.id }).catch(() => {})
    }
  }

  /** One property as a store. The service sends the current value at once. */
  prop<T = Json>(object: string, name: string, initial: T, throttleMs?: number): Readable<T> {
    return readable(initial, (set) =>
      this.addSub({
        object, properties: [name], throttleMs, id: 0,
        onEvent: (e) => { if (e.event === 'property' && e.name === name) set(e.value as T) },
      }))
  }

  /** Several properties of one object, as one store of {name: value}. */
  props<T extends Record<string, Json>>(object: string, names: (keyof T & string)[], initial: T, throttleMs?: number): Readable<T> {
    return readable(initial, (set, update) =>
      this.addSub({
        object, properties: names, throttleMs, id: 0,
        onEvent: (e) => {
          if (e.event === 'property') update((v) => ({ ...v, [e.name]: e.value }))
        },
      }))
  }

  /** A signal: the handler gets its arguments. Returns the unsubscribe. */
  signal(object: string, name: string, handler: (...args: Json[]) => void): () => void {
    return this.addSub({
      object, signals: [name], id: 0,
      onEvent: (e) => { if (e.event === 'signal' && e.name === name) handler(...e.args) },
    })
  }

  /** Live paramStore values (native units) for a set of ids, as {id: value}. */
  params(ids: string[], throttleMs = 50): Readable<Record<string, number>> {
    return readable<Record<string, number>>({}, (set, update) => {
      if (ids.length === 0) return
      return this.addSub({
        object: 'paramStore', properties: ids, throttleMs, id: 0,
        onEvent: (e) => {
          if (e.event !== 'params') return
          update((v) => {
            const n = { ...v }
            for (const [k, x] of Object.entries(e.values)) {
              if (x === null) delete n[k]; else n[k] = x
            }
            return n
          })
        },
      })
    })
  }

  /** A list-model property as a store of rows, kept current from model events. */
  model<T = Record<string, Json>>(object: string, name: string): Readable<T[]> {
    return readable<T[]>([], (set, update) =>
      this.addSub({
        object, properties: [name], id: 0,
        onEvent: (e) => {
          if (e.event !== 'model' || e.name !== name) return
          update((rows) => {
            const r = rows.slice()
            switch (e.change) {
              case 'reset': return (e.rows as T[]) ?? []
              case 'insert': r.splice(e.first, 0, ...(e.rows as T[])); return r
              case 'remove': r.splice(e.first, e.last - e.first + 1); return r
              case 'update': (e.rows as T[]).forEach((row, i) => { r[e.first + i] = row }); return r
            }
          })
        },
      }))
  }
}

/** The app's one connection. Pages import this. */
export const octave = new Octave()
