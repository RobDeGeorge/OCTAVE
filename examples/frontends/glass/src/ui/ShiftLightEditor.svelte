<script lang="ts">
  // The shift light: its switches (settings keys the manager reads) and the
  // RPM bands, written back whole through shiftLightManager.setFlags.
  import { octave, type Json } from '../lib/octave'
  import { act, toast } from '../lib/stores'
  import Toggle from './Toggle.svelte'
  import Slider from './Slider.svelte'
  import Icon from './Icon.svelte'

  interface Flag { rpmLow: number; rpmHigh: number; color: string; flash: boolean; flashSpeed: number; fullScreenFlash: boolean;
                   fullScreenFlashOpacity: number; auraFlash: boolean; auraFlashOpacity: number; auraSpread: number; [k: string]: Json }
  interface SL { enabled: boolean; showOnHomeCard: boolean; maxRpm: number; fullScreenFlashOpacity: number; lampSize: number; glowSize: number;
                 glowIntensity: number; colorTransitionSpeed: number; pulseEnabled: boolean; flags: Flag[]; rpm: number; activeIndex: number; lit: boolean; [k: string]: Json }
  const sl = octave.props<SL>('shiftLightManager',
    ['enabled', 'showOnHomeCard', 'maxRpm', 'fullScreenFlashOpacity', 'lampSize', 'glowSize', 'glowIntensity', 'colorTransitionSpeed', 'pulseEnabled', 'flags', 'rpm', 'activeIndex', 'lit'],
    { enabled: false, showOnHomeCard: true, maxRpm: 8000, fullScreenFlashOpacity: .5, lampSize: .5, glowSize: .6, glowIntensity: .6,
      colorTransitionSpeed: 100, pulseEnabled: false, flags: [], rpm: 0, activeIndex: -1, lit: false }, 100)
  const palette = ['#2ECC71', '#F1C40F', '#E67E22', '#E74C3C', '#FF00AA', '#00BFFF', '#9C27B0', '#FFFFFF']

  function key(k: string, v: unknown) { act(octave.call('settingsManager', 'save_setting', k, v)) }
  function write(flags: Flag[]) {
    const sorted = flags.slice().sort((a, b) => a.rpmLow - b.rpmLow)
    act(octave.call('shiftLightManager', 'setFlags', sorted))
  }
  function patch(i: number, p: Partial<Flag>) {
    const flags = $sl.flags.map((f, j) => (j === i ? ({ ...f, ...p } as Flag) : f))
    const f = flags[i]
    if (f.rpmLow > f.rpmHigh) { if ('rpmLow' in p) f.rpmHigh = f.rpmLow; else f.rpmLow = f.rpmHigh }
    write(flags)
  }
  function add() {
    const m = $sl.maxRpm
    write([...$sl.flags, { rpmLow: Math.round(m * .6 / 100) * 100, rpmHigh: Math.round(m * .85 / 100) * 100,
      color: palette[$sl.flags.length % palette.length], flash: false, flashSpeed: 100, fullScreenFlash: false,
      fullScreenFlashOpacity: .5, auraFlash: false, auraFlashOpacity: .6, auraSpread: .18 }])
    toast('Band added')
  }
  function remove(i: number) { write($sl.flags.filter((_, j) => j !== i)) }
</script>

<div class="row"><div class="label"><span>Shift light</span><span class="small">Flashes the screen as the engine reaches each band</span></div>
  <Toggle on={$sl.enabled} label="Shift light" onchange={(v) => key('rpm_shift_light_enabled', v)} /></div>
<div class="row"><div class="label"><span>Lamp on the home card</span></div>
  <Toggle on={$sl.showOnHomeCard} label="Lamp on the home card" onchange={(v) => key('rpm_show_on_home_card', v)} /></div>
<div class="row"><div class="label"><span>Pulse</span></div>
  <Toggle on={$sl.pulseEnabled} label="Pulse" onchange={(v) => key('rpm_pulse_enabled', v)} /></div>
<div class="row stack"><div class="label"><span>Tachometer top</span></div>
  <Slider value={$sl.maxRpm} min={3000} max={12000} step={100} label="Tachometer top" format={(v) => `${v} rpm`} onchange={(v) => key('rpm_max_rpm', Math.round(v))} /></div>
<div class="row stack"><div class="label"><span>Full-screen flash opacity</span></div>
  <Slider value={$sl.fullScreenFlashOpacity} min={0} max={1} step={0.05} label="Flash opacity" format={(v) => `${Math.round(v * 100)}%`} onchange={(v) => key('rpm_fullscreen_flash_opacity', v)} /></div>
