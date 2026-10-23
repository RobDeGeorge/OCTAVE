// Pairing (protocol 2): over the network a browser must present a paired
// device's token. The token lives in localStorage; hello sends it; media
// URLs carry a short-lived ticket instead. On loopback nothing is needed.
import { derived, writable, get, type Readable } from 'svelte/store'
import { octave } from './octave'
import { ticket } from './media'

const KEY = 'glass.token'
export function storedToken(): string | undefined { try { return localStorage.getItem(KEY) || undefined } catch { return undefined } }
function storeToken(t: string | undefined) { try { t ? localStorage.setItem(KEY, t) : localStorage.removeItem(KEY) } catch { /* private window */ } }

export const paired = writable<boolean>(!!storedToken())
/** True while the connection is online but not allowed to use the service: show the pair screen. */
export const needsPairing: Readable<boolean> = derived([octave.status, octave.hello], ([s, h]) => s === 'online' && (h?.auth ?? 'trusted') !== 'trusted')

export async function pair(code: string, name: string): Promise<void> {
  const token = await octave.client.pair(code.trim(), name.trim() || 'Browser')
  storeToken(token)
  paired.set(true)
  // subscriptions issued while untrusted were refused: a fresh connection re-issues them
  octave.client.close(); octave.client.connect()
}

export async function forgetDevice(): Promise<void> {
  try { await octave.client.unpair() } catch { /* already gone */ }
  storeToken(undefined)
  octave.client.setToken(undefined)
  paired.set(false)
  ticket.set('')
  octave.client.close(); octave.client.connect()
}

/** Keep a media ticket while we are a paired (networked) client. */
let timer: ReturnType<typeof setTimeout> | null = null
async function refreshTicket() {
  if (timer) clearTimeout(timer)
  timer = null
  const h = octave.client.hello
  if (!storedToken() || !h || (h.auth ?? 'trusted') !== 'trusted') { ticket.set(''); return }
  try {
    const t = await octave.client.mediaTicket()
    ticket.set(t.ticket)
    timer = setTimeout(refreshTicket, Math.max(30, t.expiresIn - 60) * 1000)
  } catch { ticket.set('') }
}
octave.hello.subscribe((h) => {
  if (!h) return
  // a revoked or unknown token: forget it, the pair screen follows
  if ((h as { tokenRejected?: boolean }).tokenRejected) { storeToken(undefined); octave.client.setToken(undefined); paired.set(false) }
})
// the hello listener runs before the status turns online; refresh once it has
octave.status.subscribe((s) => { if (s === 'online') refreshTicket(); else { if (timer) clearTimeout(timer); timer = null } })
export const deviceName = `${/iPhone|iPad/.test(navigator.userAgent) ? 'iPhone' : /Android/.test(navigator.userAgent) ? 'Android' : 'Browser'} (${(navigator.platform || '').slice(0, 20) || 'web'})`
