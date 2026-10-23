<script lang="ts">
  // The Now Playing presets (settingsManager.nowPlayingPresets): one tap
  // applies a bundle of media settings through applyNowPlayingPreset. The
  // preset whose every setting matches the current values is marked.
  import { octave, type Json } from '../lib/octave'
  import { act, toast } from '../lib/stores'
  interface Preset { label: string; settings: Record<string, Json> }
  let { values }: { values: Record<string, Json> } = $props()
  const presets = octave.prop<Preset[]>('settingsManager', 'nowPlayingPresets', [])
  function matches(p: Preset): boolean {
    const keys = Object.keys(p.settings)
    return keys.length > 0 && keys.every((k) => k in values && values[k] === p.settings[k])
  }
  async function apply(p: Preset) {
    const ok = await act(octave.call<boolean>('settingsManager', 'applyNowPlayingPreset', p.label))
    toast(ok ? `${p.label} applied` : `Could not apply ${p.label}`, ok ? 'ok' : 'bad')
  }
</script>

{#if $presets.length}
  <div class="row stack">
    <div class="label"><span>Presets</span><span class="small">A whole look for the Now Playing page in one tap</span></div>
    <div class="wrap" role="group" aria-label="Now Playing presets">
      {#each $presets as p}
        <button class="btn" class:on={matches(p)} onclick={() => apply(p)}>{p.label}</button>
      {/each}
    </div>
  </div>
{/if}

<style>
  .row.stack { flex-direction: column; align-items: stretch; gap: 8px; }
  .wrap { display: flex; flex-wrap: wrap; gap: 8px; }
</style>
