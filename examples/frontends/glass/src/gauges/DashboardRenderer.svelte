<script lang="ts">
  // Renders a schema-1 spec as a CSS grid. Demand for its PIDs is
  // registered under one consumer name per dashboard and released on leave.
  import type { Spec } from '../lib/widgets'
  import { selfBound } from '../lib/widgets'
  import { octave } from '../lib/octave'
  import Widget from './Widget.svelte'

  let { spec, consumer = 'glass-dashboard', editing = false, selected = -1, onselect }: { spec: Spec; consumer?: string; editing?: boolean; selected?: number; onselect?: (i: number) => void } = $props()
  const cols = $derived(spec.gridColumns && spec.gridColumns > 0 ? spec.gridColumns : 12)
  const rows = $derived(spec.gridRows && spec.gridRows > 0 ? spec.gridRows : 6)
  const ids = $derived([...new Set(spec.cells.flatMap((c) => (c.paramId ? [c.paramId] : selfBound(c.type))))])
  const live = $derived(octave.params(ids, 60))
  $effect(() => {
    const list = ids
    octave.call('obdManager', 'setParameterDemand', consumer, list).catch(() => {})
    return () => { octave.call('obdManager', 'setParameterDemand', consumer, []).catch(() => {}) }
  })
</script>

<div class="dash" style="--cols:{cols};--rows:{rows};--m:{spec.margins ?? 0}px;--s:{spec.spacing ?? 12}px" class:editing>
  {#each spec.cells as cell, i (i)}
    <div class="cell glass" class:sel={editing && selected === i}
         style="grid-column:{cell.col + 1} / span {cell.colSpan ?? 1};grid-row:{cell.row + 1} / span {cell.rowSpan ?? 1}">
      <Widget {cell} live={$live} />
      {#if editing}<button class="hit" aria-label="Select {cell.type}" onclick={() => onselect?.(i)}></button>{/if}
    </div>
  {/each}
</div>

<style>
  .dash { display: grid; grid-template-columns: repeat(var(--cols), minmax(0, 1fr)); grid-template-rows: repeat(var(--rows), minmax(0, 1fr));
          gap: var(--s); padding: var(--m); width: 100%; height: 100%; min-height: 0; }
  .cell { position: relative; padding: 12px; min-width: 0; min-height: 0; overflow: hidden; display: flex; border-radius: var(--radius-s); }
  .hit { position: absolute; inset: 0; cursor: pointer; }
  .cell.sel { outline: 3px solid var(--accent); outline-offset: -3px; }
</style>
