<script lang="ts">
  // The room behind the glass: three soft blobs in the album's colours
  // (settingsManager.albumArtColors, computed in C++), drifting slowly. With
  // no art, the hue comes from the track title so every song still has its
  // own light.
  import { albumColors, nowPlaying } from '../lib/stores'

  function hueOf(s: string): number {
    let h = 0
    for (const c of s) h = (h * 31 + c.charCodeAt(0)) >>> 0
    return h % 360
  }
  const colors = $derived.by(() => {
    const c = $albumColors as Record<string, string>
    if (c && typeof c.base === 'string' && typeof c.accent === 'string')
      return [c.base, c.accent, (c.baseAlt as string) ?? c.base]
    const h = hueOf($nowPlaying.title || 'OCTAVE')
    return [`oklch(45% .12 ${h})`, `oklch(55% .16 ${(h + 50) % 360})`, `oklch(40% .1 ${(h + 300) % 360})`]
  })
</script>

<div class="backdrop" aria-hidden="true" style="--c1:{colors[0]};--c2:{colors[1]};--c3:{colors[2]}">
  <div class="blob a"></div>
  <div class="blob b"></div>
  <div class="blob c"></div>
</div>

<style>
  .backdrop { position: fixed; inset: 0; overflow: hidden; z-index: 0; background: var(--bg); }
  .blob { position: absolute; width: 70vmax; height: 70vmax; border-radius: 50%; filter: blur(80px);
          opacity: var(--blob-opacity); transition: background 2.5s var(--ease); will-change: transform; }
  .a { background: var(--c1); top: -20vmax; left: -15vmax; animation: driftA 38s ease-in-out infinite alternate; }
  .b { background: var(--c2); bottom: -25vmax; right: -10vmax; animation: driftB 47s ease-in-out infinite alternate; }
  .c { background: var(--c3); top: 30%; left: 35%; width: 50vmax; height: 50vmax; animation: driftC 55s ease-in-out infinite alternate; }
  @keyframes driftA { to { transform: translate(18vmax, 12vmax) scale(1.15); } }
  @keyframes driftB { to { transform: translate(-16vmax, -14vmax) scale(.9); } }
  @keyframes driftC { to { transform: translate(-12vmax, 10vmax) scale(1.2); } }
</style>
