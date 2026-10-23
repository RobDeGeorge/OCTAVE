<script lang="ts">
  // A 240° arc with a readout in the middle. Dashboard type "ArcGauge";
  // also the Home page's gauge. Values are in display units already.
  let { value, min = 0, max = 100, title = '', unit = '', decimals = 0, redline = NaN, color = '', showReadout = true,
        showTicks = true, ticks = 5 }:
    { value: number | undefined; min?: number; max?: number; title?: string; unit?: string; decimals?: number;
      redline?: number; color?: string; showReadout?: boolean; showTicks?: boolean; ticks?: number } = $props()

  const start = 150, sweep = 240, r = 42, cx = 50, cy = 54
  const frac = $derived(value === undefined ? 0 : Math.min(1, Math.max(0, (value - min) / ((max - min) || 1))))
  function pt(f: number, rad = r) {
    const a = ((start + sweep * f) * Math.PI) / 180
    return [cx + rad * Math.cos(a), cy + rad * Math.sin(a)]
  }
  function arc(f0: number, f1: number) {
    const [x0, y0] = pt(f0), [x1, y1] = pt(f1)
    const large = sweep * (f1 - f0) > 180 ? 1 : 0
    return `M${x0} ${y0}A${r} ${r} 0 ${large} 1 ${x1} ${y1}`
  }
  const redFrac = $derived(Number.isFinite(redline) ? Math.min(1, Math.max(0, (redline - min) / ((max - min) || 1))) : NaN)
  const inRed = $derived(Number.isFinite(redFrac) && frac >= redFrac)
  const tickList = $derived(Array.from({ length: Math.max(2, ticks) }, (_, i) => i / (Math.max(2, ticks) - 1)))
</script>

<div class="arc" style:--c={color || 'var(--accent)'}>
  <svg viewBox="0 0 100 100" aria-label="{title} {value ?? ''} {unit}">
    <path class="track" d={arc(0, 1)} />
    {#if Number.isFinite(redFrac) && redFrac < 1}
      <path class="red" d={arc(redFrac, 1)} />
    {/if}
    {#if frac > 0}
      <path class="fill" class:hot={inRed} d={arc(0, frac)} />
    {/if}
    {#if showTicks}
      {#each tickList as f}
        {@const [x0, y0] = pt(f, r - 7)}
        {@const [x1, y1] = pt(f, r - 10)}
        <line class="tick" x1={x0} y1={y0} x2={x1} y2={y1} />
      {/each}
    {/if}
    {#if showReadout}
      <text class="value num" x="50" y="58" text-anchor="middle">{value === undefined ? '–' : value.toFixed(decimals)}</text>
      <text class="unit" x="50" y="70" text-anchor="middle">{unit}</text>
    {/if}
    <text class="title" x="50" y="97" text-anchor="middle">{title}</text>
  </svg>
</div>

<style>
  .arc { width: 100%; height: 100%; display: flex; align-items: center; justify-content: center; container-type: size; }
  svg { width: 100%; height: 100%; max-height: 100%; overflow: visible; }
  path { fill: none; stroke-linecap: round; stroke-width: 7; }
  .track { stroke: var(--track); }
  .red { stroke: color-mix(in oklab, var(--bad) 45%, transparent); }
  .fill { stroke: var(--c); transition: d .12s linear; }
  .fill.hot { stroke: var(--bad); }
  .tick { stroke: var(--ink-3); stroke-width: 1.2; }
  .value { fill: var(--ink); font-size: 20px; font-weight: 700; letter-spacing: -.02em; }
  .unit { fill: var(--ink-2); font-size: 7px; font-weight: 600; }
  .title { fill: var(--ink-2); font-size: 7.5px; font-weight: 600; }
</style>
