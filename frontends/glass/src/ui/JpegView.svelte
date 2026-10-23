<script lang="ts">
  // Android Auto's picture: androidAutoManager's "frames" stream, one whole
  // JPEG per record, painted on a canvas with letterboxing.
  import { onMount } from 'svelte'
  import { octave } from '../lib/octave'
  import type { MediaRecord } from '../lib/stream'
  let { showStats = false }: { showStats?: boolean } = $props()
  let canvas = $state<HTMLCanvasElement | null>(null)
  let frames = $state(0)
  let size = $state('')
  let error = $state('')
  let busy = false, latest: Uint8Array | null = null

  async function paint() {
    if (busy || !latest || !canvas) return
    busy = true
    const bytes = latest; latest = null
    try {
      const bmp = await createImageBitmap(new Blob([bytes as BlobPart], { type: 'image/jpeg' }))
      const cw = canvas.clientWidth, ch = canvas.clientHeight, dpr = window.devicePixelRatio || 1
      if (canvas.width !== Math.round(cw * dpr) || canvas.height !== Math.round(ch * dpr)) { canvas.width = Math.round(cw * dpr); canvas.height = Math.round(ch * dpr) }
      const ctx = canvas.getContext('2d')!
      const sc = Math.min(cw / bmp.width, ch / bmp.height), dw = bmp.width * sc, dh = bmp.height * sc
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.fillStyle = '#000'; ctx.fillRect(0, 0, cw, ch)
      ctx.drawImage(bmp, (cw - dw) / 2, (ch - dh) / 2, dw, dh)
      size = `${bmp.width}x${bmp.height}`
      bmp.close()
      frames++
    } catch (e) { error = (e as Error).message }
    busy = false
    if (latest) paint()   // only the newest frame matters
  }
  function onRecord(r: MediaRecord) { if (r.jpeg) { latest = r.payload; paint() } }
  onMount(() => {
    let closer: (() => void) | null = null, gone = false
    const unsub = octave.status.subscribe(async (s) => {
      if (s !== 'online') return
      try { const st = await octave.stream('androidAutoManager', 'frames', { onRecord }); if (gone) st.close(); else closer = st.close; error = '' }
      catch (e) { error = (e as Error).message }
    })
    return () => { gone = true; unsub(); closer?.() }
  })
</script>

<div class="aa">
  {#if error}<p class="muted small">No picture: {error}</p>{/if}
  <canvas bind:this={canvas}></canvas>
  {#if showStats}<span class="small faint">{size} · {frames} frames</span>{/if}
</div>

<style>
  .aa { width: 100%; height: 100%; display: flex; flex-direction: column; gap: 6px; }
  canvas { flex: 1; width: 100%; min-height: 0; background: #000; border-radius: var(--radius-s); display: block; }
</style>
