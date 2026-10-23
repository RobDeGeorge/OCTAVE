// The whole protocol in 60 lines (docs/api/protocol.json): one WebSocket,
// JSON text frames, replies matched to requests by id, events to a handler.
// Works in a browser and in Node 22+ (both have WebSocket). Copy it as is.
export class Octave {
  constructor(url) {
    this.url = url
    this.nextId = 1
    this.pending = new Map()
    this.handlers = []
    this.onclose = () => {}
  }

  /** Open the socket and say hello; resolves with the hello reply. */
  connect(token) {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(this.url)
      this.ws.onerror = () => reject(new Error(`cannot reach ${this.url}`))
      this.ws.onclose = () => { for (const p of this.pending.values()) p.reject(new Error('connection closed')); this.pending.clear(); this.onclose() }
      this.ws.onmessage = (m) => {
        if (typeof m.data !== 'string') return   // a media record: not used here
        const msg = JSON.parse(m.data)
        if ('event' in msg) { for (const h of this.handlers) h(msg); return }
        const p = this.pending.get(msg.id)
        if (!p) return
        this.pending.delete(msg.id)
        if (msg.ok) p.resolve(msg.result)
        else { const e = new Error(msg.error); e.code = msg.code; p.reject(e) }
      }
      this.ws.onopen = () => this.request('hello', token ? { token } : {}).then(resolve, reject)
    })
  }

  /** Any op: the reply's result, or a rejection with the server's error. */
  request(op, args = {}) {
    const id = this.nextId++
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject })
      this.ws.send(JSON.stringify({ op, id, ...args }))
    })
  }

  /** Every event (property, params, signal, model, ...) goes to `fn`. */
  onEvent(fn) { this.handlers.push(fn) }

  get(object, property) { return this.request('get', { object, property }) }
  call(object, method, ...args) { return this.request('call', { object, method, args }) }
  subscribe(object, options) { return this.request('subscribe', { object, ...options }) }

  /** Pair with the code the head unit shows, asking only for `scopes` (docs/api/README.md,
   *  Scopes). Returns the token to keep; what was granted is in `this.scopes`. */
  async pair(code, name, scopes) {
    const r = await this.request('pair', { code, name, scopes })
    this.scopes = r.scopes
    return r.token
  }

  /** A fresh media ticket for /art URLs (never put the token in a URL); cached until it nears expiry. */
  async ticket() {
    if (!this._ticket || this._ticket.until < Date.now()) {
      const t = await this.request('mediaTicket')
      this._ticket = { value: t.ticket, until: Date.now() + (t.expiresIn - 30) * 1000 }
    }
    return this._ticket.value
  }

  close() { this.ws?.close() }
}
