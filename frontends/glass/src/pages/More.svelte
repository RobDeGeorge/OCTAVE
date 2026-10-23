<script lang="ts">
  // More: downloads, the phone (mirror and Android Auto), diagnostics.
  import { octave, type Json } from '../lib/octave'
  import { act, toast } from '../lib/stores'
  import Icon from '../ui/Icon.svelte'
  import Modal from '../ui/Modal.svelte'
  import Toggle from '../ui/Toggle.svelte'
  import MirrorView from '../ui/MirrorView.svelte'
  import JpegView from '../ui/JpegView.svelte'

  let { arg = '' }: { arg?: string } = $props()
  type Tab = 'downloads' | 'phone' | 'diagnostics'
  let tab = $state<Tab>('downloads')
  $effect(() => { if (arg === 'downloads' || arg === 'phone' || arg === 'diagnostics') tab = arg })

  // ── downloads ──────────────────────────────────────────────────────────
  interface Result { song_id: string; name: string; artist: string; album_name: string; duration: string; is_downloaded: boolean; is_downloading: boolean; is_failed: boolean; progress: number; error_message: string; explicit: boolean; downloaded_playlist: string }
  interface Dl { downloadsAvailable: boolean; isSearching: boolean; activeDownloads: number; downloadPath: string; [k: string]: Json }
  const dl = octave.props<Dl>('downloadManager', ['downloadsAvailable', 'isSearching', 'activeDownloads', 'downloadPath'], { downloadsAvailable: false, isSearching: false, activeDownloads: 0, downloadPath: '' })
  const results = octave.model<Result>('downloadManager', 'searchResults')
  const queue = octave.model<Result>('downloadManager', 'downloadQueue')
  let query = $state('')
  let status = $state('')
  let playlists = $state<string[]>([])
  let target = $state('')
  $effect(() => {
    if (tab !== 'downloads') return
    const unsubs = [
      octave.signal('downloadManager', 'statusMessage', (m) => (status = String(m))),
      octave.signal('downloadManager', 'searchError', (m) => toast(String(m), 'bad')),
      octave.signal('downloadManager', 'downloadError', (_id, name, m) => toast(`${name}: ${m}`, 'bad')),
      octave.signal('downloadManager', 'downloadComplete', (_id, path) => toast(`Downloaded ${String(path).split('/').pop()}`)),
    ]
    octave.call<string[]>('mediaManager', 'get_playlist_names').then((p) => (playlists = p)).catch(() => {})
    octave.call<string>('downloadManager', 'get_download_playlist').then((p) => (target = p)).catch(() => {})
    return () => unsubs.forEach((u) => u())
  })
  function search() { if (query.trim()) act(octave.call('downloadManager', 'search', query.trim())) }

  // ── phone ──────────────────────────────────────────────────────────────
  interface Phone { nativeAvailable: boolean; isRunning: boolean; sessionState: string; deviceState: string; deviceHint: string; devices: { serial: string; name?: string; state?: string }[]; mirrorNotice: string
    connectionType: string; wirelessAddress: string; wirelessBusy: boolean; qrPairingActive: boolean; qrPairingRows: string[]; phoneAsleep: boolean; phoneInUse: boolean; audioActive: boolean; currentBitRate: number
    mediaAvailable: boolean; mediaPlaying: boolean; mediaTitle: string; mediaArtist: string; mediaApp: string; frameWidth: number; frameHeight: number; lastErrorKind: string; [k: string]: Json }
  const ph = octave.props<Phone>('phoneMirrorManager', ['nativeAvailable', 'isRunning', 'sessionState', 'deviceState', 'deviceHint', 'devices', 'mirrorNotice', 'connectionType', 'wirelessAddress', 'wirelessBusy',
    'qrPairingActive', 'qrPairingRows', 'phoneAsleep', 'phoneInUse', 'audioActive', 'currentBitRate', 'mediaAvailable', 'mediaPlaying', 'mediaTitle', 'mediaArtist', 'mediaApp', 'frameWidth', 'frameHeight', 'lastErrorKind'],
    { nativeAvailable: false, isRunning: false, sessionState: 'idle', deviceState: 'none', deviceHint: '', devices: [], mirrorNotice: '', connectionType: '', wirelessAddress: '', wirelessBusy: false, qrPairingActive: false, qrPairingRows: [],
      phoneAsleep: false, phoneInUse: false, audioActive: false, currentBitRate: 0, mediaAvailable: false, mediaPlaying: false, mediaTitle: '', mediaArtist: '', mediaApp: '', frameWidth: 0, frameHeight: 0, lastErrorKind: '' })
  const mirrorOn = octave.prop<boolean>('settingsManager', 'phoneMirrorEnabled', false)
  const aaOn = octave.prop<boolean>('settingsManager', 'androidAutoEnabled', false)
  interface AA { isDhuInstalled: boolean; dhuPath: string; isDhuEmbedded: boolean; [k: string]: Json }
  const aa = octave.props<AA>('androidAutoManager', ['isDhuInstalled', 'dhuPath', 'isDhuEmbedded'], { isDhuInstalled: false, dhuPath: '', isDhuEmbedded: false })
  let wireless = $state('')
  let describeState = $state('')
  $effect(() => { if (tab === 'phone') octave.call<string>('phoneMirrorManager', 'describeDeviceState', $ph.deviceState).then((d) => (describeState = d)).catch(() => {}) })
  $effect(() => {
    if (tab !== 'phone') return
    return octave.signal('phoneMirrorManager', 'wirelessResult', (ok, m) => toast(String(m), ok ? 'ok' : 'bad'))
  })
  $effect(() => { if (tab === 'phone') return octave.signal('phoneMirrorManager', 'scrcpyError', (m) => toast(String(m), 'bad')) })

  // ── diagnostics ────────────────────────────────────────────────────────
  interface D { logDir: string; appVersion: string; backendName: string; deviceInfo: string; lastExportPath: string; canOpenFolder: boolean; [k: string]: Json }
  const diag = octave.props<D>('diagnosticsManager', ['logDir', 'appVersion', 'backendName', 'deviceInfo', 'lastExportPath', 'canOpenFolder'], { logDir: '', appVersion: '', backendName: '', deviceInfo: '', lastExportPath: '', canOpenFolder: false })
  let logText = $state('')
  let license = $state<{ name: string; text: string } | null>(null)
  async function loadLog() { logText = await octave.call<string>('diagnosticsManager', 'recentLogLines', 200).catch(() => '') }
  $effect(() => { if (tab === 'diagnostics') loadLog() })
