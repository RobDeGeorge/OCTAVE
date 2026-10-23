<script lang="ts">
  // Network / updates, power, and about: the small Device sections.
  import { octave, type Json } from '../lib/octave'
  import { act } from '../lib/stores'
  import Modal from './Modal.svelte'
  import Icon from './Icon.svelte'
  import { paired, forgetDevice } from '../lib/pairing'
  let { which }: { which: 'network' | 'power' | 'about' } = $props()
  interface N { networkName: string; isConnected: boolean; updateStatus: string; updateMessage: string; remoteCommit: string; remoteCommitMsg: string;
                selfUpdateStatus: string; selfUpdateMessage: string; canSelfUpdate: boolean; workingTreeDirty: boolean; [k: string]: Json }
  const net = octave.props<N>('networkManager', ['networkName', 'isConnected', 'updateStatus', 'updateMessage', 'remoteCommit', 'remoteCommitMsg', 'selfUpdateStatus', 'selfUpdateMessage', 'canSelfUpdate', 'workingTreeDirty'],
    { networkName: '', isConnected: false, updateStatus: '', updateMessage: '', remoteCommit: '', remoteCommitMsg: '', selfUpdateStatus: '', selfUpdateMessage: '', canSelfUpdate: false, workingTreeDirty: false })
  const hello = octave.hello
  interface D { appVersion: string; backendName: string; deviceInfo: string; logDir: string; [k: string]: Json }
  const diag = octave.props<D>('diagnosticsManager', ['appVersion', 'backendName', 'deviceInfo', 'logDir'], { appVersion: '', backendName: '', deviceInfo: '', logDir: '' })
  let git = $state({ hash: '', date: '', msg: '' })
  $effect(() => {
    if (which !== 'about') return
    Promise.all([octave.call<string>('settingsManager', 'get_git_commit_hash'), octave.call<string>('settingsManager', 'get_git_commit_date'), octave.call<string>('settingsManager', 'get_git_commit_message')])
      .then(([hash, date, msg]) => (git = { hash, date, msg })).catch(() => {})
  })
  let confirm = $state<'restart' | 'shutdown' | 'defaults' | 'forget' | null>(null)
</script>

{#if which === 'network'}
  <div class="row"><div class="label"><span>{$net.isConnected ? $net.networkName || 'Connected' : 'Offline'}</span><span class="small">{$net.updateMessage || $net.updateStatus}</span></div>
    <div class="acts"><button class="btn icon" aria-label="Refresh" onclick={() => act(octave.call('networkManager', 'refreshNetwork'))}><Icon name="refresh" /></button>
    <button class="btn" onclick={() => act(octave.call('networkManager', 'checkForUpdates'), 'Checking')}>Check for updates</button></div></div>
  {#if $net.remoteCommit}<p class="small muted">Latest: <span class="num">{$net.remoteCommit.slice(0, 8)}</span> {$net.remoteCommitMsg}</p>{/if}
  {#if $net.canSelfUpdate}
    <div class="row"><div class="label"><span>Update OCTAVE</span><span class="small">{$net.selfUpdateMessage || $net.selfUpdateStatus}{$net.workingTreeDirty ? ' (local changes present)' : ''}</span></div>
      <button class="btn primary" onclick={() => act(octave.call('networkManager', 'applySelfUpdate'), 'Updating')}>Update</button></div>
  {/if}
{:else if which === 'power'}
  {#if $paired}
    <div class="row"><div class="label"><span>This browser is paired</span><span class="small">Its token lets it use OCTAVE over the network</span></div>
      <button class="btn danger" onclick={() => (confirm = 'forget')}>Forget this device</button></div>
  {/if}
  <div class="acts wrap">
    <button class="btn" onclick={() => (confirm = 'restart')}><Icon name="refresh" />Restart OCTAVE</button>
    <button class="btn danger" onclick={() => (confirm = 'shutdown')}><Icon name="power" />Shut down the device</button>
    <button class="btn" onclick={() => (confirm = 'defaults')}>Reset every setting</button>
  </div>
{:else}
  <div class="about small">
    <div><span class="muted">App</span> {$diag.appVersion || $hello?.app.version} ({$diag.backendName}{$hello?.app.build ? `, ${$hello.app.build}` : ''}{$hello?.app.headless ? ', headless' : ''})</div>
    <div><span class="muted">Device</span> {$diag.deviceInfo}</div>
    {#if git.hash}<div><span class="muted">Commit</span> <span class="num">{git.hash.slice(0, 10)}</span> {git.date} {git.msg}</div>{/if}
    <div><span class="muted">Logs</span> <span class="num">{$diag.logDir}</span></div>
    <div><span class="muted">Protocol</span> {$hello?.protocolVersion}</div>
  </div>
{/if}

{#if confirm}
  <Modal title={confirm === 'restart' ? 'Restart OCTAVE?' : confirm === 'shutdown' ? 'Shut down the head unit?' : confirm === 'forget' ? 'Forget this device?' : 'Reset every setting?'} onclose={() => (confirm = null)}>
    <p class="muted">{confirm === 'shutdown' ? 'The device powers off. Music stops.' : confirm === 'restart' ? 'The app closes and starts again; this page reconnects.' : confirm === 'forget' ? 'OCTAVE drops this browser and its token. Over the network it will have to pair again.' : 'Every setting goes back to its default. Playlists and trips stay.'}</p>
    <button class="btn danger on" onclick={() => {
      const c = confirm; confirm = null
      if (c === 'forget') forgetDevice()
      else if (c === 'restart') act(octave.call('networkManager', 'restartApp'))
      else if (c === 'shutdown') act(octave.call('networkManager', 'shutdown_device'))
      else act(octave.call('settingsManager', 'reset_to_defaults'), 'Settings reset')
    }}>{confirm === 'restart' ? 'Restart' : confirm === 'shutdown' ? 'Shut down' : confirm === 'forget' ? 'Forget' : 'Reset'}</button>
  </Modal>
{/if}

<style>
  .acts { display: flex; gap: 6px; } .wrap { flex-wrap: wrap; gap: 8px; }
  .about { display: flex; flex-direction: column; gap: 6px; overflow-wrap: anywhere; }
  .about .muted { display: inline-block; min-width: 80px; }
</style>
