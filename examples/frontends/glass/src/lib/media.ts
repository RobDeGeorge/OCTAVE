// URLs for the app's HTTP routes (/art, /export/...), relative to the page's
// base so they work under --web-root and the dev proxy alike. Over the
// network (protocol 2) they need a media ticket; `ticket` is set by the
// pairing code and refreshed before it expires.
import { writable, get } from 'svelte/store'

export const ticket = writable<string>('')

export function mediaUrl(route: string, query: Record<string, string | number> = {}): string {
  const base = import.meta.env.BASE_URL.replace(/\/$/, '')
  const q = new URLSearchParams()
  for (const [k, v] of Object.entries(query)) q.set(k, String(v))
  const t = get(ticket)
  if (t) q.set('ticket', t)
  const qs = q.toString()
  return `${base}${route}${qs ? '?' + qs : ''}`
}
