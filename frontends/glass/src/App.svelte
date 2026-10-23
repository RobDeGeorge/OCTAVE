<script lang="ts">
  import Backdrop from './ui/Backdrop.svelte'
  import Nav from './ui/Nav.svelte'
  import Icon from './ui/Icon.svelte'
  import { route } from './lib/router'
  import { octave } from './lib/octave'
  import { toasts, activeTheme } from './lib/stores'
  import { tint, applyAccent } from './lib/theme'
  import Home from './pages/Home.svelte'
  import Media from './pages/Media.svelte'
  import Obd from './pages/Obd.svelte'
  import Dashboards from './pages/Dashboards.svelte'
  import Trips from './pages/Trips.svelte'
  import Settings from './pages/Settings.svelte'
  import More from './pages/More.svelte'
  import Mirror from './pages/Mirror.svelte'

  const status = octave.status
  const retryIn = octave.retryIn
  const pages: Record<string, any> = { home: Home, media: Media, obd: Obd, dashboards: Dashboards, trips: Trips, settings: Settings, more: More, mirror: Mirror }
  const Page = $derived(pages[$route.page] ?? Home)
  $effect(() => { applyAccent($tint ? (($activeTheme.accent as string) ?? null) : null) })
</script>

<Backdrop />
<Nav />

<main class:offline={$status !== 'online'}>
  {#if $status !== 'online'}
    <div class="banner glass strong" role="status">
      <Icon name={$status === 'mismatch' ? 'warn' : 'unlink'} />
      {#if $status === 'connecting'}Connecting to OCTAVE…
      {:else if $status === 'mismatch'}OCTAVE speaks another protocol version. Update this page or the app.
      {:else}OCTAVE is not answering. Retrying{$retryIn ? ` in ${$retryIn}s` : '…'}
      {/if}
    </div>
  {/if}
  <Page arg={$route.arg} />
</main>

<div class="toasts" aria-live="polite">
  {#each $toasts as t (t.id)}
    <div class="toast glass strong" class:bad={t.kind === 'bad'}>{t.text}</div>
  {/each}
</div>

<style>
  main { position: fixed; inset: 0; left: calc(var(--rail) + var(--gap)); overflow-y: auto; overflow-x: hidden; z-index: 1;
         scrollbar-width: thin; }
  @media (max-width: 760px), (max-aspect-ratio: 1/1) {
    main { left: 0; bottom: calc(var(--tab) + var(--gap)); }
  }
  .banner { margin: var(--gap) var(--gap) 0; padding: 14px 20px; display: flex; gap: 12px; align-items: center; font-weight: 600; }
  .toasts { position: fixed; z-index: 20; left: 50%; bottom: 28px; transform: translateX(-50%); display: flex; flex-direction: column; gap: 8px; pointer-events: none; }
  .toast { padding: 14px 22px; border-radius: 999px; font-weight: 600; animation: pop .25s var(--ease); }
  .toast.bad { color: var(--bad); }
  @keyframes pop { from { opacity: 0; transform: translateY(10px); } }
</style>
