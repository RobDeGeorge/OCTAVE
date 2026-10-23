<script lang="ts">
  // Vehicle: the OBD link, adapters, the log, the car, every PID live,
  // trouble codes and the freeze frame.
  import { octave, type Json } from '../lib/octave'
  import { obd, catalog, catalogOrder, toDisplay, act, toast } from '../lib/stores'
  import Icon from '../ui/Icon.svelte'
  import Modal from '../ui/Modal.svelte'
  import ArcGauge from '../gauges/ArcGauge.svelte'

  let { arg = '' }: { arg?: string } = $props()
  type Tab = 'live' | 'link' | 'codes'
  let tab = $state<Tab>('live')
  $effect(() => { if (arg === 'link' || arg === 'codes' || arg === 'live') tab = arg })

  // ── live values ────────────────────────────────────────────────────────
  const supported = $derived($obd.vehicleSupportedParameters)
  const liveIds = $derived(tab === 'live' ? $catalogOrder.filter((id) => supported.length === 0 || supported.includes(id)) : [])
  const live = $derived(octave.params(liveIds, 120))
  $effect(() => {
    const ids = liveIds
    octave.call('obdManager', 'setParameterDemand', 'glass-vehicle', ids).catch(() => {})
    return () => { octave.call('obdManager', 'setParameterDemand', 'glass-vehicle', []).catch(() => {}) }
  })
  let filter = $state('')
  const shown = $derived(liveIds.filter((id) => {
    if (!filter) return true
    const info = $catalog.get(id)
    return (info?.title ?? id).toLowerCase().includes(filter.toLowerCase()) || id.toLowerCase().includes(filter.toLowerCase())
  }))

  // ── link ───────────────────────────────────────────────────────────────
  const log = octave.prop<string[]>('obdManager', 'connectionLog', [])
  const port = octave.prop<string>('settingsManager', 'obdBluetoothPort', '')
  const saved = octave.prop<{ name: string; mac: string }[]>('settingsManager', 'obdSavedAdapters', [])
  const adapters = octave.prop<{ identifier: string; name: string; kind: string }[]>('obdManager', 'availableAdapters', [])
  interface Finder { supported: boolean; bluetoothOn: boolean; scanning: boolean; busyAddress: string; status: string; statusIsError: boolean;
                     devices: { address: string; name: string; rssi: number; paired: boolean; likelyObd: boolean }[]; [k: string]: Json }
  const finder = octave.props<Finder>('obdAdapterFinder', ['supported', 'bluetoothOn', 'scanning', 'devices', 'busyAddress', 'status', 'statusIsError'],
    { supported: false, bluetoothOn: false, scanning: false, devices: [], busyAddress: '', status: '', statusIsError: false })
  let manual = $state('')
  let logEl = $state<HTMLElement | null>(null)
  $effect(() => { $log; if (logEl) logEl.scrollTop = logEl.scrollHeight })

  // ── codes ──────────────────────────────────────────────────────────────
  interface Code { code: string; description: string }
  let codes = $state<Code[]>([])
  let frame = $state<Code[]>([])
  let mil = $state(false)
  let busy = $state('')
  let confirmClear = $state(false)
  $effect(() => {
    if (tab !== 'codes') return
    const unsubs = [
      octave.signal('obdManager', 'dtcCodesChanged', (list) => { codes = list as unknown as Code[]; busy = '' }),
      octave.signal('obdManager', 'milStatusChanged', (m) => { mil = Boolean(m) }),
      octave.signal('obdManager', 'freezeFrameChanged', (list) => { frame = list as unknown as Code[]; busy = '' }),
      octave.signal('obdManager', 'dtcClearResult', (ok, msg) => { busy = ''; toast(ok ? 'Trouble codes cleared' : String(msg || 'Clearing failed'), ok ? 'ok' : 'bad') }),
    ]
    octave.call<Code[]>('obdManager', 'get_dtc_codes').then((c) => (codes = c)).catch(() => {})
    octave.call<Code[]>('obdManager', 'get_freeze_frame').then((f) => (frame = f)).catch(() => {})
    octave.call<boolean>('obdManager', 'get_mil_status').then((m) => (mil = m)).catch(() => {})
    octave.call('obdManager', 'enter_diagnostic_mode').catch(() => {})
    return () => { unsubs.forEach((u) => u()); octave.call('obdManager', 'exit_diagnostic_mode').catch(() => {}) }
  })
  function read(what: 'read_dtc' | 'read_freeze_frame' | 'read_status') {
    busy = what
    act(octave.call('obdManager', what))
    setTimeout(() => { if (busy === what) busy = '' }, 8000)
  }

  // close() leaves connectionState at Connected while `connected` goes false; trust the flag
  const linkState = $derived($obd.connectionState === 'Connected' && !$obd.connected ? 'Disconnected' : $obd.connectionState)
  const stateColor = $derived(linkState === 'Connected' ? 'good' : linkState === 'Connecting' ? 'warn' : linkState === 'Disconnected' ? '' : 'bad')
