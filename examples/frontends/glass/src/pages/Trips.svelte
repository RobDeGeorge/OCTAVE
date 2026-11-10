<script lang="ts">
  // Trips: the black box. Every drive, its summary, and any reading plotted.
  import { octave, type Json } from '../lib/octave'
  import { act, toast, hms, catalog, tripLive } from '../lib/stores'
  import Icon from '../ui/Icon.svelte'
  import Modal from '../ui/Modal.svelte'
  import Toggle from '../ui/Toggle.svelte'
  import SeriesChart from '../ui/SeriesChart.svelte'
  import { mediaUrl } from '../lib/media'

  interface Trip { id: number; startIso: string; endIso?: string; durationS: number; sampleCount: number; open: boolean; make: string; modelYear: number; vin: string; clean: boolean; paramCount?: number }
  interface ParamStat { id: string; title: string; unit: string; min: number; max: number; avg: number; count: number; first: number; last: number }
  interface Detail extends Trip { distanceMiles?: number; maxSpeed?: number; maxRpm?: number; maxCoolant?: number; maxOilTemp?: number; fuelUsedPct?: number;
    minVoltage?: number; maxVoltage?: number; params: ParamStat[]; events: { t: number; kind: string; detail: string }[]; file: string; summaryFile: string }
  interface Store { enabled: boolean; storeReady: boolean; storageBytes: number; historyDir: string; lastError: string; [k: string]: Json }

  const trips = octave.prop<Trip[]>('tripRecorder', 'trips', [])
  const store = octave.props<Store>('tripRecorder', ['enabled', 'storeReady', 'storageBytes', 'historyDir', 'lastError'],
    { enabled: false, storeReady: false, storageBytes: 0, historyDir: '', lastError: '' })
  let selected = $state<number | null>(null)
  let detail = $state<Detail | null>(null)
  let param = $state('SPEED')
  let series = $state<{ t: number; v: number; lo: number; hi: number }[]>([])
  let loadingSeries = $state(false)
  let confirm = $state<'delete' | 'clear' | null>(null)
  let exported = $state<number | null>(null)   // the trip whose export is ready to download

  $effect(() => {
    const unsubs = [
      octave.signal('tripRecorder', 'detailReady', (id, d) => { if (id === selected) detail = d as unknown as Detail }),
      octave.signal('tripRecorder', 'seriesReady', (id, p, pts) => { if (id === selected && p === param) { series = pts as unknown as typeof series; loadingSeries = false } }),
      octave.signal('tripRecorder', 'exportFinished', (id, path) => { exported = path ? Number(id) : null; toast(path ? `Exported on the head unit: ${String(path).split('/').pop()}` : 'Export failed', path ? 'ok' : 'bad') }),
      octave.signal('tripRecorder', 'tripEnded', () => { if (selected) octave.call('tripRecorder', 'requestDetail', selected).catch(() => {}) }),
    ]
    return () => unsubs.forEach((u) => u())
  })
  $effect(() => {
    if (selected === null && $trips.length) selected = $trips[0].id
    if (selected !== null && !$trips.some((t) => t.id === selected)) { selected = $trips[0]?.id ?? null; detail = null }
  })
  $effect(() => {
    if (selected === null) return
    detail = null; series = []
    octave.call('tripRecorder', 'requestDetail', selected).catch(() => {})
  })
  $effect(() => {
    if (selected === null || !detail) return
    const have = detail.params.some((p) => p.id === param)
    if (!have && detail.params.length) { param = detail.params.find((p) => p.id === 'SPEED')?.id ?? detail.params[0].id; return }
    loadingSeries = true; series = []
    octave.call('tripRecorder', 'requestSeries', selected, param, 360).catch(() => (loadingSeries = false))
  })
  function when(iso: string) { const d = new Date(iso); return isNaN(+d) ? iso : d.toLocaleString(undefined, { weekday: 'short', month: 'short', day: 'numeric', hour: 'numeric', minute: '2-digit' }) }
  function mb(b: number) { return b > 1e9 ? `${(b / 1e9).toFixed(2)} GB` : `${(b / 1e6).toFixed(1)} MB` }
  const stat = $derived(detail?.params.find((p) => p.id === param))
  const info = $derived($catalog.get(param))
  const events = $derived((detail?.events ?? []).map((e) => ({ ...e, text: ({ dtc: `Codes ${e.detail}`, mil: `Check engine ${e.detail}`, dtc_clear: 'Codes cleared', disconnected: 'Link lost', reconnected: 'Link back' } as Record<string, string>)[e.kind] ?? `${e.kind} ${e.detail}` })))
