<script lang="ts">
  import { octave, type Json } from '../lib/octave'
  import { act } from '../lib/stores'
  import Toggle from './Toggle.svelte'
  const connected = octave.prop<boolean>('berryIMU', 'connected', false)
  const cards = octave.prop<Record<string, boolean>>('settingsManager', 'sensorCards', {})
</script>

<div class="row">
  <div class="label"><span>{$connected ? 'Sensor hub connected' : 'No sensor hub'}</span><span class="small">Level the car, then set the zero</span></div>
  <div class="acts">
    <button class="btn" disabled={!$connected} onclick={() => act(octave.call('berryIMU', 'calibrateTare'), 'Zero set')}>Set zero</button>
    <button class="btn" disabled={!$connected} onclick={() => act(octave.call('berryIMU', 'resetTare'), 'Zero reset')}>Reset</button>
  </div>
</div>
{#each Object.entries($cards) as [card, on]}
  <div class="row"><div class="label"><span>{card === 'GForce' ? 'G-force' : card} card</span></div>
    <Toggle {on} label="{card} card" onchange={(v) => act(octave.call('settingsManager', 'save_sensor_card_visible', card, v))} /></div>
{/each}

<style> .acts { display: flex; gap: 6px; } </style>
