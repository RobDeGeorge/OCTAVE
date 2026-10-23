import { writable } from 'svelte/store'

export type Theme = 'auto' | 'light' | 'dark'
function load(): Theme {
  const q = new URLSearchParams(location.search).get('theme')
  if (q === 'light' || q === 'dark' || q === 'auto') return q
  try { return (localStorage.getItem('glass.theme') as Theme) || 'auto' } catch { return 'auto' }
}
export const theme = writable<Theme>(load())
theme.subscribe((t) => {
  try { localStorage.setItem('glass.theme', t) } catch { /* private window */ }
  if (t === 'auto') delete document.documentElement.dataset.theme
  else document.documentElement.dataset.theme = t
})

/** Tint this page's accent from the app's active theme (themeCatalog.activeTheme). */
export const tint = writable<boolean>((() => { try { return localStorage.getItem('glass.tint') === '1' } catch { return false } })())
tint.subscribe((on) => { try { localStorage.setItem('glass.tint', on ? '1' : '0') } catch { /* private window */ } })
export function applyAccent(accent: string | null): void {
  const root = document.documentElement
  if (accent && /^#[0-9a-f]{6}$/i.test(accent)) root.style.setProperty('--accent', accent)
  else root.style.removeProperty('--accent')
}
