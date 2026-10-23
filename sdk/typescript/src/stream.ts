// Media streams (phone mirror video, Android Auto frames) ride the same
// WebSocket as binary frames. Record: u8 version (1) | u8 stream | u8 flags
// | u8 reserved | u64 pts (microseconds, big-endian) | payload. Flags:
// 1 keyframe, 2 config, 4 jpeg, 8 discontinuity. A video payload is one
// Annex-B access unit with SPS / PPS inline on keyframes; a JPEG record is
// a whole JPEG.
export const F_KEY = 1, F_CONFIG = 2, F_JPEG = 4, F_DISCONTINUITY = 8
export const RECORD_HEADER = 12

export interface MediaRecord {
  stream: number
  flags: number
  /** Presentation time in microseconds. */
  pts: number
  payload: Uint8Array
  keyframe: boolean
  jpeg: boolean
  config: boolean
  discontinuity: boolean
}

export function parseRecord(buf: ArrayBuffer): MediaRecord | null {
  if (buf.byteLength < RECORD_HEADER) return null
  const v = new DataView(buf)
  if (v.getUint8(0) !== 1) return null
  const flags = v.getUint8(2)
  return {
    stream: v.getUint8(1), flags, pts: Number(v.getBigUint64(4, false)), payload: new Uint8Array(buf, RECORD_HEADER),
    keyframe: (flags & F_KEY) !== 0, jpeg: (flags & F_JPEG) !== 0, config: (flags & F_CONFIG) !== 0, discontinuity: (flags & F_DISCONTINUITY) !== 0,
  }
}

/** Build a record the way the service does (tests, fake sources). */
export function buildRecord(stream: number, flags: number, pts: number, payload: Uint8Array): ArrayBuffer {
  const out = new ArrayBuffer(RECORD_HEADER + payload.byteLength)
  const v = new DataView(out)
  v.setUint8(0, 1); v.setUint8(1, stream); v.setUint8(2, flags); v.setUint8(3, 0)
  v.setBigUint64(4, BigInt(Math.round(pts)), false)
  new Uint8Array(out, RECORD_HEADER).set(payload)
  return out
}

/** What the `stream` op replies, and what a `streamConfig` event carries. */
export interface StreamInfo { stream: number; codec?: string; width?: number; height?: number; format?: string }
export interface StreamHandlers {
  onRecord: (r: MediaRecord) => void
  onConfig?: (info: StreamInfo) => void
  /** The service dropped `count` records for this stream; the next is a keyframe. */
  onDropped?: (count: number) => void
}
