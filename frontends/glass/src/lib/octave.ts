// Glass's connection to OCTAVE: the octave-client package (sdk/typescript)
// with its subscriptions wrapped as Svelte stores. Pages import `octave`.
import { readable, writable, type Readable, type Writable } from 'svelte/store'
import { OctaveClient, defaultUrl as sdkDefaultUrl, type Json, type Status, type Hello, type Description, type ServiceEvent, type StreamHandlers, type StreamInfo } from 'octave-client'

export { OctaveError, PROTOCOL_VERSION } from 'octave-client'
export type { Json, Status, Hello, Description, ServiceEvent, StreamHandlers, StreamInfo, MediaRecord, ModelChange } from 'octave-client'

/** Where the service is for this page: dev-proxied under /ws, or the page's own host. */
export function defaultUrl(): string {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
  const host = location.host || '127.0.0.1:9876'
  return import.meta.env.DEV ? `${proto}//${host}/ws` : sdkDefaultUrl()
}

export class Octave {
  readonly client: OctaveClient
  readonly status: Writable<Status> = writable('connecting')
  readonly hello: Writable<Hello | null> = writable(null)

  constructor(url: string = defaultUrl()) {
    this.client = new OctaveClient({ url, checkVersion: false })
    this.client.on('status', (s) => this.status.set(s))
    this.client.on('hello', (h) => {
      this.hello.set(h)
      // loopback is open under protocol 2 as under 1; anything else is a page / app mismatch
      if (![1, 2].includes(h.protocolVersion)) this.status.set('mismatch')
    })
  }

  connect(): void { this.client.connect() }
  close(): void { this.client.close() }
  whenOnline(ms?: number): Promise<void> { return this.client.whenOnline(ms) }
  request(op: string, args?: Record<string, unknown>): Promise<Json> { return this.client.request(op, args) }
  get<T = Json>(object: string, property?: string): Promise<T> { return this.client.get<T>(object, property) }
  set<T = Json>(object: string, property: string, value: unknown): Promise<T> { return this.client.set<T>(object, property, value) }
  call<T = Json>(object: string, method: string, ...args: unknown[]): Promise<T> { return this.client.call<T>(object, method, ...args) }
  rows<T = Record<string, Json>>(object: string, property: string, from = 0, count?: number): Promise<T[]> { return this.client.rows<T>(object, property, from, count) }
  describe(object: string): Promise<Description> { return this.client.describe(object) }
  stream(object: string, name: string, handlers: StreamHandlers): Promise<{ info: StreamInfo; close: () => void }> { return this.client.stream(object, name, handlers) }

  /** One property as a store; the service sends the current value at once. */
  prop<T = Json>(object: string, name: string, initial: T, throttleMs?: number): Readable<T> {
    return readable(initial, (set) => this.client.property<T>(object, name, set, throttleMs))
  }

  /** Several properties of one object as one store of {name: value}. */
  props<T extends Record<string, Json>>(object: string, names: (keyof T & string)[], initial: T, throttleMs?: number): Readable<T> {
    return readable(initial, (_set, update) =>
      this.client.subscribe(object, { properties: names, throttleMs }, (e) => { if (e.event === 'property') update((v) => ({ ...v, [e.name]: e.value })) }))
  }

  /** A signal: the handler gets its arguments. Returns the unsubscribe. */
  signal(object: string, name: string, handler: (...args: Json[]) => void): () => void {
    return this.client.signal(object, name, handler)
  }

  /** Live paramStore values (native units) for a set of ids, as {id: value}. */
  params(ids: string[], throttleMs = 50): Readable<Record<string, number>> {
    return readable<Record<string, number>>({}, (_set, update) => {
      if (ids.length === 0) return
      return this.client.params(ids, (values) => update((v) => {
        const n = { ...v }
        for (const [k, x] of Object.entries(values)) { if (x === null) delete n[k]; else n[k] = x }
        return n
      }), throttleMs)
    })
  }

  /** A list-model property as a store of rows, kept current from model events. */
  model<T = Record<string, Json>>(object: string, name: string): Readable<T[]> {
    return readable<T[]>([], (set) => this.client.model<T>(object, name, set))
  }
}

/** The app's one connection. Pages import this. */
export const octave = new Octave()
