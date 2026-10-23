<script lang="ts">
  let { value, min = 0, max = 100, title = '', unit = '', decimals = 0, warnAbove = NaN, color = '', orientation = 'horizontal', showLabel = true, showValue = true }:
    { value: number | undefined; min?: number; max?: number; title?: string; unit?: string; decimals?: number; warnAbove?: number; color?: string; orientation?: string; showLabel?: boolean; showValue?: boolean } = $props()
  const frac = $derived(value === undefined ? 0 : Math.min(1, Math.max(0, (value - min) / ((max - min) || 1))))
  const warn = $derived(Number.isFinite(warnAbove) && value !== undefined && value >= warnAbove)
  const vert = $derived(orientation === 'vertical')
</script>

<div class="bar" class:vert style:--c={color || 'var(--accent)'}>
  {#if showLabel || showValue}
    <div class="head">
      {#if showLabel}<span class="t">{title}</span>{/if}
      {#if showValue}<span class="v num" class:warn>{value === undefined ? '–' : value.toFixed(decimals)}<span class="u">{unit}</span></span>{/if}
    </div>
  {/if}
  <div class="track"><div class="fill" class:warn style={vert ? `height:${frac * 100}%` : `width:${frac * 100}%`}></div></div>
</div>

<style>
  .bar { width: 100%; height: 100%; display: flex; flex-direction: column; gap: 8px; justify-content: center; min-width: 0; }
  .vert { flex-direction: row; align-items: stretch; }
  .head { display: flex; justify-content: space-between; align-items: baseline; gap: 8px; min-width: 0; }
  .vert .head { flex-direction: column; justify-content: flex-end; }
  .t { font-size: 14px; color: var(--ink-2); font-weight: 600; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .v { font-size: 22px; font-weight: 700; } .v.warn { color: var(--bad); }
  .u { font-size: 12px; color: var(--ink-2); margin-left: 4px; }
  .track { flex: none; height: 14px; border-radius: 7px; background: var(--track); overflow: hidden; }
  .vert .track { flex: 1; height: auto; width: 18px; display: flex; align-items: flex-end; }
  .fill { height: 100%; border-radius: 7px; background: var(--c); transition: width .15s linear, height .15s linear; }
  .vert .fill { width: 100%; height: 0; }
  .fill.warn { background: var(--bad); }
</style>
