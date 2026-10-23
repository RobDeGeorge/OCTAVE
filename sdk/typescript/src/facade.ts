// The typed view of the API: `client.obj('volumeController').call('applyVolume', 40)`
// type-checks against the generated `Objects` (types.ts, from docs/api/).
import type { OctaveClient, Json, ServiceEvent } from './client.js'
import type { Objects } from './types.js'

export interface ObjectShape {
  properties: Record<string, unknown>
  methods: Record<string, { args: unknown[]; returns: unknown }>
  signals: Record<string, unknown[]>
}

type Props<O extends ObjectShape> = O['properties']
type Methods<O extends ObjectShape> = O['methods']
type Signals<O extends ObjectShape> = O['signals']

export class TypedObject<O extends ObjectShape> {
  constructor(private client: OctaveClient, readonly name: string) {}

  get<P extends keyof Props<O> & string>(property: P): Promise<Props<O>[P]> {
    return this.client.get(this.name, property) as Promise<Props<O>[P]>
  }
  set<P extends keyof Props<O> & string>(property: P, value: Props<O>[P]): Promise<Props<O>[P]> {
    return this.client.set(this.name, property, value) as Promise<Props<O>[P]>
  }
  call<M extends keyof Methods<O> & string>(method: M, ...args: Methods<O>[M]['args']): Promise<Methods<O>[M]['returns']> {
    return this.client.call(this.name, method, ...args) as Promise<Methods<O>[M]['returns']>
  }
  property<P extends keyof Props<O> & string>(property: P, cb: (value: Props<O>[P]) => void, throttleMs?: number): () => void {
    return this.client.property(this.name, property, cb as (v: Json) => void, throttleMs)
  }
  signal<S extends keyof Signals<O> & string>(signal: S, cb: (...args: Signals<O>[S]) => void): () => void {
    return this.client.signal(this.name, signal, cb as (...args: Json[]) => void)
  }
  subscribe(options: { properties?: (keyof Props<O> & string)[]; signals?: (keyof Signals<O> & string)[]; throttleMs?: number }, handler: (e: ServiceEvent) => void): () => void {
    return this.client.subscribe(this.name, options, handler)
  }
}

/** `obj('nowPlaying')` with every property, method and signal typed. */
export function obj<K extends keyof Objects & string>(client: OctaveClient, name: K): TypedObject<Objects[K]> {
  return new TypedObject<Objects[K]>(client, name)
}
