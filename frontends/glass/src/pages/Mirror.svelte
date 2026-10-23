<script lang="ts">
  // The phone's screen, full size: phoneMirrorManager's video stream with
  // touch, whatever the session state (the stream itself says whether a
  // picture is coming). The Phone tab links here.
  import { octave, type Json } from '../lib/octave'
  import { act } from '../lib/stores'
  import { go } from '../lib/router'
  import Icon from '../ui/Icon.svelte'
  import MirrorView from '../ui/MirrorView.svelte'
  interface P { isRunning: boolean; sessionState: string; phoneAsleep: boolean; [k: string]: Json }
  const ph = octave.props<P>('phoneMirrorManager', ['isRunning', 'sessionState', 'phoneAsleep'], { isRunning: false, sessionState: 'idle', phoneAsleep: false })
  let stats = $state(false)
</script>

<div class="mirror-page">
  <div class="bar">
    <button class="btn icon" aria-label="Back" onclick={() => go('more', 'phone')}><Icon name="back" /></button>
    <span class="muted small">{$ph.isRunning ? 'Mirroring' : $ph.sessionState}{$ph.phoneAsleep ? ' · phone asleep' : ''}</span>
    <span class="spacer"></span>
    <button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'pressBack'))}>Back</button>
    <button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'pressHome'))}>Home</button>
    <button class="btn" onclick={() => act(octave.call('phoneMirrorManager', 'pressAppSwitch'))}>Apps</button>
    <button class="btn icon" class:on={stats} aria-label="Statistics" onclick={() => (stats = !stats)}><Icon name="activity" /></button>
  </div>
  <div class="screen"><MirrorView showStats={stats} /></div>
</div>

<style>
  .mirror-page { display: flex; flex-direction: column; height: 100%; padding: var(--gap); gap: 12px; }
  .bar { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
  .spacer { flex: 1; }
  .screen { flex: 1; min-height: 0; }
</style>