<div class="row stack"><div class="label"><span>Lamp size</span></div>
  <Slider value={$sl.lampSize} min={0.1} max={1} step={0.05} label="Lamp size" format={(v) => `${Math.round(v * 100)}%`} onchange={(v) => key('rpm_shift_light_size', v)} /></div>
<div class="row stack"><div class="label"><span>Glow size</span></div>
  <Slider value={$sl.glowSize} min={0} max={1} step={0.05} label="Glow size" format={(v) => `${Math.round(v * 100)}%`} onchange={(v) => key('rpm_glow_size', v)} /></div>
<div class="row stack"><div class="label"><span>Glow intensity</span></div>
  <Slider value={$sl.glowIntensity} min={0} max={1} step={0.05} label="Glow intensity" format={(v) => `${Math.round(v * 100)}%`} onchange={(v) => key('rpm_glow_intensity', v)} /></div>
<div class="row stack"><div class="label"><span>Colour transition</span></div>
  <Slider value={$sl.colorTransitionSpeed} min={0} max={1000} step={50} label="Colour transition" format={(v) => `${v} ms`} onchange={(v) => key('rpm_color_transition_speed', Math.round(v))} /></div>

<div class="bands">
  <div class="scale" aria-hidden="true">
    {#each $sl.flags as f, i}
      <div class="band" style="left:{(f.rpmLow / $sl.maxRpm) * 100}%; width:{((f.rpmHigh - f.rpmLow) / $sl.maxRpm) * 100}%; background:{f.color}" class:lit={$sl.activeIndex === i && $sl.lit}></div>
    {/each}
    <div class="needle" style="left:{Math.min(100, ($sl.rpm / $sl.maxRpm) * 100)}%"></div>
  </div>
  <div class="scale-labels small muted num"><span>0</span><span>{$sl.maxRpm} rpm</span></div>

  {#each $sl.flags as f, i (i)}
    <div class="flag glass">
      <div class="flag-head">
        <div class="swatches">
          {#each palette as c}
            <button class="sw" class:on={f.color.toLowerCase() === c.toLowerCase()} style="background:{c}" aria-label="Colour {c}" onclick={() => patch(i, { color: c })}></button>
          {/each}
        </div>
        <button class="btn icon danger" aria-label="Remove band" onclick={() => remove(i)}><Icon name="trash" /></button>
      </div>
      <div class="row stack"><div class="label"><span>From</span></div>
        <Slider value={f.rpmLow} min={0} max={$sl.maxRpm} step={100} label="From" format={(v) => `${v} rpm`} onchange={(v) => patch(i, { rpmLow: v })} /></div>
      <div class="row stack"><div class="label"><span>To</span></div>
        <Slider value={f.rpmHigh} min={0} max={$sl.maxRpm} step={100} label="To" format={(v) => `${v} rpm`} onchange={(v) => patch(i, { rpmHigh: v })} /></div>
      <div class="row"><div class="label"><span>Flash the lamp</span></div><Toggle on={f.flash} label="Flash" onchange={(v) => patch(i, { flash: v })} /></div>
      {#if f.flash}
        <div class="row stack"><div class="label"><span>Flash period</span></div>
          <Slider value={f.flashSpeed} min={50} max={1000} step={50} label="Flash period" format={(v) => `${v} ms`} onchange={(v) => patch(i, { flashSpeed: v })} /></div>
      {/if}
      <div class="row"><div class="label"><span>Flash the whole screen</span></div><Toggle on={f.fullScreenFlash} label="Full-screen flash" onchange={(v) => patch(i, { fullScreenFlash: v })} /></div>
      <div class="row"><div class="label"><span>Glow at the edges</span></div><Toggle on={f.auraFlash} label="Edge glow" onchange={(v) => patch(i, { auraFlash: v })} /></div>
    </div>
  {/each}
  <button class="btn" onclick={add}><Icon name="plus" />Add a band</button>
</div>

<style>
  .row.stack { flex-direction: column; align-items: stretch; gap: 8px; }
  .bands { display: flex; flex-direction: column; gap: 12px; margin-top: 16px; }
  .scale { position: relative; height: 22px; border-radius: 11px; background: var(--track); overflow: hidden; }
  .band { position: absolute; top: 0; bottom: 0; opacity: .55; }
  .band.lit { opacity: 1; }
  .needle { position: absolute; top: 0; bottom: 0; width: 3px; background: var(--ink); transition: left .1s linear; }
  .scale-labels { display: flex; justify-content: space-between; }
  .flag { padding: 14px 18px; border-radius: var(--radius-s); }
  .flag-head { display: flex; justify-content: space-between; align-items: center; gap: 10px; }
  .swatches { display: flex; gap: 8px; flex-wrap: wrap; }
  .sw { width: 34px; height: 34px; border-radius: 50%; border: 3px solid transparent; }
  .sw.on { border-color: var(--ink); }
</style>
