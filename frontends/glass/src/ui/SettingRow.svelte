<script lang="ts">
  // One setting: label, hint, and the control for its kind.
  import type { Item } from '../lib/settings-spec'
  import { octave, type Json } from '../lib/octave'
  import { act } from '../lib/stores'
  import Toggle from './Toggle.svelte'
  import Slider from './Slider.svelte'

  let { item, value, options = [] }: { item: Item; value: Json; options?: { value: string | number | boolean; label: string }[] } = $props()
  function save(v: unknown) { act(octave.call('settingsManager', item.slot, v)) }
  let text = $state('')
  $effect(() => { text = value == null ? '' : String(value) })
  const decimals = $derived(item.step && item.step < 1 ? String(item.step).split('.')[1].length : 0)
</script>

<div class="row" class:stack={item.kind === 'slider' || item.kind === 'chips'}>
  <div class="label">
    <span>{item.label}</span>
    {#if item.hint}<span class="small">{item.hint}</span>{/if}
  </div>
  <div class="ctl">
    {#if item.kind === 'toggle'}
      <Toggle on={Boolean(value)} label={item.label} onchange={save} />
    {:else if item.kind === 'slider'}
      <Slider value={Number(value ?? item.min ?? 0)} min={item.min} max={item.max} step={item.step} label={item.label}
              format={(v) => `${v.toFixed(decimals)}${item.unit ? ' ' + item.unit : ''}`}
              onchange={(v) => save(item.float ? v : Math.round(v))} />
    {:else if item.kind === 'chips'}
      <div class="seg" role="radiogroup" aria-label={item.label}>
        {#each options as o}
          <button class:on={o.value === value} role="radio" aria-checked={o.value === value} onclick={() => save(o.value)}>{o.label}</button>
        {/each}
      </div>
    {:else if item.kind === 'number'}
      <div class="numfield">
        <input class="field num" type="number" bind:value={text} aria-label={item.label} step={item.float ? 'any' : 1}
               onchange={() => save(item.float ? Number(text) : Math.round(Number(text)))} />
        {#if item.unit}<span class="muted">{item.unit}</span>{/if}
      </div>
    {:else}
      <input class="field" type="text" bind:value={text} aria-label={item.label} placeholder={item.hint}
             onchange={() => save(text)} onkeydown={(e) => e.key === 'Enter' && save(text)} />
    {/if}
  </div>
</div>

<style>
  .row.stack { flex-direction: column; align-items: stretch; gap: 8px; }
  .ctl { display: flex; align-items: center; justify-content: flex-end; min-width: 0; }
  .stack .ctl { justify-content: flex-start; }
  .row:not(.stack) .ctl { max-width: 60%; }
  .numfield { display: flex; align-items: center; gap: 10px; }
  .numfield input { width: 130px; text-align: right; }
  .seg { max-width: 100%; }
</style>
