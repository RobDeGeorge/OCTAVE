// H.264 Annex-B helpers for the video player: NAL unit splitting, the
// sequence parameter set (codec string, picture size), access-unit grouping
// for a raw stream. The service sends whole access units, so only the SPS
// reading is needed on the live path; the splitter feeds the dev page.

export const NAL_SLICE = 1, NAL_IDR = 5, NAL_SEI = 6, NAL_SPS = 7, NAL_PPS = 8, NAL_AUD = 9

export interface Nal { type: number; start: number; end: number }

/** Offsets of each NAL unit (after its start code) in an Annex-B buffer. */
export function splitNals(buf: Uint8Array): Nal[] {
  const out: Nal[] = []
  let i = 0, start = -1
  const n = buf.length
  while (i + 2 < n) {
    if (buf[i] === 0 && buf[i + 1] === 0 && buf[i + 2] === 1) {
      if (start >= 0) out.push({ type: buf[start] & 0x1f, start, end: i - (i > 0 && buf[i - 1] === 0 ? 1 : 0) })
      i += 3
      start = i
    } else i++
  }
  if (start >= 0) out.push({ type: buf[start] & 0x1f, start, end: n })
  return out
}

/** The exp-Golomb bit reader an SPS needs (emulation-prevention bytes removed first). */
class Bits {
  private pos = 0
  constructor(private b: Uint8Array) {}
  u(n: number): number { let v = 0; for (let i = 0; i < n; i++) v = (v << 1) | this.bit(); return v >>> 0 }
  bit(): number { const v = (this.b[this.pos >> 3] >> (7 - (this.pos & 7))) & 1; this.pos++; return v }
  ue(): number { let z = 0; while (this.bit() === 0 && z < 32) z++; return z ? ((1 << z) - 1 + this.u(z)) : 0 }
  se(): number { const k = this.ue(); return k & 1 ? (k + 1) / 2 : -(k / 2) }
}

function unescape(b: Uint8Array): Uint8Array {
  const out: number[] = []
  for (let i = 0; i < b.length; i++) {
    if (i + 2 < b.length && b[i] === 0 && b[i + 1] === 0 && b[i + 2] === 3) { out.push(0, 0); i += 2 } else out.push(b[i])
  }
  return Uint8Array.from(out)
}

export interface SpsInfo { codec: string; width: number; height: number; profile: number; level: number }

/** Read an SPS NAL (the bytes starting at its header byte). */
export function parseSps(nal: Uint8Array): SpsInfo {
  const profile = nal[1], constraints = nal[2], level = nal[3]
  const codec = `avc1.${hex(profile)}${hex(constraints)}${hex(level)}`
  const r = new Bits(unescape(nal.subarray(4)))
  r.ue() // seq_parameter_set_id
  let chromaFormat = 1
  if ([100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135].includes(profile)) {
    chromaFormat = r.ue()
    if (chromaFormat === 3) r.u(1)
    r.ue(); r.ue(); r.u(1)
    if (r.u(1)) { // scaling matrix
      for (let i = 0; i < (chromaFormat !== 3 ? 8 : 12); i++) if (r.u(1)) {
        const size = i < 6 ? 16 : 64
        let last = 8, next = 8
        for (let j = 0; j < size; j++) { if (next) next = (last + r.se() + 256) % 256; last = next ? next : last }
      }
    }
  }
  r.ue() // log2_max_frame_num_minus4
  const poc = r.ue()
  if (poc === 0) r.ue()
  else if (poc === 1) { r.u(1); r.se(); r.se(); const n = r.ue(); for (let i = 0; i < n; i++) r.se() }
  r.ue(); r.u(1)
  const wMbs = r.ue() + 1, hMap = r.ue() + 1
  const frameMbsOnly = r.u(1)
  if (!frameMbsOnly) r.u(1)
  r.u(1)
  let cl = 0, cr = 0, ct = 0, cb = 0
  if (r.u(1)) { cl = r.ue(); cr = r.ue(); ct = r.ue(); cb = r.ue() }
  const subW = chromaFormat === 1 || chromaFormat === 2 ? 2 : 1, subH = chromaFormat === 1 ? 2 : 1
  const unitY = (2 - frameMbsOnly) * subH
  return { codec, profile, level, width: wMbs * 16 - (cl + cr) * subW, height: (2 - frameMbsOnly) * hMap * 16 - (ct + cb) * unitY }
}
const hex = (v: number) => v.toString(16).padStart(2, '0')

export interface AccessUnit { data: Uint8Array; keyframe: boolean; sps?: Uint8Array; pts: number }

/** Group a raw stream's NAL units into access units: one per coded picture,
 *  with the SPS / PPS that precede a picture attached to it. Good enough
 *  for the one-slice-per-frame streams the dev page plays. */
export function groupAccessUnits(buf: Uint8Array, fps = 30): AccessUnit[] {
  const nals = splitNals(buf)
  const out: AccessUnit[] = []
  let pending: Nal[] = []
  let sps: Uint8Array | undefined
  let frame = 0
  const flush = () => {
    if (!pending.some((n) => n.type === NAL_SLICE || n.type === NAL_IDR)) return
    const first = pending[0].start - 4 >= 0 ? pending[0].start - 4 : 0
    const last = pending[pending.length - 1].end
    out.push({ data: buf.subarray(startCode(buf, pending[0].start), last), keyframe: pending.some((n) => n.type === NAL_IDR), sps, pts: Math.round((frame++ * 1e6) / fps) })
    void first
    pending = []
  }
  for (const n of nals) {
    if (n.type === NAL_SPS) { flush(); sps = buf.subarray(n.start, n.end) }
    else if (n.type === NAL_PPS || n.type === NAL_AUD || n.type === NAL_SEI) { if (pending.some((p) => p.type === NAL_SLICE || p.type === NAL_IDR)) flush() }
    else if (n.type === NAL_SLICE || n.type === NAL_IDR) { if (pending.some((p) => p.type === NAL_SLICE || p.type === NAL_IDR)) flush() }
    pending.push(n)
  }
  flush()
  return out
}
function startCode(buf: Uint8Array, nalStart: number): number {
  // the start code before a NAL is 3 or 4 bytes
  return nalStart >= 4 && buf[nalStart - 4] === 0 ? nalStart - 4 : nalStart - 3
}
