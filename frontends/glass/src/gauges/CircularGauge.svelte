<script lang="ts">
  let { value, min = 0, max = 100, title = '', unit = '', decimals = 0, redline = NaN, color = '', needleColor = '', showNeedle = false,
        showTicks = true, showReadout = true, majorTicks = 9, minorPerMajor = 5, startAngle = 135, sweepAngle = 270 }:
    { value: number | undefined; min?: number; max?: number; title?: string; unit?: string; decimals?: number; redline?: number; color?: string; needleColor?: string;
      showNeedle?: boolean; showTicks?: boolean; showReadout?: boolean; majorTicks?: number; minorPerMajor?: number; startAngle?: number; sweepAngle?: number } = $props()
  const r = 44, cx = 50, cy = 50
  const frac = $derived(value === undefined ? 0 : Math.min(1, Math.max(0, (value - min) / ((max - min) || 1))))
  const pt = (f: number, rad: number) => { const a = ((startAngle + sweepAngle * f) * Math.PI) / 180; return [cx + rad * Math.cos(a), cy + rad * Math.sin(a)] }
  function arc(f0: number, f1: number) {
    if (f1 - f0 >= 0.9999) { const [x0, y0] = pt(f0, r), [xm, ym] = pt(f0 + .5, r); return `M${x0} ${y0}A${r} ${r} 0 1 1 ${xm} ${ym}A${r} ${r} 0 1 1 ${x0} ${y0}` }
    const [x0, y0] = pt(f0, r), [x1, y1] = pt(f1, r)
    return `M${x0} ${y0}A${r} ${r} 0 ${sweepAngle * (f1 - f0) > 180 ? 1 : 0} 1 ${x1} ${y1}`
  }
  const redFrac = $derived(Number.isFinite(redline) ? Math.min(1, Math.max(0, (redline - min) / ((max - min) || 1))) : NaN)
  const hot = $derived(Number.isFinite(redFrac) && frac >= redFrac)
  const majors = $derived(Array.from({ length: Math.max(2, majorTicks) }, (_, k) => k / (Math.max(2, majorTicks) - 1)))
  const minors = $derived.by(() => { const n = (Math.max(2, majorTicks) - 1) * Math.max(0, minorPerMajor); return n ? Array.from({ length: n + 1 }, (_, k) => k / n).filter((f) => !majors.some((m) => Math.abs(m - f) < 1e-6)) : [] })
  const needle = $derived(pt(frac, r - 6))
</script>

<div class="g" style:--c={color || 'var(--accent)'} style:--n={needleColor || 'var(--ink)'}>
  <svg viewBox="0 0 100 100" aria-label="{title} {value ?? ''} {unit}">
    <path class="track" d={arc(0, 1)} />
    {#if Number.isFinite(redFrac) && redFrac < 1}<path class="red" d={arc(redFrac, 1)} />{/if}
    {#if frac > 0 && !showNeedle}<path class="fill" class:hot d={arc(0, frac)} />{/if}
    {#if showTicks}
      {#each minors as f}{@const [x0, y0] = pt(f, r - 6)}{@const [x1, y1] = pt(f, r - 8)}<line class="minor" x1={x0} y1={y0} x2={x1} y2={y1} />{/each}
      {#each majors as f}{@const [x0, y0] = pt(f, r - 6)}{@const [x1, y1] = pt(f, r - 11)}<line class="major" x1={x0} y1={y0} x2={x1} y2={y1} />
        {@const [tx, ty] = pt(f, r - 17)}{#if !showNeedle || (f > 0.05 && f < 0.95)}<text class="tl" x={tx} y={ty + 2} text-anchor="middle">{Math.round(min + (max - min) * f)}</text>{/if}{/each}
    {/if}
    {#if showNeedle}
      <line class="needle" class:hot x1={cx} y1={cy} x2={needle[0]} y2={needle[1]} />
      <circle class="hub" cx={cx} cy={cy} r="3.5" />
    {/if}
    {#if showReadout}
      <text class="value" x="50" y={showNeedle ? 80 : 55} text-anchor="middle">{value === undefined ? '–' : value.toFixed(decimals)}</text>
      <text class="unit" x="50" y={showNeedle ? 88 : 65} text-anchor="middle">{unit}</text>
    {/if}
    <text class="title" x="50" y="96" text-anchor="middle">{title}</text>
  </svg>
</div>

<style>
  .g { width: 100%; height: 100%; display: flex; align-items: center; justify-content: center; }
  svg { width: 100%; height: 100%; overflow: visible; }
  path { fill: none; stroke-width: 6; stroke-linecap: round; }
  .track { stroke: var(--track); } .red { stroke: color-mix(in oklab, var(--bad) 45%, transparent); }
  .fill { stroke: var(--c); } .fill.hot { stroke: var(--bad); }
  .minor { stroke: var(--ink-3); stroke-width: .8; } .major { stroke: var(--ink-2); stroke-width: 1.4; }
  .tl { fill: var(--ink-3); font-size: 5px; font-weight: 600; }
  .needle { stroke: var(--n); stroke-width: 2.2; stroke-linecap: round; transition: x2 .1s linear, y2 .1s linear; } .needle.hot { stroke: var(--bad); }
  .hub { fill: var(--n); }
  .value { fill: var(--ink); font-size: 17px; font-weight: 700; letter-spacing: -.02em; }
  .unit { fill: var(--ink-2); font-size: 6px; font-weight: 600; }
  .title { fill: var(--ink-2); font-size: 6.5px; font-weight: 600; }
</style>
