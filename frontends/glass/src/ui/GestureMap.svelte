<script lang="ts">
  import { octave, type Json } from '../lib/octave'
  import { act } from '../lib/stores'
  const gestures = octave.prop<string[]>('gestureSensor', 'gestures', [])
  const actions = octave.prop<{ key: string; label: string }[]>('gestureSensor', 'actions', [])
  const status = octave.prop<string>('gestureSensor', 'connectionStatus', '')
  let mapping = $state<Record<string, string>>({})
  async function load() { mapping = (await octave.call<Record<string, string>>('settingsManager', 'get_gesture_mapping_dict')) ?? {} }
  $effect(() => octave.signal('settingsManager', 'gestureMappingChanged', load))
  $effect(() => { const u = octave.status.subscribe((s) => { if (s === 'online') load() }); return u })
  function pretty(g: string) { return g.toLowerCase().replace(/-/g, ' ').replace(/^./, (c) => c.toUpperCase()) }
</script>

<p class="muted small">Sensor: {$status || 'unknown'}</p>
{#each $gestures as g}
  <div class="row">
    <div class="label"><span>{pretty(g)}</span></div>
    <select class="field sel" aria-label={pretty(g)} value={mapping[g] ?? 'none'} onchange={(e) => act(octave.call('settingsManager', 'save_gesture_action', g, (e.target as HTMLSelectElement).value))}>
      {#each $actions as a}<option value={a.key}>{a.label}</option>{/each}
    </select>
  </div>
{/each}
<button class="btn" onclick={() => act(octave.call('settingsManager', 'reset_gesture_mapping'), 'Gestures reset')}>Reset to defaults</button>

<style> .sel { max-width: 240px; } </style>