</script>

<div class="page">
  <div class="page-head">
    <h1>Vehicle</h1>
    <div class="seg" role="tablist">
      <button class:on={tab === 'live'} role="tab" aria-selected={tab === 'live'} onclick={() => (tab = 'live')}>Live</button>
      <button class:on={tab === 'link'} role="tab" aria-selected={tab === 'link'} onclick={() => (tab = 'link')}>Connection</button>
      <button class:on={tab === 'codes'} role="tab" aria-selected={tab === 'codes'} onclick={() => (tab = 'codes')}>Trouble codes</button>
    </div>
  </div>

  <div class="glass panel status">
    <span class="light {stateColor}"></span>
    <div class="label">
      <strong>{linkState === 'NoVehicle' ? 'Adapter found, no vehicle' : linkState}</strong>
      <span class="small muted">{$obd.connectionDetail || $obd.connectionStatus}</span>
    </div>
    {#if $obd.connected}
      <span class="muted car">{$obd.vehicleModelYear || ''} {$obd.vehicleMake} <span class="num faint">{$obd.vin}</span></span>
    {/if}
    {#if $obd.connectionState === 'Connecting'}
      <div class="progress"><div style="width:{$obd.connectionProgress}%"></div></div>
    {/if}
  </div>

  {#if tab === 'live'}
    <div class="glass panel">
      <div class="bar">
        <input class="field search" placeholder="Find a reading" bind:value={filter} aria-label="Find a reading" />
        <span class="muted small">{shown.length} of {liveIds.length} readings{$obd.vehicleScanComplete ? '' : ', scan in progress'}</span>
      </div>
      {#if liveIds.length === 0}
        <p class="muted">Nothing to read: the car is not connected.</p>
      {/if}
      <div class="cards">
        {#each shown as id (id)}
          {@const info = $catalog.get(id)}
          {@const v = $live[id]}
          <div class="card" class:stale={v === undefined}>
            <div class="small muted title">{info?.title ?? id}</div>
            <div class="val num">{v === undefined ? '–' : toDisplay(info, v).toFixed(info?.decimals ?? 0)}<span class="unit">{info?.unit ?? ''}</span></div>
            {#if info && info.kind !== 'bidirectional'}
              <div class="mini"><div style="width:{Math.min(100, Math.max(0, ((toDisplay(info, v ?? info.min) - info.min) / ((info.max - info.min) || 1)) * 100))}%"></div></div>
            {/if}
          </div>
        {/each}
      </div>
    </div>

  {:else if tab === 'link'}
    <div class="grid">
      <div class="glass panel span-6 col">
        <h2>Link</h2>
        <p class="muted small">Adapter: <span class="num">{$port || 'none chosen'}</span></p>
        <div class="wrap">
          {#if $obd.connected || $obd.connectionState === 'Connecting'}
            <button class="btn" onclick={() => act(octave.call('obdManager', 'reconnect'))}><Icon name="refresh" />Reconnect</button>
            <button class="btn danger" onclick={() => act(octave.call('obdManager', 'close'))}><Icon name="unlink" />Disconnect</button>
          {:else}
            <button class="btn primary" onclick={() => act(octave.call('obdManager', 'connect_obd'))}><Icon name="link" />Connect</button>
            <button class="btn" onclick={() => act(octave.call('obdManager', 'force_connect'))}>Force connect</button>
          {/if}
          <button class="btn" onclick={() => act(octave.call('obdManager', 'reset_connection'))}>Reset</button>
          <button class="btn" onclick={() => act(octave.call('obdManager', 'scan_vehicle'), 'Scanning the vehicle')}>Rescan PIDs</button>
          <button class="btn" onclick={() => act(octave.call('obdManager', 'read_vin'))}>Read VIN</button>
        </div>
        <h3>Log</h3>
        <pre class="log small" bind:this={logEl}>{$log.join('\n') || 'Nothing yet.'}</pre>
        <button class="btn" onclick={() => act(octave.call('obdManager', 'clearConnectionLog'))}>Clear log</button>
      </div>

      <div class="glass panel span-6 col">
        <h2>Adapters</h2>
        {#if $saved.length}
          <h3>Saved</h3>
          {#each $saved as a}
            <div class="row">
              <div class="label"><span>{a.name || a.mac}</span><span class="small num">{a.mac}</span></div>
              <div class="wrap">
                <button class="btn" class:on={$port === a.mac} onclick={() => act(octave.call('obdManager', 'connect_to_adapter', a.mac), `Connecting to ${a.name || a.mac}`)}>Use</button>
                <button class="btn icon danger" aria-label="Forget {a.name}" onclick={() => act(octave.call('settingsManager', 'remove_obd_saved_adapter', a.mac), 'Forgotten')}><Icon name="trash" /></button>
              </div>
            </div>
          {/each}
        {/if}

        <h3>Ports</h3>
        <div class="wrap"><button class="btn" onclick={() => act(octave.call('obdManager', 'scan'))}><Icon name="refresh" />Look for ports</button></div>
        {#each $adapters as a}
          <div class="row">
            <div class="label"><span>{a.name}</span><span class="small num muted">{a.identifier} · {a.kind}</span></div>
            <button class="btn" class:on={$port === a.identifier} onclick={() => act(octave.call('obdManager', 'connect_to_adapter', a.identifier), `Connecting to ${a.name}`)}>Use</button>
          </div>
        {/each}
        <div class="manual">
          <input class="field" placeholder="Address or port, e.g. 00:1D:A5:00:12:34 or /dev/rfcomm0" bind:value={manual} aria-label="Adapter address" />
          <button class="btn" disabled={!manual.trim()} onclick={() => act(octave.call('obdManager', 'connect_to_adapter', manual.trim()))}>Use</button>
        </div>

        <h3>Bluetooth</h3>
        {#if !$finder.supported}
          <p class="muted small">Bluetooth scanning is not available on this platform; pair the adapter in the OS and use its address above.</p>
        {:else}
          <div class="wrap">
            {#if !$finder.bluetoothOn}
              <button class="btn primary" onclick={() => act(octave.call('obdAdapterFinder', 'powerOn'))}><Icon name="bluetooth" />Turn Bluetooth on</button>
            {:else if $finder.scanning}
              <button class="btn on" onclick={() => act(octave.call('obdAdapterFinder', 'stopScan'))}>Stop scanning</button>
            {:else}
              <button class="btn primary" onclick={() => act(octave.call('obdAdapterFinder', 'startScan'))}><Icon name="bluetooth" />Scan for adapters</button>
            {/if}
            {#if $finder.busyAddress}
              <button class="btn" onclick={() => act(octave.call('obdAdapterFinder', 'cancel'))}>Cancel</button>
            {/if}
          </div>
          {#if $finder.status}<p class="small" class:bad={$finder.statusIsError}>{$finder.status}</p>{/if}
          {#each $finder.devices as d}
            <div class="row">
              <div class="label"><span>{d.name || 'Unnamed'} {#if d.likelyObd}<span class="tag">OBD</span>{/if}</span><span class="small num muted">{d.address} · {d.paired ? 'paired' : 'not paired'}{d.rssi ? ` · ${d.rssi} dBm` : ''}</span></div>
              <button class="btn" disabled={$finder.busyAddress !== ''} onclick={() => act(octave.call('obdAdapterFinder', 'connectAdapter', d.address))}>{$finder.busyAddress === d.address ? 'Pairing…' : 'Pair and connect'}</button>
            </div>
          {/each}
        {/if}
      </div>
    </div>

  {:else}
    <div class="grid">
      <div class="glass panel span-7 col">
        <div class="bar">
          <h2>Trouble codes</h2>
          <span class="chip" class:bad={mil}><Icon name="warn" size={18} />{mil ? 'Check engine on' : 'Check engine off'}</span>
        </div>
        <div class="wrap">
          <button class="btn primary" disabled={!$obd.connected || busy !== ''} onclick={() => read('read_dtc')}>{busy === 'read_dtc' ? 'Reading…' : 'Read codes'}</button>
          <button class="btn" disabled={!$obd.connected || busy !== ''} onclick={() => read('read_status')}>Check lamp</button>
          <button class="btn danger" disabled={!$obd.connected || busy !== ''} onclick={() => (confirmClear = true)}><Icon name="trash" />Clear codes</button>
        </div>
        {#if codes.length === 0}
          <p class="muted">No stored codes read. Press Read codes.</p>
        {:else}
          <ul class="codes">
            {#each codes as c}<li><span class="code num">{c.code}</span><span class="muted">{c.description || describe(c.code)}</span></li>{/each}
          </ul>
        {/if}
      </div>
      <div class="glass panel span-5 col">
        <h2>Freeze frame</h2>
        <p class="muted small">The code the ECU stored its snapshot for.</p>
        <div class="wrap"><button class="btn" disabled={!$obd.connected || busy !== ''} onclick={() => read('read_freeze_frame')}>{busy === 'read_freeze_frame' ? 'Reading…' : 'Read freeze frame'}</button></div>
        {#if frame.length === 0}
          <p class="muted">Nothing read yet.</p>
        {:else}
          <ul class="codes">
            {#each frame as c}<li><span class="code num">{c.code}</span><span class="muted">{c.description || describe(c.code)}</span></li>{/each}
          </ul>
        {/if}
      </div>
    </div>
  {/if}
</div>

{#if confirmClear}
  <Modal title="Clear trouble codes?" onclose={() => (confirmClear = false)}>
    <p class="muted">This also turns off the check-engine lamp and resets the readiness monitors. The codes come back if the fault is still there.</p>
    <button class="btn danger on" onclick={() => { confirmClear = false; busy = 'clear'; act(octave.call('obdManager', 'clear_dtc')) }}>Clear codes</button>
  </Modal>
{/if}

<script module lang="ts">
  // The family a code belongs to, when the ECU gives no text.
  function describe(code: string): string {
    const f = code[0], n = code.slice(1)
    const sys = f === 'P' ? 'Powertrain' : f === 'C' ? 'Chassis' : f === 'B' ? 'Body' : f === 'U' ? 'Network' : 'Unknown'
    const generic = n[0] === '0' || n[0] === '2' ? 'standard' : 'manufacturer'
    return `${sys}, ${generic} code`
  }
</script>

<style>
  .status { display: flex; align-items: center; gap: 16px; padding: 18px 26px; flex-wrap: wrap; position: relative; overflow: hidden; }
  .light { width: 14px; height: 14px; border-radius: 50%; background: var(--ink-3); flex: none; }
  .light.good { background: var(--good); box-shadow: 0 0 12px var(--good); }
  .light.warn { background: var(--warn); box-shadow: 0 0 12px var(--warn); }
  .light.bad { background: var(--bad); box-shadow: 0 0 12px var(--bad); }
  .status .label { display: flex; flex-direction: column; flex: 1; min-width: 0; }
  .car { margin-left: auto; }
  .progress { position: absolute; left: 0; right: 0; bottom: 0; height: 4px; background: var(--track); }
  .progress div { height: 100%; background: var(--warn); transition: width .3s; }
  .bar { display: flex; justify-content: space-between; align-items: center; gap: 14px; flex-wrap: wrap; margin-bottom: 14px; }
  .search { max-width: 360px; }
  .cards { display: grid; grid-template-columns: repeat(auto-fill, minmax(190px, 1fr)); gap: 12px; }
  .card { background: var(--control); border-radius: var(--radius-s); padding: 14px 16px; display: flex; flex-direction: column; gap: 4px; min-width: 0; }
  .card.stale { opacity: .55; }
  .card .title { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .val { font-size: 30px; font-weight: 700; letter-spacing: -.02em; line-height: 1.1; }
  .unit { font-size: 14px; font-weight: 600; color: var(--ink-2); margin-left: 6px; }
  .mini { height: 5px; border-radius: 3px; background: var(--track); overflow: hidden; margin-top: 6px; }
  .mini div { height: 100%; background: var(--accent); transition: width .2s linear; }
  .col { display: flex; flex-direction: column; gap: 14px; }
  .wrap { display: flex; flex-wrap: wrap; gap: 8px; }
  .log { margin: 0; max-height: 260px; overflow: auto; background: var(--control); border-radius: var(--radius-s); padding: 14px; white-space: pre-wrap; font-family: ui-monospace, monospace; }
  .manual { display: flex; gap: 8px; }
  .tag { font-size: 11px; padding: 2px 8px; border-radius: 999px; background: var(--good); color: #000; margin-left: 6px; vertical-align: middle; }
  .bad { color: var(--bad); }
  .chip { display: inline-flex; align-items: center; gap: 8px; min-height: 40px; padding: 0 14px; border-radius: 999px; background: var(--control); font-weight: 600; font-size: 15px; }
  .chip.bad { background: var(--warn); color: #000; }
  .codes { list-style: none; margin: 0; padding: 0; }
  .codes li { display: flex; gap: 16px; align-items: baseline; padding: 12px 0; border-top: 1px solid var(--glass-edge); }
  .code { font-size: 24px; font-weight: 700; min-width: 5ch; }
  @media (max-width: 760px) {
    .status .label { flex-basis: calc(100% - 30px); }
    .car { margin-left: 30px; }
  }
</style>
