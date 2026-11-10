<script lang="ts">
  // One series over time: a 2px line, the per-bucket low/high band under
  // it, a recessive grid, and a crosshair tooltip on hover or touch.
  import { mmss } from '../lib/stores'
  interface P { t: number; v: number; lo: number; hi: number }
  let { points, unit = '', decimals = 0, title = '' }: { points: P[]; unit?: string; decimals?: number; title?: string } = $props()
  const W = 800, H = 260, L = 54, R = 16, T = 14, B = 30
  const ext = $derived.by(() => {
    if (!points.length) return { t0: 0, t1: 1, v0: 0, v1: 1 }
    let v0 = Infinity, v1 = -Infinity
    for (const p of points) { v0 = Math.min(v0, p.lo ?? p.v); v1 = Math.max(v1, p.hi ?? p.v) }
    if (v0 === v1) { v0 -= 1; v1 += 1 }
    const pad = (v1 - v0) * .08
    return { t0: points[0].t, t1: Math.max(points[points.length - 1].t, points[0].t + 1), v0: v0 - pad, v1: v1 + pad }
  })
  const x = (t: number) => L + ((t - ext.t0) / (ext.t1 - ext.t0)) * (W - L - R)
  const y = (v: number) => T + (1 - (v - ext.v0) / (ext.v1 - ext.v0)) * (H - T - B)
  const line = $derived(points.map((p, i) => `${i ? 'L' : 'M'}${x(p.t).toFixed(1)} ${y(p.v).toFixed(1)}`).join(''))
  const band = $derived(points.length ? `M${points.map((p) => `${x(p.t).toFixed(1)} ${y(p.hi).toFixed(1)}`).join('L')}L${points.slice().reverse().map((p) => `${x(p.t).toFixed(1)} ${y(p.lo).toFixed(1)}`).join('L')}Z` : '')
  const yTicks = $derived.by(() => { const n = 4; return Array.from({ length: n + 1 }, (_, i) => ext.v0 + ((ext.v1 - ext.v0) * i) / n) })
  const xTicks = $derived.by(() => { const n = 5; return Array.from({ length: n + 1 }, (_, i) => ext.t0 + ((ext.t1 - ext.t0) * i) / n) })
  let hover = $state<P | null>(null)
  function move(e: PointerEvent) {
    const svg = e.currentTarget as SVGSVGElement
    const r = svg.getBoundingClientRect()
    const t = ext.t0 + ((e.clientX - r.left) / r.width * W - L) / (W - L - R) * (ext.t1 - ext.t0)
    let best = points[0], d = Infinity
    for (const p of points) { const dd = Math.abs(p.t - t); if (dd < d) { d = dd; best = p } }
    hover = best ?? null
  }
</script>

<div class="chart">
  {#if !points.length}
    <p class="muted small">No samples for this reading.</p>
  {:else}
    <svg viewBox="0 0 {W} {H}" role="img" aria-label="{title} over the trip" onpointermove={move} onpointerleave={() => (hover = null)}>
      {#each yTicks as v}
        <line class="grid" x1={L} x2={W - R} y1={y(v)} y2={y(v)} />
        <text class="tick" x={L - 8} y={y(v) + 4} text-anchor="end">{v.toFixed(decimals)}</text>
      {/each}
      {#each xTicks as t}
        <text class="tick" x={x(t)} y={H - 8} text-anchor="middle">{mmss(t * 1000)}</text>
      {/each}
      <path class="band" d={band} />
      <path class="line" d={line} />
      {#if hover}
        <line class="cross" x1={x(hover.t)} x2={x(hover.t)} y1={T} y2={H - B} />
        <circle class="dot" cx={x(hover.t)} cy={y(hover.v)} r="5" />
      {/if}
    </svg>
    <div class="tip num small" class:show={hover}>
      {#if hover}{mmss(hover.t * 1000)} <strong>{hover.v.toFixed(decimals)} {unit}</strong>{#if hover.lo !== hover.hi} <span class="muted">{hover.lo.toFixed(decimals)} to {hover.hi.toFixed(decimals)}</span>{/if}{:else}&nbsp;{/if}
    </div>
  {/if}
</div>

<style>
  .chart { width: 100%; }
  svg { width: 100%; height: auto; display: block; touch-action: none; }
  .grid { stroke: var(--glass-edge); stroke-width: 1; }
  .tick { fill: var(--ink-3); font-size: 12px; font-variant-numeric: tabular-nums; }
  .band { fill: var(--accent); opacity: .16; }
  .line { fill: none; stroke: var(--accent); stroke-width: 2; stroke-linejoin: round; stroke-linecap: round; }
  .cross { stroke: var(--ink-2); stroke-width: 1; stroke-dasharray: 3 3; }
  .dot { fill: var(--accent); stroke: var(--bg); stroke-width: 2; }
  .tip { min-height: 22px; padding: 2px 8px; }
</style>