</script>

<div class="page">
  <div class="page-head">
    <h1>More</h1>
    <div class="seg" role="tablist">
      <button class:on={tab === 'downloads'} role="tab" aria-selected={tab === 'downloads'} onclick={() => (tab = 'downloads')}>Downloads</button>
      <button class:on={tab === 'phone'} role="tab" aria-selected={tab === 'phone'} onclick={() => (tab = 'phone')}>Phone</button>
      <button class:on={tab === 'diagnostics'} role="tab" aria-selected={tab === 'diagnostics'} onclick={() => (tab = 'diagnostics')}>Diagnostics</button>
    </div>
  </div>

  {#if tab === 'downloads'}
    {#if !$dl.downloadsAvailable}
      <div class="glass panel muted">Downloads are not available in this build of OCTAVE.</div>
    {:else}
      <div class="glass panel col">
        <div class="searchrow">
          <input class="field" placeholder="Search for music" bind:value={query} aria-label="Search" onkeydown={(e) => e.key === 'Enter' && search()} />
          <button class="btn primary" disabled={Boolean($dl.isSearching) || !query.trim()} onclick={search}><Icon name="search" />{$dl.isSearching ? 'Searching…' : 'Search'}</button>
        </div>
        <div class="row target"><div class="label"><span>Save into</span><span class="small">{$dl.downloadPath}</span></div>
          <select class="field sel" aria-label="Playlist" value={target} onchange={(e) => { target = (e.target as HTMLSelectElement).value; act(octave.call('downloadManager', 'set_download_playlist', target)) }}>
            {#each playlists as p}<option value={p}>{p}</option>{/each}
            {#if target && !playlists.includes(target)}<option value={target}>{target}</option>{/if}
          </select></div>
        {#if status}<p class="small muted">{status}</p>{/if}
        {#if $results.length}
          <ul class="list">
            {#each $results as r (r.song_id)}
              <li>
                <div class="label"><span>{r.name}{#if r.explicit} <span class="tag">E</span>{/if}</span><span class="small muted">{[r.artist, r.album_name, r.duration].filter(Boolean).join(' · ')}</span></div>
                {#if r.is_downloaded}<span class="small muted">In {r.downloaded_playlist || 'the library'}</span>
                {:else if r.is_downloading}<span class="small num">{Math.round(r.progress * 100)}%</span>
                {:else}<button class="btn icon" aria-label="Download {r.name}" onclick={() => act(octave.call('downloadManager', 'download', r.song_id, r.name), `Queued ${r.name}`)}><Icon name="download" /></button>{/if}
              </li>
            {/each}
          </ul>
          <button class="btn" disabled={Boolean($dl.isSearching)} onclick={() => act(octave.call('downloadManager', 'search_more'))}>More results</button>
        {/if}
      </div>
      <div class="glass panel col">
        <div class="bar"><h2>Queue</h2><span class="small muted">{$dl.activeDownloads} active</span></div>
        {#if !$queue.length}<p class="muted small">Nothing queued.</p>{/if}
        <ul class="list">
          {#each $queue as q (q.song_id)}
            <li>
              <div class="label"><span>{q.name}</span><span class="small muted">{q.is_failed ? q.error_message || 'Failed' : q.is_downloaded ? 'Done' : q.is_downloading ? `${Math.round(q.progress * 100)}%` : 'Waiting'}</span>
                {#if q.is_downloading}<div class="mini"><div style="width:{q.progress * 100}%"></div></div>{/if}</div>
              <div class="acts">
                {#if q.is_downloading}<button class="btn" onclick={() => act(octave.call('downloadManager', 'pause_download', q.song_id))}>Pause</button>
                {:else if !q.is_downloaded && !q.is_failed}<button class="btn" onclick={() => act(octave.call('downloadManager', 'resume_download', q.song_id))}>Resume</button>{/if}
                {#if !q.is_downloaded}<button class="btn icon danger" aria-label="Cancel" onclick={() => act(octave.call('downloadManager', 'cancel_download', q.song_id))}><Icon name="x" /></button>{/if}
              </div>
            </li>
          {/each}
        </ul>
        {#if $queue.length}
          <div class="acts">
            <button class="btn" onclick={() => act(octave.call('downloadManager', 'clear_finished_downloads'), 'Cleared')}>Clear finished</button>
            <button class="btn danger" onclick={() => act(octave.call('downloadManager', 'clear_download_queue'), 'Queue cleared')}>Clear queue</button>
          </div>
        {/if}
      </div>
    {/if}

  {:else if tab === 'phone'}
    <div class="grid">
      <div class="glass panel span-7 col">
        <div class="bar"><h2>Phone mirror</h2><Toggle on={$mirrorOn} label="Phone mirror" onchange={(v) => act(octave.call('settingsManager', 'save_phone_mirror_enabled', v))} /></div>
        {#if !$ph.nativeAvailable}<p class="small muted">The built-in mirror client is not available in this build.</p>{/if}
        <div class="row"><div class="label"><span>{$ph.sessionState === 'running' || $ph.isRunning ? 'Mirroring' : $ph.sessionState}</span>
          <span class="small">{describeState || $ph.deviceState}{$ph.deviceHint ? ` · ${$ph.deviceHint}` : ''}{$ph.connectionType ? ` · ${$ph.connectionType}` : ''}</span></div>
          <div class="acts">
            {#if $ph.isRunning}
              <button class="btn danger" onclick={() => act(octave.call('phoneMirrorManager', 'stopScrcpy'))}>Stop</button>
            {:else}
              <button class="btn primary" disabled={!$mirrorOn} onclick={() => act(octave.call('phoneMirrorManager', 'startScrcpy'))}>Start</button>
            {/if}
            <button class="btn icon" aria-label="Refresh" onclick={() => act(octave.call('phoneMirrorManager', 'refreshDeviceState'))}><Icon name="refresh" /></button>
          </div></div>
        {#if $ph.mirrorNotice}<div class="row"><span class="small">{$ph.mirrorNotice}</span><button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'dismissNotice'))}>Dismiss</button></div>{/if}
        {#if $ph.lastErrorKind}<p class="small bad">Last error: {$ph.lastErrorKind}</p>{/if}
        {#if $ph.isRunning}
          <div class="screen"><MirrorView showStats /></div>
          <p class="small muted">Touch the picture to use the phone. {$ph.frameWidth}×{$ph.frameHeight}{$ph.currentBitRate ? `, ${$ph.currentBitRate} Mb/s` : ''}{$ph.audioActive ? ', audio' : ''}{$ph.phoneAsleep ? ', phone asleep' : ''}{$ph.phoneInUse ? ', phone in use' : ''}</p>
          <div class="acts wrap">
            <button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'pressHome'))}>Home</button>
            <button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'pressBack'))}>Back</button>
            <button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'pressAppSwitch'))}>Apps</button>
            <button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'wakePhone'))}>Wake</button>
            <button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'resumeMirroring'))}>Resume</button>
          </div>
        {/if}
        {#if $ph.devices.length}
          <h3>Devices</h3>
          {#each $ph.devices as d}
            <div class="row"><div class="label"><span>{d.name || d.serial}</span><span class="small num">{d.serial}{d.state ? ` · ${d.state}` : ''}</span></div>
              <button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'selectDevice', d.serial))}>Use</button></div>
          {/each}
        {/if}
        <h3>Wireless</h3>
        <div class="row"><div class="label"><span>{$ph.wirelessAddress || 'No wireless address'}</span></div>
          {#if $ph.wirelessAddress}<button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'forgetWireless'), 'Forgotten')}>Forget</button>{/if}</div>
        <div class="searchrow">
          <input class="field" placeholder="ip:port" bind:value={wireless} aria-label="Wireless address" />
          <button class="btn" disabled={!wireless.trim() || $ph.wirelessBusy} onclick={() => act(octave.call('phoneMirrorManager', 'setWirelessAddress', wireless.trim()))}>Connect</button>
        </div>
        <div class="acts">
          {#if $ph.qrPairingActive}
            <button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'cancelQrPairing'))}>Cancel pairing</button>
          {:else}
            <button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'startQrPairing'))}>Pair with a QR code</button>
          {/if}
        </div>
        {#if $ph.qrPairingActive && $ph.qrPairingRows.length}
          <pre class="qr">{$ph.qrPairingRows.join('\n')}</pre>
          <p class="small muted">Scan this from the phone's wireless debugging page.</p>
        {/if}
        {#if $ph.mediaAvailable}
          <h3>Phone media</h3>
          <div class="row"><div class="label"><span>{$ph.mediaTitle}</span><span class="small">{$ph.mediaArtist}{$ph.mediaApp ? ` · ${$ph.mediaApp}` : ''}</span></div>
            <div class="acts">
              <button class="btn icon" aria-label="Previous" onclick={() => act(octave.call('phoneMirrorManager', 'mediaPrevious'))}><Icon name="prev" /></button>
              <button class="btn icon primary" aria-label="Play or pause" onclick={() => act(octave.call('phoneMirrorManager', 'mediaPlayPause'))}><Icon name={$ph.mediaPlaying ? 'pause' : 'play'} /></button>
              <button class="btn icon" aria-label="Next" onclick={() => act(octave.call('phoneMirrorManager', 'mediaNext'))}><Icon name="next" /></button>
            </div></div>
        {/if}
      </div>
      <div class="glass panel span-5 col">
        <div class="bar"><h2>Android Auto</h2><Toggle on={$aaOn} label="Android Auto" onchange={(v) => act(octave.call('settingsManager', 'save_android_auto_enabled', v))} /></div>
        <p class="small muted">{$aa.isDhuInstalled ? `Desktop Head Unit at ${$aa.dhuPath}` : 'The Desktop Head Unit is not installed.'}{$aa.isDhuEmbedded ? ' · running' : ''}</p>
        {#if $aa.isDhuEmbedded}<div class="screen"><JpegView showStats /></div>{/if}
        <div class="acts wrap">
          <button class="btn primary" disabled={!$aaOn || !$aa.isDhuInstalled} onclick={() => act(octave.call('androidAutoManager', 'launchDhuSeamless'))}>Launch</button>
          <button class="btn" disabled={!$aa.isDhuEmbedded} onclick={() => act(octave.call('androidAutoManager', 'closeDhu'))}>Close</button>
          <button class="btn" onclick={async () => { const t = await octave.call<string>('androidAutoManager', 'getDhuInstallInstructions').catch(() => ''); if (t) license = { name: 'Installing the Desktop Head Unit', text: t } }}>How to install</button>
        </div>
      </div>
    </div>

  {:else}
    <div class="glass panel col">
      <div class="bar"><h2>Diagnostics</h2><span class="small muted">{$diag.appVersion} · {$diag.backendName}</span></div>
      <p class="small muted">{$diag.deviceInfo}</p>
      <p class="small muted num">Logs in {$diag.logDir}</p>
      <div class="acts wrap">
        <button class="btn primary" onclick={async () => { const p = await act(octave.call<string>('diagnosticsManager', 'exportLogs')); if (p) toast(`Exported to ${p}`) }}><Icon name="share" />Export logs</button>
        <button class="btn" onclick={() => act(octave.call('diagnosticsManager', 'copyLogsToClipboard', 500), 'Copied on the head unit')}>Copy to clipboard</button>
        {#if $diag.canOpenFolder}<button class="btn" onclick={() => act(octave.call('diagnosticsManager', 'openLogFolder'))}><Icon name="folder" />Open folder</button>{/if}
        <button class="btn icon" aria-label="Refresh" onclick={loadLog}><Icon name="refresh" /></button>
      </div>
      {#if $diag.lastExportPath}<p class="small muted num">Last export: {$diag.lastExportPath}</p>{/if}
      <pre class="log small">{logText || 'No log lines.'}</pre>
    </div>
  {/if}
</div>

{#if license}
  <Modal title={license.name} onclose={() => (license = null)} wide>
    <pre class="log small">{license.text}</pre>
  </Modal>
{/if}

<style>
  .col { display: flex; flex-direction: column; gap: 12px; }
  .bar { display: flex; justify-content: space-between; align-items: center; gap: 12px; flex-wrap: wrap; }
  .searchrow { display: flex; gap: 8px; }
  .sel { max-width: 240px; }
  .list { list-style: none; margin: 0; padding: 0; }
  .list li { display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 10px 0; border-top: 1px solid var(--glass-edge); }
  .list .label { display: flex; flex-direction: column; min-width: 0; flex: 1; }
  .list .label > span:first-child { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .tag { font-size: 11px; padding: 1px 6px; border-radius: 6px; background: var(--control); vertical-align: middle; }
  .acts { display: flex; gap: 6px; } .wrap { flex-wrap: wrap; gap: 8px; }
  .mini { height: 5px; border-radius: 3px; background: var(--track); overflow: hidden; margin-top: 6px; } .mini div { height: 100%; background: var(--accent); }
  .log { margin: 0; max-height: 420px; overflow: auto; background: var(--control); border-radius: var(--radius-s); padding: 14px; white-space: pre-wrap; overflow-wrap: anywhere; font-family: ui-monospace, monospace; }
  .qr { font-family: ui-monospace, monospace; line-height: 1; font-size: 10px; background: #fff; color: #000; padding: 12px; border-radius: 12px; display: inline-block; }
  .bad { color: var(--bad); }
  .screen { height: 420px; }
  @media (max-width: 760px) { .screen { height: 60vh; } .target { flex-direction: column; align-items: stretch; } .target .sel { max-width: none; } }
</style>
