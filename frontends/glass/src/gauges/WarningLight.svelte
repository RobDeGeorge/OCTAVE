<script lang="ts">
  let { value, label = '', title = '', color = '', triggerAbove = NaN, triggerBelow = NaN, pulse = false }:
    { value: number | undefined; label?: string; title?: string; color?: string; triggerAbove?: number; triggerBelow?: number; pulse?: boolean } = $props()
  const on = $derived(value !== undefined && ((Number.isFinite(triggerAbove) && value > triggerAbove) || (Number.isFinite(triggerBelow) && value < triggerBelow)))
</script>

<div class="wl" class:on class:pulse={pulse && on} style:--c={color || 'var(--warn)'}>
  <div class="lamp"></div>
  <div class="t">{label || title}</div>
</div>

<style>
  .wl { width: 100%; height: 100%; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 8px; }
  .lamp { width: 40%; aspect-ratio: 1; border-radius: 50%; background: var(--track); transition: background .2s, box-shadow .2s; }
  .on .lamp { background: var(--c); box-shadow: 0 0 24px var(--c); }
  .pulse .lamp { animation: pulse .8s infinite; }
  @keyframes pulse { 50% { opacity: .35; } }
  .t { font-size: 14px; font-weight: 700; color: var(--ink-2); text-align: center; }
  .on .t { color: var(--ink); }
</style>
