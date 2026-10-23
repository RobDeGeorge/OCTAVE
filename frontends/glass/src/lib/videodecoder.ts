// A WebCodecs H.264 player for Annex-B access units: configures from the
// SPS, starts at a keyframe, reconfigures when the SPS changes, drops to the
// next keyframe when it falls behind, and paints each frame on a canvas with
// letterboxing. The service's live feed and the dev page's fake source feed
// it the same way: feed({data, keyframe, pts}).
import { parseSps, splitNals, NAL_SPS, type SpsInfo } from './h264'

export interface FedUnit { data: Uint8Array; keyframe: boolean; pts: number }
export interface Stats { codec: string; width: number; height: number; decoded: number; dropped: number; queued: number; errors: number; state: string }

const MAX_QUEUE = 6   // chunks waiting in the decoder before we skip to the next keyframe

export class H264Player {
  private decoder: VideoDecoder | null = null
  private ctx: CanvasRenderingContext2D
  private spsBytes: Uint8Array | null = null
  private info: SpsInfo | null = null
  private waitingKey = true
  private stats: Stats = { codec: '', width: 0, height: 0, decoded: 0, dropped: 0, queued: 0, errors: 0, state: 'idle' }
  /** The frame's rectangle inside the canvas, after letterboxing (css pixels). */
  rect = { x: 0, y: 0, w: 0, h: 0 }
  onstats?: (s: Stats) => void

  constructor(private canvas: HTMLCanvasElement) {
    const c = canvas.getContext('2d', { alpha: false })
    if (!c) throw new Error('no 2d context')
    this.ctx = c
  }

  static supported(): boolean { return typeof VideoDecoder !== 'undefined' }

  getStats(): Stats { return { ...this.stats, queued: this.decoder?.decodeQueueSize ?? 0 } }

  /** Feed one access unit (Annex-B, SPS/PPS inline on keyframes). */
  feed(u: FedUnit): void {
    const sps = findSps(u.data)
    if (sps && (!this.spsBytes || !same(sps, this.spsBytes))) this.configure(sps)
    if (!this.decoder || this.decoder.state !== 'configured') return
    if (this.waitingKey && !u.keyframe) { this.stats.dropped++; return }
    if (this.decoder.decodeQueueSize > MAX_QUEUE && !u.keyframe) {
      // behind: skip until the next keyframe rather than build up delay
      this.waitingKey = true
      this.stats.dropped++
      return
    }
    this.waitingKey = false
    try {
      this.decoder.decode(new EncodedVideoChunk({ type: u.keyframe ? 'key' : 'delta', timestamp: u.pts, data: u.data }))
    } catch (e) {
      this.stats.errors++
      this.waitingKey = true
      console.warn('decode:', (e as Error).message)
    }
    this.emit()
  }

  private configure(sps: Uint8Array): void {
    let info: SpsInfo
    try { info = parseSps(sps) } catch (e) { console.warn('sps:', (e as Error).message); return }
    this.spsBytes = sps.slice()
    this.info = info
    this.stats.codec = info.codec
    this.stats.width = info.width
    this.stats.height = info.height
    this.decoder?.close()
    this.decoder = new VideoDecoder({
      output: (frame) => this.paint(frame),
      error: (e) => { this.stats.errors++; this.stats.state = 'error'; console.warn('decoder:', e.message); this.waitingKey = true; this.emit() },
    })
    // `avc.format` is in the WebCodecs registry but not yet in TypeScript's lib
    this.decoder.configure({ codec: info.codec, optimizeForLatency: true, avc: { format: 'annexb' } } as VideoDecoderConfig & { avc: { format: string } })
    this.waitingKey = true
    this.stats.state = 'configured'
    this.emit()
  }

  private paint(frame: VideoFrame): void {
    const w = frame.displayWidth, h = frame.displayHeight
    const cw = this.canvas.clientWidth || w, ch = this.canvas.clientHeight || h
    const dpr = window.devicePixelRatio || 1
    if (this.canvas.width !== Math.round(cw * dpr) || this.canvas.height !== Math.round(ch * dpr)) {
      this.canvas.width = Math.round(cw * dpr)
      this.canvas.height = Math.round(ch * dpr)
    }
    const scale = Math.min(cw / w, ch / h)
    const dw = w * scale, dh = h * scale, dx = (cw - dw) / 2, dy = (ch - dh) / 2
    this.rect = { x: dx, y: dy, w: dw, h: dh }
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    if (dx > 0 || dy > 0) { this.ctx.fillStyle = '#000'; this.ctx.fillRect(0, 0, cw, ch) }
    this.ctx.drawImage(frame, dx, dy, dw, dh)
    frame.close()
    this.stats.decoded++
    this.stats.state = 'playing'
    this.emit()
  }

  /** Canvas-relative css pixel -> frame-relative 0..1, or null outside the picture. */
  toFrame(x: number, y: number): [number, number] | null {
    const r = this.rect
    if (!r.w || !r.h) return null
    const fx = (x - r.x) / r.w, fy = (y - r.y) / r.h
    if (fx < 0 || fx > 1 || fy < 0 || fy > 1) return null
    return [fx, fy]
  }

  /** Forget the stream: the next keyframe (with its SPS) starts afresh. */
  reset(): void {
    this.decoder?.close()
    this.decoder = null
    this.spsBytes = null
    this.waitingKey = true
    this.stats.state = 'idle'
    this.emit()
  }

  close(): void { this.reset() }

  private emit(): void { this.onstats?.(this.getStats()) }
  get size(): { width: number; height: number } | null { return this.info ? { width: this.info.width, height: this.info.height } : null }
}

function findSps(au: Uint8Array): Uint8Array | null {
  for (const n of splitNals(au)) if (n.type === NAL_SPS) return au.subarray(n.start, n.end)
  return null
}
function same(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false
  return true
}
