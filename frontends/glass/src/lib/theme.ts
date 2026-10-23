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
