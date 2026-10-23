<script lang="ts">
  import VideoPlayer from '../ui/VideoPlayer.svelte'
  import { groupAccessUnits, type AccessUnit } from '../lib/h264'
  import type { FedUnit, Stats } from '../lib/videodecoder'

  let units: AccessUnit[] = []   // not reactive on purpose: a plain array read by the ticker
  let count = $state(0)
  let loaded = $state(false)
  let error = $state('')
  let stalled = $state(false)
  let burst = $state(0)
  let stats = $state<Stats | null>(null)
  let touches = $state<string[]>([])
  let fps = $state(30)
  let subs = new Set<(u: FedUnit) => void>()
  let i = 0, t0 = 0, sent = 0
  const source = { subscribe(cb: (u: FedUnit) => void) { subs.add(cb); return () => subs.delete(cb) } }

  async function load() {
    try {
      const r = await fetch('/dev/test.h264')
      if (!r.ok) throw new Error(`${r.status} ${r.statusText}: run dev/make_test_video.sh`)
      units = groupAccessUnits(new Uint8Array(await r.arrayBuffer()), 30)
      count = units.length
      loaded = true
      t0 = performance.now()
      tick()
    } catch (e) { error = (e as Error).message }
  }
  function emit() {
    const u = units[i % units.length]
    for (const cb of subs) cb({ data: u.data, keyframe: u.keyframe, pts: sent * Math.round(1e6 / fps) })
    i++; sent++
  }
  function tick() {
    requestAnimationFrame(tick)
    if (stalled) return
    // catch up to the wall clock; a burst after a stall is what a real link does
    const due = Math.floor(((performance.now() - t0) / 1000) * fps)
    let n = 0
    while (sent < due && n++ < 400) emit()
  }
  function stall(ms: number) { stalled = true; setTimeout(() => { stalled = false }, ms) }
  function sendBurst(n: number) { burst = n; for (let k = 0; k < n; k++) emit(); burst = 0 }
  load()
  function ontouch(id: number, action: number, x: number, y: number) {
    touches = [`${['down', 'up', 'move'][action]} #${id} ${x.toFixed(3)},${y.toFixed(3)}`, ...touches].slice(0, 8)
  }
  // exposed for the scripted check
  ;(window as unknown as { glassVideo: unknown }).glassVideo = { stall, sendBurst, get stats() { return stats }, get touches() { return touches }, get units() { return units.length } }
</script>

<div class="page">
  <div class="page-head"><h1>Video player</h1><span class="muted small">dev/test.h264 through the WebCodecs player</span></div>
  {#if error}<div class="glass panel" style="color:var(--bad)">{error}</div>{/if}
  <div class="glass panel stage">
    {#if loaded}<VideoPlayer {source} {ontouch} showStats onstats={(s) => (stats = s)} />{:else}<p class="muted">Loading the stream…</p>{/if}
  </div>
  <div class="glass panel row-wrap">
    <button class="btn" onclick={() => stall(1500)}>Stall 1.5 s</button>
    <button class="btn" onclick={() => sendBurst(90)}>Burst 90 frames</button>
    <label class="small muted">fps <input class="field tiny" type="number" bind:value={fps} min="1" max="120" /></label>
    <span class="small muted">{stalled ? 'stalled' : burst ? `burst ${burst}` : 'playing'} · {count} access units in the file</span>
  </div>
  <div class="glass panel">
    <h3>Touches (logged only)</h3>
    <pre class="small">{touches.join('\n') || 'Tap or drag on the picture.'}</pre>
  </div>
</div>

<style>
  .stage { height: 420px; padding: 12px; }
  .row-wrap { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }
  .tiny { width: 90px; }
  pre { margin: 0; font-family: ui-monospace, monospace; }
</style>
