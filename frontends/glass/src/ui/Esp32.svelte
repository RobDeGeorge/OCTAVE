<script lang="ts">
  import { octave, type Json } from '../lib/octave'
  import { act } from '../lib/stores'
  import Icon from './Icon.svelte'
  interface E { connected: boolean; connectionStatus: string; connectionDetail: string; availablePorts: { port: string; label: string; isEsp32S3: boolean }[]; [k: string]: Json }
  const e = octave.props<E>('esp32VolumeManager', ['connected', 'connectionStatus', 'connectionDetail', 'availablePorts'],
    { connected: false, connectionStatus: '', connectionDetail: '', availablePorts: [] })
  const port = octave.prop<string>('settingsManager', 'esp32VolumePort', '')
</script>

<div class="row">
  <div class="label"><span>{$e.connected ? 'Knob connected' : $e.connectionStatus || 'Not connected'}</span><span class="small">{$e.connectionDetail}</span></div>
  <div class="acts">
    {#if $e.connected}
      <button class="btn" onclick={() => act(octave.call('esp32VolumeManager', 'disconnect_device'))}>Disconnect</button>
    {:else}
      <button class="btn" onclick={() => act(octave.call('esp32VolumeManager', 'connect_device'))}>Connect</button>
    {/if}
    <button class="btn icon" aria-label="Refresh ports" onclick={() => act(octave.call('esp32VolumeManager', 'refresh_ports'))}><Icon name="refresh" /></button>
  </div>
</div>
<div class="row">
  <div class="label"><span>Serial port</span></div>
  <select class="field sel" aria-label="Serial port" value={$port} onchange={(ev) => act(octave.call('settingsManager', 'save_esp32_volume_port', (ev.target as HTMLSelectElement).value))}>
    <option value="">Not set</option>
    {#each $e.availablePorts as p}<option value={p.port}>{p.label}{p.isEsp32S3 ? ' (ESP32-S3)' : ''}</option>{/each}
    {#if $port && !$e.availablePorts.some((p) => p.port === $port)}<option value={$port}>{$port}</option>{/if}
  </select>
</div>

<style> .sel { max-width: 320px; } .acts { display: flex; gap: 6px; } </style>
