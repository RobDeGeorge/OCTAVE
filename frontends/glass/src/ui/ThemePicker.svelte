<script lang="ts">
  // The app's themes as palette tiles (themeCatalog.themes), the active one
  // marked, saved through settingsManager.save_theme_setting. On an app
  // without themeCatalog the names come from the spec's list instead.
  import { octave, type Json } from '../lib/octave'
  import { act } from '../lib/stores'
  import { themeNames } from '../lib/settings-spec'
  import { tint } from '../lib/theme'
  import Toggle from './Toggle.svelte'

  interface Palette { base: string; baseAlt: string; accent: string; text?: { primary?: string; secondary?: string } }
  const themes = octave.prop<Record<string, Palette>>('themeCatalog', 'themes', {})
  const names = octave.prop<string[]>('themeCatalog', 'themeNames', [])
  const lightNames = octave.prop<string[]>('themeCatalog', 'lightThemeNames', [])
  const activeName = octave.prop<string>('themeCatalog', 'activeThemeName', '')
  const setting = octave.prop<string>('settingsManager', 'themeSetting', '')
  const list = $derived($names.length ? $names : themeNames)
  function pretty(n: string) { return n.replace(/([a-z])([A-Z])/g, '$1 $2') }
</script>

<div class="tiles" role="radiogroup" aria-label="Theme">
  {#each list as n}
    {@const p = $themes[n]}
    <button class="tile" class:on={$setting === n} role="radio" aria-checked={$setting === n} onclick={() => act(octave.call('settingsManager', 'save_theme_setting', n))}>
      {#if p}
        <span class="pal" style="background:{p.base}">
          <span class="alt" style="background:{p.baseAlt}"></span>
          <span class="acc" style="background:{p.accent}"></span>
          <span class="txt" style="background:{p.text?.primary ?? '#fff'}"></span>
        </span>
      {:else}
        <span class="pal plain"></span>
      {/if}
      <span class="name">{pretty(n)}</span>
      <span class="small faint">{$lightNames.includes(n) ? 'light' : $activeName === n ? 'in use' : ''}</span>
    </button>
  {/each}
</div>
<div class="row">
  <div class="label"><span>Tint this page from the theme</span><span class="small">Use the active theme's accent colour here too</span></div>
  <Toggle on={$tint} label="Tint from the theme" onchange={(v) => tint.set(v)} />
</div>

<style>
  .tiles { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 10px; }
  .tile { display: flex; flex-direction: column; gap: 6px; padding: 10px; border-radius: 16px; background: var(--control); text-align: left; border: 3px solid transparent; }
  .tile.on { border-color: var(--accent); }
  .pal { display: block; position: relative; height: 56px; border-radius: 10px; overflow: hidden; }
  .pal.plain { background: var(--track); }
  .alt { position: absolute; left: 0; bottom: 0; width: 100%; height: 40%; }
  .acc { position: absolute; right: 10px; top: 10px; width: 22px; height: 22px; border-radius: 50%; }
  .txt { position: absolute; left: 10px; top: 14px; width: 36px; height: 6px; border-radius: 3px; }
  .name { font-weight: 600; font-size: 15px; }
</style>
