<script lang="ts">
  // What is playing, with transport, seek, shuffle and volume. The Home page
  // uses the compact form, the Media page the full one.
  import { octave } from '../lib/octave'
  import { nowPlaying, position, volume, mmss, act, artSrc, levels } from '../lib/stores'
  import Icon from './Icon.svelte'
  import Slider from './Slider.svelte'
  import Art from './Art.svelte'

  let { compact = false }: { compact?: boolean } = $props()
  const np = nowPlaying
  let seeking = $state(false)
  let seekTo = $state(0)
  const pos = $derived(seeking ? seekTo : $position)
  const wave = $derived(compact ? null : levels())
</script>

<div class="glass panel card" class:compact>
  <div class="top">
    <Art title={$np.title} artist={$np.artist} url={$artSrc} size={compact ? 110 : 200} />
    <div class="text">
      <div class="src faint small">{$np.source === 'phone' ? 'From the phone' : $np.hasTrack ? 'Playing from the library' : 'Library'}</div>
      <div class="title">{$np.hasTrack ? $np.title || 'Untitled' : 'Nothing playing'}</div>
      <div class="artist muted">{[$np.artist, $np.album].filter(Boolean).join(' – ') || ($np.hasTrack ? '' : 'Pick a song in Media')}</div>
    </div>
  </div>

  {#if wave && $np.hasTrack}
    <div class="wave" aria-hidden="true">
      {#each $wave as v, i (i)}<span style="height:{Math.max(6, (v / 8) * 100)}%"></span>{/each}
    </div>
  {/if}

  <div class="seek">
    <Slider value={pos} min={0} max={Math.max(1, $np.durationMs)} step={500} label="Position" disabled={!$np.canSeek}
            format={() => ''} oninput={(v) => { seeking = true; seekTo = v }}
            onchange={(v) => { seeking = false; act(octave.call('nowPlaying', 'seek', Math.round(v))) }} />
    <div class="times num small muted"><span>{mmss(pos)}</span><span>{mmss($np.durationMs)}</span></div>
  </div>

  <div class="transport">
    <button class="btn icon" class:on={$np.shuffled} disabled={!$np.canShuffle} aria-label="Shuffle" onclick={() => act(octave.call('nowPlaying', 'toggleShuffle'))}><Icon name="shuffle" /></button>
    <button class="btn icon big" aria-label="Previous" onclick={() => act(octave.call('nowPlaying', 'previous'))}><Icon name="prev" /></button>
    <button class="btn icon big primary" aria-label={$np.playing ? 'Pause' : 'Play'} onclick={() => act(octave.call('nowPlaying', 'togglePlay'))}><Icon name={$np.playing ? 'pause' : 'play'} size={30} /></button>
    <button class="btn icon big" aria-label="Next" onclick={() => act(octave.call('nowPlaying', 'next'))}><Icon name="next" /></button>
    <button class="btn icon" class:on={$volume.muted} aria-label={$volume.muted ? 'Unmute' : 'Mute'} onclick={() => act(octave.set('volumeController', 'muted', !$volume.muted))}><Icon name={$volume.effectivelyMuted ? 'mute' : 'volume'} /></button>
  </div>

  <div class="vol">
    <Icon name="volume" size={20} />
    <Slider value={$volume.percent} label="Volume" format={(v) => `${v}%`}
            oninput={(v) => octave.call('volumeController', 'applyVolume', v).catch(() => {})}
            onchange={(v) => act(octave.call('volumeController', 'applyVolume', v))} />
  </div>
</div>

<style>
  .card { display: flex; flex-direction: column; gap: 16px; min-width: 0; }
  .top { display: flex; gap: 22px; align-items: center; min-width: 0; }
  .text { min-width: 0; display: flex; flex-direction: column; gap: 4px; }
  .title { font-size: 30px; font-weight: 700; letter-spacing: -.02em; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .compact .title { font-size: 24px; }
  .artist { font-size: 18px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .wave { display: flex; align-items: flex-end; gap: 2px; height: 56px; }
  .wave span { flex: 1; min-width: 0; border-radius: 2px 2px 0 0; background: var(--accent); opacity: .7; transition: height .08s linear; }
  .times { display: flex; justify-content: space-between; margin-top: -8px; padding: 0 4px; }
  .transport { display: flex; justify-content: center; align-items: center; gap: 14px; }
  .vol { display: flex; align-items: center; gap: 12px; color: var(--ink-2); }
  .compact .vol { display: none; }
  @media (max-width: 760px) {
    .top { flex-direction: column; align-items: flex-start; gap: 14px; }
    .compact .top { flex-direction: row; align-items: center; }
    .title { font-size: 24px; } .artist { font-size: 15px; }
    .transport { gap: 8px; }
    .transport .big { min-width: 60px; min-height: 60px; }
  }
</style>
