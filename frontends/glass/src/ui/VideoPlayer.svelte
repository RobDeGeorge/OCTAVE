<script lang="ts">
  // The mirror picture: a canvas the H264Player paints, with a touch layer
  // that maps a finger on the picture to frame-relative coordinates (what
  // phoneMirrorManager.injectTouch takes). The page decides where touches
  // go; this component never talks to the service itself.
  import { onMount } from 'svelte'
  import { H264Player, type FedUnit, type Stats } from '../lib/videodecoder'

  /** Android MotionEvent actions, as injectTouch expects them. */
  const DOWN = 0, UP = 1, MOVE = 2
  let { source, ontouch, showStats = false, onstats }:
    { source: { subscribe: (cb: (u: FedUnit) => void) => () => void }; ontouch?: (pointerId: number, action: number, relX: number, relY: number) => void;
      showStats?: boolean; onstats?: (s: Stats) => void } = $props()
  let canvas = $state<HTMLCanvasElement | null>(null)
  let player: H264Player | null = null
  let stats = $state<Stats | null>(null)
  let unsupported = $state(false)
  const pressed = new Map<number, true>()

  onMount(() => {
    if (!H264Player.supported()) { unsupported = true; return }
    player = new H264Player(canvas!)
    player.onstats = (s) => { stats = s; onstats?.(s) }
    const unsub = source.subscribe((u) => player!.feed(u))
    return () => { unsub(); player!.close() }
  })

  function touch(e: PointerEvent, action: number) {
    if (!player || !ontouch) return
    const r = canvas!.getBoundingClientRect()
    const f = player.toFrame(e.clientX - r.left, e.clientY - r.top)
    if (action === DOWN) { if (!f) return; pressed.set(e.pointerId, true); canvas!.setPointerCapture(e.pointerId) }
    else if (!pressed.has(e.pointerId)) return
    if (action === UP) pressed.delete(e.pointerId)
    const [x, y] = f ?? [Math.min(1, Math.max(0, (e.clientX - r.left - player.rect.x) / player.rect.w)), Math.min(1, Math.max(0, (e.clientY - r.top - player.rect.y) / player.rect.h))]
    ontouch(e.pointerId, action, x, y)
  }
</script>

<div class="video">
  {#if unsupported}
    <p class="muted">This browser cannot decode H.264 with WebCodecs. Chromium 94 or newer, on a secure origin.</p>
  {:else}
    <canvas bind:this={canvas} onpointerdown={(e) => touch(e, DOWN)} onpointermove={(e) => touch(e, MOVE)}
            onpointerup={(e) => touch(e, UP)} onpointercancel={(e) => touch(e, UP)}></canvas>
    {#if showStats && stats}
      <div class="stats num small">{stats.codec} {stats.width}x{stats.height} · {stats.state} · {stats.decoded} frames · {stats.dropped} dropped · queue {stats.queued}{stats.errors ? ` · ${stats.errors} errors` : ''}</div>
    {/if}
  {/if}
</div>

<style>
  .video { position: relative; width: 100%; height: 100%; background: #000; border-radius: var(--radius-s); overflow: hidden; }
  canvas { width: 100%; height: 100%; display: block; touch-action: none; }
  .stats { position: absolute; left: 10px; bottom: 8px; padding: 4px 10px; border-radius: 999px; background: rgba(0,0,0,.55); color: #fff; pointer-events: none; }
  .muted { padding: 20px; color: #aaa; }
</style>