</script>

<div class="page">
  <div class="page-head">
    <h1>Trips</h1>
    <div class="head-ctl">
      <span class="small muted">Recording {#if $store.enabled}on{:else}off{/if}, {mb($store.storageBytes)} used</span>
      <Toggle on={$store.enabled} label="Black box" onchange={(v) => act(octave.call('tripRecorder', 'setEnabled', v))} />
    </div>
  </div>
  {#if $store.lastError}<div class="glass panel bad">{$store.lastError}</div>{/if}

  <div class="layout">
    <div class="glass panel list">
      {#if $trips.length === 0}<p class="muted">No drives recorded yet. Connect to the car with the black box on.</p>{/if}
      {#each $trips as t (t.id)}
        <button class="trip" class:on={t.id === selected} onclick={() => (selected = t.id)}>
          <span class="t1">{when(t.startIso)}{#if t.open}<span class="live"><span class="reddot"></span>live</span>{/if}</span>
          <span class="small muted">{t.open ? hms($tripLive.currentTripElapsedS) : hms(t.durationS)} · {t.open ? $tripLive.currentSampleCount : t.sampleCount} samples{t.make ? ` · ${t.modelYear || ''} ${t.make}` : ''}{t.clean || t.open ? '' : ' · cut short'}</span>
        </button>
      {/each}
      <div class="wrap">
        {#if $tripLive.recording}<button class="btn" onclick={() => act(octave.call('tripRecorder', 'endTripNow'), 'Trip ended')}><Icon name="stop" />End this trip</button>{/if}
        <button class="btn" onclick={() => act(octave.call('tripRecorder', 'refresh'))}><Icon name="refresh" />Refresh</button>
        {#if $trips.length}<button class="btn danger" onclick={() => (confirm = 'clear')}>Clear history</button>{/if}
      </div>
    </div>

    <div class="detail">
      {#if !detail}
        <div class="glass panel muted">{selected === null ? 'Pick a trip.' : 'Loading…'}</div>
      {:else}
        <div class="glass panel">
          <div class="bar">
            <h2>{when(detail.startIso)}</h2>
            <div class="wrap">
              <button class="btn" disabled={detail.open} onclick={() => act(octave.call('tripRecorder', 'exportTrip', detail!.id), 'Exporting')}><Icon name="share" />Export</button>
              <button class="btn danger" disabled={detail.open} onclick={() => (confirm = 'delete')}><Icon name="trash" />Delete</button>
            </div>
          </div>
          {#if exported === detail.id}
            <div class="wrap dl">
              <a class="btn" href={mediaUrl('/export/trip')} download="trip.csv"><Icon name="download" />Download CSV</a>
              <a class="btn" href={mediaUrl('/export/trip', { format: 'json' })} download="trip.json"><Icon name="download" />Download JSON</a>
            </div>
          {/if}
          <div class="stats">
            <div><span class="k">Duration</span><span class="v num">{hms(detail.durationS)}</span></div>
            {#if detail.distanceMiles !== undefined}<div><span class="k">Distance</span><span class="v num">{detail.distanceMiles.toFixed(1)}<small> mi</small></span></div>{/if}
            {#if detail.maxSpeed !== undefined}<div><span class="k">Top speed</span><span class="v num">{Math.round(detail.maxSpeed)}<small> mph</small></span></div>{/if}
            {#if detail.maxRpm !== undefined}<div><span class="k">Peak rpm</span><span class="v num">{Math.round(detail.maxRpm)}</span></div>{/if}
            {#if detail.maxCoolant !== undefined}<div><span class="k">Max coolant</span><span class="v num">{Math.round(detail.maxCoolant)}<small> °C</small></span></div>{/if}
            {#if detail.fuelUsedPct !== undefined}<div><span class="k">Fuel used</span><span class="v num">{detail.fuelUsedPct.toFixed(1)}<small> %</small></span></div>{/if}
            {#if detail.minVoltage !== undefined}<div><span class="k">Voltage</span><span class="v num">{detail.minVoltage.toFixed(1)}<small> to {detail.maxVoltage?.toFixed(1)} V</small></span></div>{/if}
            <div><span class="k">Readings</span><span class="v num">{detail.paramCount ?? detail.params.length}</span></div>
          </div>
          {#if detail.vin}<p class="small muted num">{detail.modelYear} {detail.make} · {detail.vin}</p>{/if}
        </div>

        <div class="glass panel">
          <div class="bar">
            <h3>{info?.title ?? stat?.title ?? param}</h3>
            <select class="field sel" aria-label="Reading" bind:value={param}>
              {#each detail.params as p}<option value={p.id}>{$catalog.get(p.id)?.title ?? p.title ?? p.id}</option>{/each}
            </select>
          </div>
          {#if stat}
            <p class="small muted num">min {stat.min.toFixed(1)} · avg {stat.avg.toFixed(1)} · max {stat.max.toFixed(1)} {stat.unit} · {stat.count} samples</p>
          {/if}
          {#if loadingSeries}<p class="muted small">Loading…</p>{/if}
          <SeriesChart points={series} unit={stat?.unit ?? ''} decimals={info?.decimals ?? 1} title={stat?.title ?? param} />
        </div>

        {#if events.length}
          <div class="glass panel">
            <h3>Events</h3>
            <ul class="events">
              {#each events as e}<li><span class="num muted">{hms(e.t)}</span><span>{e.text}</span></li>{/each}
            </ul>
          </div>
        {/if}
      {/if}
    </div>
  </div>
</div>

{#if confirm}
  <Modal title={confirm === 'delete' ? 'Delete this trip?' : 'Clear every trip?'} onclose={() => (confirm = null)}>
    <p class="muted">{confirm === 'delete' ? 'Its samples and files go. This cannot be undone.' : 'The whole black box history is erased. This cannot be undone.'}</p>
    <button class="btn danger on" onclick={() => {
      const c = confirm; confirm = null
      if (c === 'delete' && detail) act(octave.call('tripRecorder', 'deleteTrip', detail.id), 'Trip deleted')
      else act(octave.call('tripRecorder', 'clearHistory'), 'History cleared')
    }}>{confirm === 'delete' ? 'Delete trip' : 'Clear history'}</button>
  </Modal>
{/if}

<style>
  .head-ctl { display: flex; align-items: center; gap: 14px; }
  .layout { display: grid; grid-template-columns: 340px 1fr; gap: var(--gap); align-items: start; }
  .list { display: flex; flex-direction: column; gap: 6px; }
  .trip { display: flex; flex-direction: column; gap: 2px; text-align: left; padding: 12px 16px; border-radius: 16px; min-height: 64px; }
  .trip.on { background: var(--control-on); color: var(--control-on-ink); }
  .trip.on .muted { color: inherit; opacity: .7; }
  .t1 { font-weight: 600; display: flex; gap: 10px; align-items: center; }
  .live { display: inline-flex; align-items: center; gap: 6px; font-size: 12px; text-transform: lowercase; color: var(--bad); }
  .reddot { width: 8px; height: 8px; border-radius: 50%; background: var(--bad); animation: blink 1.2s steps(2, jump-none) infinite; }
  @keyframes blink { 50% { opacity: .3; } }
  .wrap { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 8px; }
  .detail { display: flex; flex-direction: column; gap: var(--gap); min-width: 0; }
  .bar { display: flex; justify-content: space-between; align-items: center; gap: 12px; flex-wrap: wrap; margin-bottom: 12px; }
  .bar .wrap { margin: 0; }
  .stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 12px; }
  .stats div { display: flex; flex-direction: column; gap: 2px; }
  .k { font-size: 14px; color: var(--ink-2); }
  .v { font-size: 26px; font-weight: 700; letter-spacing: -.02em; }
  .v small { font-size: 14px; color: var(--ink-2); font-weight: 600; }
  .sel { max-width: 300px; }
  .events { list-style: none; margin: 0; padding: 0; }
  .events li { display: flex; gap: 16px; padding: 8px 0; border-top: 1px solid var(--glass-edge); }
  .bad { color: var(--bad); }
  .dl { margin: 0 0 12px; }
  a.btn { text-decoration: none; }
  @media (max-width: 760px), (max-aspect-ratio: 1/1) { .layout { grid-template-columns: 1fr; } }
</style>
