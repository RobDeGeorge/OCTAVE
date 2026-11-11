<script lang="ts">
  let { heading = 0, color = '', showDegrees = true }: { heading?: number; color?: string; showDegrees?: boolean } = $props()
  const card = $derived(['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'][Math.round((((heading % 360) + 360) % 360) / 45) % 8])
</script>

<div class="cp" style:--c={color || 'var(--bad)'}>
  <svg viewBox="0 0 100 100" aria-label="Heading {Math.round(heading)}°">
    <circle class="ring" cx="50" cy="50" r="44" />
    <g style="transform: rotate({-heading}deg); transform-origin: 50px 50px; transition: transform .2s ease-out">
      {#each Array.from({ length: 36 }, (_, k) => k * 10) as a}
        <line class={a % 90 === 0 ? 'major' : 'minor'} x1="50" y1="6" x2="50" y2={a % 90 === 0 ? 14 : 10} transform="rotate({a} 50 50)" />
      {/each}
      {#each [['N', 0], ['E', 90], ['S', 180], ['W', 270]] as [l, a]}
        <text class="card" class:n={l === 'N'} x="50" y="24" text-anchor="middle" transform="rotate({a} 50 50)">{l}</text>
      {/each}
    </g>
    <path class="lubber" d="M50 2 L46 9 L54 9 Z" />
    <text class="big" x="50" y="56" text-anchor="middle">{card}</text>
    {#if showDegrees}<text class="deg num" x="50" y="70" text-anchor="middle">{Math.round(heading)}°</text>{/if}
  </svg>
</div>

<style>
  .cp { width: 100%; height: 100%; display: flex; align-items: center; justify-content: center; }
  svg { width: 100%; height: 100%; }
  .ring { fill: none; stroke: var(--track); stroke-width: 1; }
  .minor { stroke: var(--ink-3); stroke-width: .8; } .major { stroke: var(--ink-2); stroke-width: 1.5; }
  .card { fill: var(--ink-2); font-size: 8px; font-weight: 700; } .card.n { fill: var(--c); }
  .lubber { fill: var(--ink); }
  .big { fill: var(--ink); font-size: 20px; font-weight: 700; }
  .deg { fill: var(--ink-2); font-size: 8px; font-weight: 600; }
</style>
