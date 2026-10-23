<script lang="ts">
  // Home: the time, what is playing, a few gauges, and whether the car is on the line.
  import { octave } from '../lib/octave'
  import { clock, nowPlaying, position, volume, obd, catalog, toDisplay, mmss, act, shiftLight, tripLive, hms } from '../lib/stores'
  import { go } from '../lib/router'
  import Icon from '../ui/Icon.svelte'
  import ArcGauge from '../gauges/ArcGauge.svelte'
  import NowPlayingCard from '../ui/NowPlayingCard.svelte'

  const homeIds = octave.prop<string[]>('settingsManager', 'homeOBDParameters', [])
  const ids = $derived($homeIds.slice(0, 4))
  const live = $derived(octave.params(ids, 60))
  $effect(() => {
    const list = ids
    octave.call('obdManager', 'setParameterDemand', 'glass-home', list).catch(() => {})
    return () => { octave.call('obdManager', 'setParameterDemand', 'glass-home', []).catch(() => {}) }
  })

  let now = $state(new Date())
  $effect(() => { const t = setInterval(() => (now = new Date()), 1000); return () => clearInterval(t) })
  const dateText = $derived(now.toLocaleDateString(undefined, { weekday: 'long', month: 'long', day: 'numeric' }))
  const timeText = $derived($clock || now.toLocaleTimeString(undefined, { hour: 'numeric', minute: '2-digit' }))
</script>

<div class="page">
  <section class="hero">
    <div class="glass panel clock">
      <div class="time num">{timeText}</div>
      <div class="date muted">{dateText}</div>
      <div class="chips">
        <button class="chip" class:good={$obd.connected} class:bad={$obd.connectionState === 'Failed'} onclick={() => go('obd')}>
          <Icon name="car" size={20} />
          {$obd.connected ? ($obd.vehicleMake ? `${$obd.vehicleModelYear || ''} ${$obd.vehicleMake}`.trim() : 'Vehicle connected') : $obd.connectionState === 'Connecting' ? 'Connecting to the car' : 'No vehicle'}
        </button>
        {#if $tripLive.recording}
          <button class="chip rec" onclick={() => go('trips')}><span class="reddot"></span>Recording {hms($tripLive.currentTripElapsedS)}</button>
        {/if}
        {#if $shiftLight.enabled && $shiftLight.active}
          <span class="chip" class:lit={$shiftLight.lit}><Icon name="bolt" size={20} />Shift</span>
        {/if}
      </div>
    </div>
    <NowPlayingCard compact />
  </section>

  <section class="gauges">
    {#each ids as id (id)}
      {@const info = $catalog.get(id)}
      <button class="glass gauge" onclick={() => go('obd')} aria-label="Open vehicle page">
        <ArcGauge value={$live[id] === undefined ? undefined : toDisplay(info, $live[id])}
                  min={info?.min ?? 0} max={info?.max ?? 100} title={info?.title ?? id} unit={info?.unit ?? ''}
                  decimals={info?.decimals ?? 0} />
      </button>
    {/each}
    {#if ids.length === 0}
      <div class="glass panel muted">No home gauges chosen. Pick them under Settings, Vehicle.</div>
    {/if}
  </section>
</div>

<style>
  .hero { display: grid; grid-template-columns: 5fr 7fr; gap: var(--gap); }
  .clock { display: flex; flex-direction: column; justify-content: space-between; gap: 18px; min-height: 300px; }
  .time { font-size: clamp(56px, 7.2vw, 96px); font-weight: 300; white-space: nowrap; letter-spacing: -.04em; line-height: 1; }
  .date { font-size: 22px; }
  .chips { display: flex; flex-wrap: wrap; gap: 10px; }
  .chip { display: inline-flex; align-items: center; gap: 8px; min-height: 44px; padding: 0 16px; border-radius: 999px;
          background: var(--control); font-weight: 600; font-size: 15px; color: var(--ink-2); }
  .chip.good { color: var(--good); }
  .chip.bad { color: var(--bad); }
  .chip.lit { background: var(--warn); color: #000; }
  .reddot { width: 10px; height: 10px; border-radius: 50%; background: var(--bad); animation: blink 1.2s infinite; }
  @keyframes blink { 50% { opacity: .3; } }
  .gauges { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: var(--gap); }
  .gauge { aspect-ratio: 1 / 1; padding: 14px; display: block; }
  @media (max-width: 760px), (max-aspect-ratio: 1/1) {
    .hero { grid-template-columns: 1fr; }
    .clock { min-height: 0; }
    .gauges { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  }
</style>
