<script lang="ts">
  // Which readings the home page shows, in order.
  import { octave } from '../lib/octave'
  import { act, catalog, catalogOrder, obd } from '../lib/stores'
  import Icon from './Icon.svelte'
  import Modal from './Modal.svelte'
  const ids = octave.prop<string[]>('settingsManager', 'homeOBDParameters', [])
  const max = octave.prop<number>('settingsManager', 'maxHomeObdParameters', 4)
  let picking = $state(false)
  let q = $state('')
  const candidates = $derived($catalogOrder.filter((id) => !$ids.includes(id) && ($obd.vehicleSupportedParameters.length === 0 || $obd.vehicleSupportedParameters.includes(id) || $catalog.get(id)?.source !== 'obd'))
    .filter((id) => !q || ($catalog.get(id)?.title ?? id).toLowerCase().includes(q.toLowerCase())))
</script>

<div class="list">
  <div class="muted small">Home gauges, up to {$max}</div>
  {#each $ids as id, i (id)}
    <div class="row">
      <div class="label"><span>{$catalog.get(id)?.title ?? id}</span><span class="small num">{id}</span></div>
      <div class="acts">
        <button class="btn icon" disabled={i === 0} aria-label="Move up" onclick={() => act(octave.call('settingsManager', 'move_home_obd_parameter', i, i - 1))}><Icon name="up" /></button>
        <button class="btn icon" disabled={i === $ids.length - 1} aria-label="Move down" onclick={() => act(octave.call('settingsManager', 'move_home_obd_parameter', i, i + 1))}><Icon name="down" /></button>
        <button class="btn icon danger" aria-label="Remove" onclick={() => act(octave.call('settingsManager', 'remove_home_obd_parameter', id))}><Icon name="x" /></button>
      </div>
    </div>
  {/each}
  <button class="btn" disabled={$ids.length >= $max} onclick={() => (picking = true)}><Icon name="plus" />Add a gauge</button>
</div>

{#if picking}
  <Modal title="Add a home gauge" onclose={() => (picking = false)}>
    <input class="field" placeholder="Find a reading" bind:value={q} />
    <div class="pick">
      {#each candidates as id}
        <button class="btn" onclick={() => { act(octave.call('settingsManager', 'add_home_obd_parameter', id)); picking = false }}>{$catalog.get(id)?.title ?? id}</button>
      {/each}
    </div>
  </Modal>
{/if}

<style>
  .list { display: flex; flex-direction: column; gap: 6px; }
  .acts { display: flex; gap: 6px; }
  .pick { display: flex; flex-wrap: wrap; gap: 8px; }
</style>
