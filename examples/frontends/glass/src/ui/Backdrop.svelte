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
  // a new arrangement per track: the blobs glide there once, then rest
  const seed = $derived(hueOf($nowPlaying.title + $nowPlaying.artist))
  const pos = $derived([
    { x: -15 + (seed % 23), y: -20 + ((seed * 7) % 17), s: 1 + ((seed * 3) % 20) / 100 },
    { x: -10 - ((seed * 5) % 19), y: -25 - ((seed * 11) % 13), s: 0.9 + ((seed * 13) % 20) / 100 },
    { x: -12 + ((seed * 17) % 25), y: 10 - ((seed * 19) % 21), s: 1.05 + ((seed * 23) % 20) / 100 },
  ])
  const colors = $derived.by(() => {
    const c = $albumColors as Record<string, string>
    if (c && typeof c.base === 'string' && typeof c.accent === 'string')
      return [c.base, c.accent, (c.baseAlt as string) ?? c.base]
    const h = hueOf($nowPlaying.title || 'OCTAVE')
    return [`oklch(45% .12 ${h})`, `oklch(55% .16 ${(h + 50) % 360})`, `oklch(40% .1 ${(h + 300) % 360})`]
  })
</script>

<div class="backdrop" aria-hidden="true" style="--c1:{colors[0]};--c2:{colors[1]};--c3:{colors[2]}">
  <div class="blob a" style="transform:translate({pos[0].x}vmax,{pos[0].y}vmax) scale({pos[0].s})"></div>
  <div class="blob b" style="transform:translate({pos[1].x}vmax,{pos[1].y}vmax) scale({pos[1].s})"></div>
  <div class="blob c" style="transform:translate({pos[2].x}vmax,{pos[2].y}vmax) scale({pos[2].s})"></div>
</div>

<style>
  .backdrop { position: fixed; inset: 0; overflow: hidden; z-index: 0; background: var(--bg); }
  .blob { position: absolute; width: 70vmax; height: 70vmax; border-radius: 50%; filter: blur(80px);
          opacity: var(--blob-opacity); transition: background 2.5s var(--ease), transform 3s var(--ease); }
  .a { background: var(--c1); top: -20vmax; left: -15vmax; }
  .b { background: var(--c2); bottom: -25vmax; right: -10vmax; }
  .c { background: var(--c3); top: 30%; left: 35%; width: 50vmax; height: 50vmax; }
  /* low power: no blur to composite, a soft radial gradient does the job */
  :global(:root[data-power='low']) .blob { filter: none; transition: none; opacity: 1;
    background: radial-gradient(circle at center, color-mix(in oklab, var(--c, var(--c1)) 55%, transparent), transparent 70%) !important; }
  :global(:root[data-power='low']) .a { --c: var(--c1); } :global(:root[data-power='low']) .b { --c: var(--c2); } :global(:root[data-power='low']) .c { --c: var(--c3); }
</style>
