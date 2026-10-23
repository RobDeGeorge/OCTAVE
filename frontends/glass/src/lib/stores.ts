// Stores every page shares: one subscription each, however many pages use them.
import { derived, readable, writable, type Readable } from 'svelte/store'
import { octave, type Json } from './octave'
import { mediaUrl, ticket } from './media'

export interface NowPlaying {
  source: string; playing: boolean; hasTrack: boolean
  title: string; artist: string; album: string; artUrl: string; artRevision: number
  durationMs: number; positionMs: number
  canSeek: boolean; canShuffle: boolean; shuffled: boolean
  [k: string]: Json
}
export const nowPlaying = octave.props<NowPlaying>('nowPlaying',
  ['source', 'playing', 'hasTrack', 'title', 'artist', 'album', 'artUrl', 'artRevision', 'durationMs', 'positionMs', 'canSeek', 'canShuffle', 'shuffled'],
  { source: 'local', playing: false, hasTrack: false, title: '', artist: '', album: '', artUrl: '', artRevision: 0,
    durationMs: 0, positionMs: 0, canSeek: false, canShuffle: false, shuffled: false }, 250)

/** Position run forward between reports while playing, at ~10 Hz. */
export const position: Readable<number> = readable(0, (set) => {
  let base = 0, at = Date.now(), playing = false, duration = 0
  const unsub = nowPlaying.subscribe((np) => {
    base = np.positionMs; at = Date.now(); playing = np.playing; duration = np.durationMs
    set(base)
  })
  const t = setInterval(() => {
    if (!playing) return
    const p = base + (Date.now() - at)
    set(duration > 0 ? Math.min(p, duration) : p)
  }, 250)   /* 4 Hz is plenty for a seek bar, and each tick repaints a frosted panel */
  return () => { unsub(); clearInterval(t) }
})

export interface Volume { percent: number; muted: boolean; effectivelyMuted: boolean; [k: string]: Json }
export const volume = octave.props<Volume>('volumeController', ['percent', 'muted', 'effectivelyMuted'],
  { percent: 0, muted: false, effectivelyMuted: false })

export interface ObdState {
  connected: boolean; connectionState: string; connectionStatus: string; connectionDetail: string
  connectionProgress: number; vin: string; vehicleMake: string; vehicleModelYear: number
  vehicleSupportedParameters: string[]; vehicleScanComplete: boolean; [k: string]: Json
}
export const obd = octave.props<ObdState>('obdManager',
  ['connected', 'connectionState', 'connectionStatus', 'connectionDetail', 'connectionProgress', 'vin', 'vehicleMake',
   'vehicleModelYear', 'vehicleSupportedParameters', 'vehicleScanComplete'],
  { connected: false, connectionState: 'Disconnected', connectionStatus: '', connectionDetail: '', connectionProgress: 0,
    vin: '', vehicleMake: '', vehicleModelYear: 0, vehicleSupportedParameters: [], vehicleScanComplete: false })

export interface ParamInfo {
  id: string; title: string; shortTitle: string; unit: string; min: number; max: number
  decimals: number; kind: string; source: string; aliasOf: string
  displayScale: number; displayOffset: number
}
const displayParameters = octave.prop<ParamInfo[]>('parameterCatalog', 'displayParameters', [])
/** id -> catalog row, in the user's units. */
export const catalog: Readable<Map<string, ParamInfo>> = derived(displayParameters, (rows) => {
  const m = new Map<string, ParamInfo>()
  for (const r of rows) m.set(r.id, { ...r, displayScale: r.displayScale ?? 1, displayOffset: r.displayOffset ?? 0 })
  return m
})
export const catalogOrder: Readable<string[]> = derived(displayParameters, (rows) => rows.map((r) => r.id))

export function toDisplay(info: ParamInfo | undefined, native: number): number {
  return info ? native * info.displayScale + info.displayOffset : native
}
export function fmt(info: ParamInfo | undefined, native: number | undefined): string {
  if (native === undefined || native === null || Number.isNaN(native)) return '–'
  return toDisplay(info, native).toFixed(info?.decimals ?? 0)
}

/** The current cover, served by the app at GET /art; the revision busts the cache. '' when there is none. */
export const artSrc: Readable<string> = derived([nowPlaying, ticket], ([np, t]) => (np.hasTrack && np.artRevision > 0 ? mediaUrl('/art', t ? { rev: np.artRevision } : { rev: np.artRevision }) : ''))

export const albumColors = octave.prop<Record<string, Json>>('settingsManager', 'albumArtColors', {})

/** Clock text from the app, in the user's format. */
export const clock: Readable<string> = readable('', (set) => {
  octave.get<string>('clock', 'time').then(set).catch(() => {})
  return octave.signal('clock', 'timeChanged', (t) => set(String(t)))
})

export interface ShiftLight { active: boolean; lit: boolean; enabled: boolean; [k: string]: Json }
export const shiftLight = octave.props<ShiftLight>('shiftLightManager', ['active', 'lit', 'enabled'],
  { active: false, lit: false, enabled: false })

export interface TripLive { recording: boolean; currentTripElapsedS: number; currentSampleCount: number; [k: string]: Json }
export const tripLive = octave.props<TripLive>('tripRecorder', ['recording', 'currentTripElapsedS', 'currentSampleCount'],
  { recording: false, currentTripElapsedS: 0, currentSampleCount: 0 }, 500)

/** Toasts: short confirmations and errors. */
export interface Toast { id: number; text: string; kind: 'ok' | 'bad' }
export const toasts = writable<Toast[]>([])
let toastId = 0
export function toast(text: string, kind: Toast['kind'] = 'ok'): void {
  const id = ++toastId
  toasts.update((t) => [...t, { id, text, kind }])
  setTimeout(() => toasts.update((t) => t.filter((x) => x.id !== id)), 3200)
}
/** Run a call, toasting its failure. */
export async function act<T>(p: Promise<T>, done?: string): Promise<T | undefined> {
  try {
    const r = await p
    if (done) toast(done)
    return r
  } catch (e) {
    toast((e as Error).message, 'bad')
    return undefined
  }
}

export function mmss(ms: number): string {
  const s = Math.max(0, Math.round(ms / 1000))
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`
}
export function hms(totalS: number): string {
  const s = Math.max(0, Math.round(totalS))
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60)
  return h ? `${h}h ${String(m).padStart(2, '0')}m` : `${m}:${String(s % 60).padStart(2, '0')}`
}

/** The palette the app has in force (album art > custom > built-in). */
export const activeTheme = octave.prop<Record<string, Json>>('themeCatalog', 'activeTheme', {})

/** The waveform: 96 bar levels 0..8 from audioAnalyzer, while someone asks for them. */
export function levels(): Readable<number[]> {
  return readable<number[]>([], (set) => {
    octave.call('audioAnalyzer', 'setDemand', 'glass', true).catch(() => {})
    const unsub = octave.prop<number[]>('audioAnalyzer', 'levels', [], 50).subscribe(set)
    return () => { unsub(); octave.call('audioAnalyzer', 'setDemand', 'glass', false).catch(() => {}) }
  })
}
