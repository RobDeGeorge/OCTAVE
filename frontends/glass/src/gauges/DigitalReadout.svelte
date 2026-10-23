<script lang="ts">
  let { value, title = '', unit = '', decimals = 0, color = '', showTitle = true, showUnit = true, padDigits = 0, valueScale = 3.5 }:
    { value: number | undefined; title?: string; unit?: string; decimals?: number; color?: string; showTitle?: boolean; showUnit?: boolean; padDigits?: number; valueScale?: number } = $props()
  const text = $derived.by(() => {
    if (value === undefined) return '–'
    const s = Math.abs(value).toFixed(decimals)
    const [int, frac] = s.split('.')
    return (value < 0 ? '-' : '') + int.padStart(padDigits, '0') + (frac ? '.' + frac : '')
  })
</script>

<div class="dr" style:--c={color || 'var(--ink)'} style:--s={valueScale}>
  {#if showTitle}<div class="t">{title}</div>{/if}
  <div class="v num">{text}{#if showUnit}<span class="u">{unit}</span>{/if}</div>
</div>

<style>
  .dr { width: 100%; height: 100%; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 2px; container-type: size; }
  .t { font-size: 14px; color: var(--ink-2); font-weight: 600; }
  .v { font-size: min(calc(var(--s) * 18px), 70cqh, 34cqw); font-weight: 700; letter-spacing: -.04em; line-height: 1; color: var(--c); }
  .u { font-size: .3em; color: var(--ink-2); margin-left: .15em; letter-spacing: 0; font-weight: 600; }
</style>
