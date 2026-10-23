import { readable } from 'svelte/store'

export interface Route { page: string; arg: string }
function parse(): Route {
  const h = location.hash.replace(/^#\/?/, '')
  const [page, ...rest] = h.split('/')
  return { page: page || 'home', arg: rest.join('/') }
}
export const route = readable<Route>(parse(), (set) => {
  const on = () => set(parse())
  addEventListener('hashchange', on)
  return () => removeEventListener('hashchange', on)
})
export function go(page: string, arg = ''): void {
  location.hash = arg ? `#/${page}/${arg}` : `#/${page}`
}
