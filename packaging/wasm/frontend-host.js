// Running a web frontend in a page against the OCTAVE core in that page
// (Module.octaveService, src/platform/wasm/webservice.h). Shared by
// index.html (a frontend chosen in Settings > Device > Frontends, shown
// over the built-in screen) and playground.html.
//
// The frontend runs in a sandboxed iframe without allow-same-origin: it
// gets an opaque origin, so a frontend from anywhere can't touch this site
// or its storage. Its page gets a WebSocket stand-in first (shimSource),
// which carries each text frame to this page by postMessage; bridge()
// carries them on to service connections and back. An unmodified frontend
// (new WebSocket("ws://" + location.host + "/")) works as on a head unit.
(function () {
  'use strict';

  // Runs in the iframe before any of the frontend's own scripts
  function shimSource() {
    return `(function () {
  var parentWindow = window.parent;
  var sockets = new Map();
  var next = 0;
  function post(m) { m.octave = 1; parentWindow.postMessage(m, '*'); }
  window.addEventListener('message', function (e) {
    if (e.source !== parentWindow) return;
    var m = e.data;
    if (!m || m.octave !== 1) return;
    var s = sockets.get(m.sock);
    if (!s) return;
    if (m.type === 'opened') s._opened(m.ok);
    else if (m.type === 'message') s._message(m.data);
    else if (m.type === 'closed') s._closed();
  });
  class OctaveSocket extends EventTarget {
    constructor(url) {
      super();
      this.url = String(url); this.readyState = 0; this.protocol = ''; this.extensions = '';
      this.binaryType = 'blob'; this.bufferedAmount = 0;
      this.onopen = this.onmessage = this.onclose = this.onerror = null;
      this._sock = ++next;
      sockets.set(this._sock, this);
      post({ type: 'open', sock: this._sock });
    }
    _fire(type, ev) {
      const h = this['on' + type];
      if (typeof h === 'function') { try { h.call(this, ev); } catch (e) { setTimeout(() => { throw e; }); } }
      this.dispatchEvent(ev);
    }
    _opened(ok) {
      if (this.readyState !== 0) return;
      if (!ok) {
        this.readyState = 3;
        sockets.delete(this._sock);
        this._fire('error', new Event('error'));
        this._fire('close', new CloseEvent('close', { code: 1006, wasClean: false }));
        return;
      }
      this.readyState = 1;
      this._fire('open', new Event('open'));
    }
    _message(text) {
      if (this.readyState === 1) this._fire('message', new MessageEvent('message', { data: text }));
    }
    _closed() {
      if (this.readyState === 3) return;
      this.readyState = 3;
      sockets.delete(this._sock);
      this._fire('close', new CloseEvent('close', { code: 1006, wasClean: false }));
    }
    send(data) {
      if (this.readyState !== 1) throw new DOMException('WebSocket is not open', 'InvalidStateError');
      post({ type: 'send', sock: this._sock, data: typeof data === 'string' ? data : new TextDecoder().decode(data) });
    }
    close() {
      if (this.readyState >= 2) return;
      const wasOpen = this.readyState === 1;
      this.readyState = 3;
      sockets.delete(this._sock);
      post({ type: 'close', sock: this._sock });
      if (wasOpen) this._fire('close', new CloseEvent('close', { code: 1000, wasClean: true }));
    }
  }
  for (const [k, v] of [['CONNECTING', 0], ['OPEN', 1], ['CLOSING', 2], ['CLOSED', 3]]) {
    Object.defineProperty(OctaveSocket, k, { value: v });
    Object.defineProperty(OctaveSocket.prototype, k, { value: v });
  }
  window.WebSocket = OctaveSocket;
})();`;
  }

  // The frontend's sockets (by its own numbers) to service connections.
  // `trace(dir, text)` (optional) sees each message: '>' from the frontend,
  // '<' to it. closeAll() ends every connection of the frontend loaded so far
  function bridge(frame, service, trace) {
    let socks = new Map();
    window.addEventListener('message', (e) => {
      if (e.source !== frame.contentWindow) return;
      const m = e.data;
      if (!m || m.octave !== 1 || typeof m.sock !== 'number') return;
      const target = e.source;
      const mine = socks;
      const reply = (msg) => { msg.octave = 1; msg.sock = m.sock; target.postMessage(msg, '*'); };
      if (m.type === 'open') {
        if (mine.has(m.sock)) return;
        const id = service.open((text) => { if (trace) trace('<', text); reply({ type: 'message', data: text }); });
        if (id) mine.set(m.sock, id);
        reply({ type: 'opened', ok: !!id });
      } else if (m.type === 'send' && typeof m.data === 'string') {
        const id = mine.get(m.sock);
        if (!id) return reply({ type: 'closed' });
        if (trace) trace('>', m.data);
        service.send(id, m.data);
      } else if (m.type === 'close') {
        const id = mine.get(m.sock);
        if (id) { service.close(id); mine.delete(m.sock); }
      }
    });
    return {
      closeAll() {
        for (const id of socks.values()) service.close(id);
        socks = new Map();
      },
    };
  }

  // Loads the frontend at `url` (a folder or an .html page; another site
  // must allow it with CORS, as GitHub Pages does) into `frame`, with the
  // stand-in first. Resolves with the page's URL; throws on failure
  async function load(frame, url) {
    const base = new URL(url, location.href);
    const page = base.pathname.endsWith('.html') ? base : new URL('index.html', base);
    const response = await fetch(page, { mode: 'cors' });
    if (!response.ok) throw new Error(`HTTP ${response.status} for ${page}`);
    let html = await response.text();
    const head = `<base href="${new URL('.', page).href}"><script>${shimSource()}<\/script>`;
    html = /<head[^>]*>/i.test(html) ? html.replace(/<head[^>]*>/i, (m) => m + head) : head + html;
    frame.srcdoc = html;
    return page.href;
  }

  // The site's frontends (frontends/catalog.json, stage_frontends.py); [] without one
  async function catalog() {
    try {
      const r = await fetch('frontends/catalog.json');
      const list = r.ok ? await r.json() : [];
      return Array.isArray(list) ? list : [];
    } catch (e) {
      return [];
    }
  }

  // Module.octaveService once the core has installed it
  function service() {
    return new Promise((resolve) => {
      if (globalThis.octaveService) return resolve(globalThis.octaveService);
      const before = globalThis.onOctaveServiceReady;
      globalThis.onOctaveServiceReady = (s) => {
        if (typeof before === 'function') before(s);
        resolve(s);
      };
    });
  }

  // One service connection for the page itself: request(op, args) -> result,
  // and on(handler) for its events
  function connect(svc) {
    let nextId = 1;
    const pending = new Map();
    const listeners = [];
    const id = svc.open((text) => {
      const m = JSON.parse(text);
      if (m.event) { for (const l of listeners) l(m); return; }
      const p = pending.get(m.id);
      if (!p) return;
      pending.delete(m.id);
      m.ok ? p.resolve(m.result) : p.reject(new Error(m.error));
    });
    return {
      request(op, args) {
        return new Promise((resolve, reject) => {
          const rid = nextId++;
          pending.set(rid, { resolve, reject });
          svc.send(id, JSON.stringify({ op, id: rid, ...args }));
        });
      },
      call(object, method, ...args) { return this.request('call', { object, method, args }); },
      on(handler) { listeners.push(handler); },
    };
  }

  window.OctaveFrontendHost = { shimSource, bridge, load, catalog, service, connect };
})();
