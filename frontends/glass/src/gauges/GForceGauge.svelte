<script lang="ts">
  let { lateral = 0, longitudinal = 0, maxG = 1.5, color = '', showTrail = true, showValue = true }:
    { lateral?: number; longitudinal?: number; maxG?: number; color?: string; showTrail?: boolean; showValue?: boolean } = $props()
  const x = $derived(50 + (Math.max(-maxG, Math.min(maxG, lateral)) / maxG) * 42)
  const y = $derived(50 - (Math.max(-maxG, Math.min(maxG, longitudinal)) / maxG) * 42)
  let trail = $state<[number, number][]>([])
  $effect(() => { const p: [number, number] = [x, y]; trail = [...trail, p].slice(-25) })
  const mag = $derived(Math.hypot(lateral, longitudinal))
</script>

<div class="gf" style:--c={color || 'var(--accent)'}>
  <svg viewBox="0 0 100 100" aria-label="G-force {mag.toFixed(2)} g">
    {#each [1, .66, .33] as r}<circle class="ring" cx="50" cy="50" r={42 * r} />{/each}
    <line class="ring" x1="8" y1="50" x2="92" y2="50" /><line class="ring" x1="50" y1="8" x2="50" y2="92" />
    {#if showTrail}<polyline class="trail" points={trail.map((p) => p.join(',')).join(' ')} />{/if}
    <circle class="dot" cx={x} cy={y} r="4.5" />
    {#if showValue}<text class="v" x="50" y="99" text-anchor="middle">{mag.toFixed(2)} g</text>{/if}
  </svg>
</div>

<style>
  .gf { width: 100%; height: 100%; display: flex; align-items: center; justify-content: center; }
  svg { width: 100%; height: 100%; overflow: visible; }
  .ring { fill: none; stroke: var(--track); stroke-width: 1; }
  .trail { fill: none; stroke: var(--c); stroke-width: 1.2; opacity: .5; }
  .dot { fill: var(--c); }
  .v { fill: var(--ink-2); font-size: 7px; font-weight: 700; }
</style>
