<script lang="ts">
  // A fat touch slider. `oninput` fires while dragging, `onchange` on release.
  let { value, min = 0, max = 100, step = 1, label = '', format = (v: number) => String(v), oninput, onchange, disabled = false }:
    { value: number; min?: number; max?: number; step?: number; label?: string; format?: (v: number) => string;
      oninput?: (v: number) => void; onchange?: (v: number) => void; disabled?: boolean } = $props()
  let local = $state(0)
  let dragging = $state(false)
  $effect(() => { if (!dragging) local = value })
  const pct = $derived(max > min ? ((local - min) / (max - min)) * 100 : 0)
</script>

<div class="slider" class:disabled style="--pct:{pct}%">
  <input type="range" {min} {max} {step} value={local} aria-label={label} {disabled}
         oninput={(e) => { dragging = true; local = Number((e.target as HTMLInputElement).value); oninput?.(local) }}
         onchange={() => { dragging = false; onchange?.(local) }} />
  <div class="val num">{format(local)}</div>
</div>

<style>
  .slider { display: flex; align-items: center; gap: 16px; width: 100%; }
  .disabled { opacity: .4; }
  input { flex: 1; appearance: none; height: 44px; margin: 0; background: transparent; min-width: 120px; }
  input::-webkit-slider-runnable-track { height: 14px; border-radius: 7px;
    background: linear-gradient(90deg, var(--accent) var(--pct), var(--track) var(--pct)); }
  input::-moz-range-track { height: 14px; border-radius: 7px; background: var(--track); }
  input::-moz-range-progress { height: 14px; border-radius: 7px; background: var(--accent); }
  input::-webkit-slider-thumb { appearance: none; width: 34px; height: 34px; border-radius: 50%; margin-top: -10px;
    background: #fff; box-shadow: 0 2px 8px rgba(0,0,0,.3); border: 0; }
  input::-moz-range-thumb { width: 34px; height: 34px; border-radius: 50%; background: #fff; border: 0; box-shadow: 0 2px 8px rgba(0,0,0,.3); }
  .val { min-width: 4.5ch; text-align: right; font-weight: 600; }
</style>
