<script lang="ts">
  // A centre-zero bar for bidirectional readings (trims, g, pitch).
  let { value, min = -1, max = 1, title = '', unit = '', decimals = 1, color = '', showTicks = true, majorTicks = 5 }:
    { value: number | undefined; min?: number; max?: number; title?: string; unit?: string; decimals?: number; color?: string; showTicks?: boolean; majorTicks?: number } = $props()
  const zero = $derived(Math.min(1, Math.max(0, (0 - min) / ((max - min) || 1))))
  const f = $derived(value === undefined ? zero : Math.min(1, Math.max(0, (value - min) / ((max - min) || 1))))
  const left = $derived(Math.min(zero, f)), width = $derived(Math.abs(f - zero))
  const ticks = $derived(Array.from({ length: Math.max(2, majorTicks) }, (_, k) => k / (Math.max(2, majorTicks) - 1)))
</script>

<div class="lin" style:--c={color || 'var(--accent)'}>
  <div class="head"><span class="t">{title}</span><span class="v num">{value === undefined ? '–' : value.toFixed(decimals)}<span class="u">{unit}</span></span></div>
  <div class="track">
    <div class="fill" style="left:{left * 100}%;width:{width * 100}%"></div>
    <div class="zero" style="left:{zero * 100}%"></div>
  </div>
  {#if showTicks}<div class="ticks num">{#each ticks as t}<span style="left:{t * 100}%">{(min + (max - min) * t).toFixed(decimals > 1 ? 1 : decimals)}</span>{/each}</div>{/if}
</div>

<style>
  .lin { width: 100%; height: 100%; display: flex; flex-direction: column; gap: 6px; justify-content: center; }
  .head { display: flex; justify-content: space-between; align-items: baseline; }
  .t { font-size: 14px; color: var(--ink-2); font-weight: 600; } .v { font-size: 22px; font-weight: 700; } .u { font-size: 12px; color: var(--ink-2); margin-left: 4px; }
  .track { position: relative; height: 14px; border-radius: 7px; background: var(--track); overflow: hidden; }
  .fill { position: absolute; top: 0; bottom: 0; background: var(--c); transition: left .12s linear, width .12s linear; }
  .zero { position: absolute; top: 0; bottom: 0; width: 2px; background: var(--ink-2); transform: translateX(-1px); }
  .ticks { position: relative; height: 14px; font-size: 11px; color: var(--ink-3); }
  .ticks span { position: absolute; transform: translateX(-50%); }
</style>
