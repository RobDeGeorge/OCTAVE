<script lang="ts">
  // The phone's screen, live: phoneMirrorManager's "video" stream decoded
  // by the WebCodecs player, touches sent back with injectTouch. Opened
  // again whenever the link comes back.
  import { octave } from '../lib/octave'
  import { act } from '../lib/stores'
  import type { FedUnit } from '../lib/videodecoder'
  import type { MediaRecord } from 'octave-client'
  import VideoPlayer from './VideoPlayer.svelte'

  let { touch = true, showStats = false }: { touch?: boolean; showStats?: boolean } = $props()
  let subs = new Set<(u: FedUnit) => void>()
  let skip = $state<(() => void) | null>(null)
  let info = $state<{ codec?: string; width?: number; height?: number } | null>(null)
  let dropped = $state(0)
  let error = $state('')
  const source = { subscribe(cb: (u: FedUnit) => void) { subs.add(cb); return () => subs.delete(cb) } }

  function onRecord(r: MediaRecord) {
    if (r.jpeg) return
    if (r.discontinuity) skip?.()
    for (const cb of subs) cb({ data: r.payload, keyframe: r.keyframe, pts: r.pts })
  }
  $effect(() => {
    let closer: (() => void) | null = null
    let gone = false
    const unsub = octave.status.subscribe(async (s) => {
      if (s !== 'online') { closer = null; return }
      try {
        const st = await octave.stream('phoneMirrorManager', 'video', {
          onRecord,
          onConfig: (i) => { info = i },
          onDropped: (n) => { dropped += n; skip?.() },
        })
        if (gone) { st.close(); return }
        closer = st.close
        error = ''
      } catch (e) { error = (e as Error).message }
    })
    return () => { gone = true; unsub(); closer?.() }
  })
  function ontouch(id: number, action: number, x: number, y: number) {
    if (touch) act(octave.call('phoneMirrorManager', 'injectTouch', id, action, x, y))
  }
</script>

<div class="mirror">
  {#if error}
    <p class="muted small">No picture: {error}</p>
  {:else}
    <VideoPlayer {source} ontouch={touch ? ontouch : undefined} {showStats} bind:skipToKeyframe={skip} />
  {/if}
  {#if info?.width && showStats}<span class="small faint">{info.codec} {info.width}x{info.height}{dropped ? ` · ${dropped} dropped by the service` : ''}</span>{/if}
</div>

<style>
  .mirror { width: 100%; height: 100%; display: flex; flex-direction: column; gap: 6px; }
</style>
