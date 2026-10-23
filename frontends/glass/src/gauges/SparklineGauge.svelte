<script lang="ts">
  let { value, min = 0, max = 100, title = '', unit = '', decimals = 0, color = '', autoScale = false, fillBelow = true, showHeader = true, maxSamples = 60, sampleIntervalMs = 500 }:
    { value: number | undefined; min?: number; max?: number; title?: string; unit?: string; decimals?: number; color?: string; autoScale?: boolean; fillBelow?: boolean; showHeader?: boolean; maxSamples?: number; sampleIntervalMs?: number } = $props()
  let samples = $state<number[]>([])
  let latest: number | undefined
  $effect(() => { latest = value })
  $effect(() => {
    const t = setInterval(() => { if (latest !== undefined) samples = [...samples, latest].slice(-maxSamples) }, sampleIntervalMs)
    return () => clearInterval(t)
  })
  const W = 100, H = 40
  const range = $derived.by(() => {
    if (!autoScale || samples.length < 2) return [min, max]
    let lo = Math.min(...samples), hi = Math.max(...samples); if (lo === hi) { lo -= 1; hi += 1 }
    return [lo, hi]
  })
  const pts = $derived(samples.map((v, k) => [((k) / Math.max(1, maxSamples - 1)) * W, H - ((v - range[0]) / ((range[1] - range[0]) || 1)) * H]))
  const line = $derived(pts.map(([x, y], k) => `${k ? 'L' : 'M'}${x.toFixed(1)} ${y.toFixed(1)}`).join(''))
  const area = $derived(pts.length > 1 ? `${line}L${pts[pts.length - 1][0].toFixed(1)} ${H}L${pts[0][0].toFixed(1)} ${H}Z` : '')
</script>

<div class="sp" style:--c={color || 'var(--accent)'}>
  {#if showHeader}<div class="head"><span class="t">{title}</span><span class="v num">{value === undefined ? '–' : value.toFixed(decimals)}<span class="u">{unit}</span></span></div>{/if}
  <svg viewBox="0 0 {W} {H}" preserveAspectRatio="none">{#if fillBelow && pts.length > 1}<path class="area" d={area} />{/if}<path class="line" d={line} vector-effect="non-scaling-stroke" /></svg>
</div>

<style>
  .sp { width: 100%; height: 100%; display: flex; flex-direction: column; gap: 4px; }
  .head { display: flex; justify-content: space-between; align-items: baseline; }
  .t { font-size: 14px; color: var(--ink-2); font-weight: 600; } .v { font-size: 20px; font-weight: 700; } .u { font-size: 12px; color: var(--ink-2); margin-left: 4px; }
  svg { flex: 1; width: 100%; min-height: 0; }
  .area { fill: var(--c); opacity: .18; }
  .line { fill: none; stroke: var(--c); stroke-width: 2; stroke-linejoin: round; }
</style>
