<script lang="ts">
  import Icon from './Icon.svelte'
  import { route, go } from '../lib/router'
  import { octave } from '../lib/octave'
  import { theme } from '../lib/theme'

  const items = [
    { id: 'home', label: 'Home', icon: 'home' },
    { id: 'media', label: 'Media', icon: 'music' },
    { id: 'obd', label: 'Vehicle', icon: 'gauge' },
    { id: 'dashboards', label: 'Dashboards', icon: 'grid' },
    { id: 'trips', label: 'Trips', icon: 'route' },
    { id: 'settings', label: 'Settings', icon: 'sliders' },
    { id: 'more', label: 'More', icon: 'more' },
  ]
  const status = octave.status
  function cycleTheme() {
    theme.update((t) => (t === 'auto' ? 'dark' : t === 'dark' ? 'light' : 'auto'))
  }
</script>

<nav class="glass strong" aria-label="Pages">
  <div class="brand" title={$status}>
    <span class="dot" class:on={$status === 'online'}></span>
    <span class="word">OCTAVE</span>
  </div>
  <div class="items">
    {#each items as it}
      <button class="item" class:on={$route.page === it.id} onclick={() => go(it.id)} aria-current={$route.page === it.id ? 'page' : undefined}>
        <Icon name={it.icon} size={26} />
        <span>{it.label}</span>
      </button>
    {/each}
  </div>
  <button class="item theme" onclick={cycleTheme} aria-label="Theme: {$theme}">
    <Icon name={$theme === 'light' ? 'sun' : $theme === 'dark' ? 'moon' : 'clock'} size={24} />
    <span>{$theme === 'auto' ? 'Auto' : $theme === 'dark' ? 'Dark' : 'Light'}</span>
  </button>
</nav>

<style>
  nav { position: fixed; z-index: 10; display: flex; flex-direction: column; align-items: stretch;
        left: var(--gap); top: var(--gap); bottom: var(--gap); width: var(--rail); padding: 14px 8px; gap: 6px; }
  .brand { display: flex; flex-direction: column; align-items: center; gap: 6px; padding: 6px 0 14px; font-size: 11px;
           letter-spacing: .18em; color: var(--ink-2); font-weight: 700; }
  .dot { width: 10px; height: 10px; border-radius: 50%; background: var(--bad); box-shadow: 0 0 10px var(--bad); transition: background .3s, box-shadow .3s; }
  .dot.on { background: var(--good); box-shadow: 0 0 10px var(--good); }
  .items { display: flex; flex-direction: column; gap: 4px; flex: 1; }
  .item { display: flex; flex-direction: column; align-items: center; gap: 5px; padding: 10px 4px; border-radius: 18px;
          color: var(--ink-2); font-size: 12px; font-weight: 600; transition: background .2s var(--ease), color .2s; }
  .item.on { background: var(--control-on); color: var(--control-on-ink); }
  .item:not(.on):hover { background: var(--control); }
  .theme { margin-top: auto; }

  @media (max-width: 760px), (max-aspect-ratio: 1/1) {
    nav { left: var(--gap); right: var(--gap); top: auto; bottom: var(--gap); width: auto; height: var(--tab);
          flex-direction: row; align-items: center; padding: 6px 8px; }
    .brand, .word { display: none; }
    .items { flex-direction: row; justify-content: space-around; }
    .item { flex: 1; padding: 6px 2px; gap: 3px; font-size: 10px; border-radius: 14px; }
    .theme { display: none; }
  }
</style>
