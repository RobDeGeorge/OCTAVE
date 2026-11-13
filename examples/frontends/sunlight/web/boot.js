// Relative URLs throughout: on a head unit they are OCTAVE's own port, in the
// browser demo the folder the frame's <base> names. No cookies, no storage.
(async function () {
  const loading = document.getElementById('loading');
  const line = document.getElementById('line');
  const fill = document.getElementById('fill');
  try {
    if (typeof Go !== 'function') throw new Error('wasm_exec.js did not load');
    if (typeof WebAssembly !== 'object') throw new Error('this browser has no WebAssembly');
    const response = await fetch('sunlight.wasm', { credentials: 'omit' });
    if (!response.ok) throw new Error('HTTP ' + response.status + ' for sunlight.wasm');
    // read it by hand for the progress line (several MB over a car's Wi-Fi)
    const total = Number(response.headers.get('Content-Length')) || 0;
    let bytes;
    if (response.body && response.body.getReader) {
      const reader = response.body.getReader();
      const chunks = [];
      let got = 0;
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
        got += value.length;
        line.textContent = 'Loading Sunlight ' + (got / 1048576).toFixed(1) + ' MB';
        if (total > got) fill.style.width = (100 * got / total) + '%';
      }
      bytes = new Uint8Array(got);
      let at = 0;
      for (const c of chunks) { bytes.set(c, at); at += c.length; }
    } else {
      bytes = await response.arrayBuffer();
    }
    fill.style.width = '100%';
    line.textContent = 'Starting Sunlight';
    const go = new Go();
    const { instance } = await WebAssembly.instantiate(bytes, go.importObject);
    loading.remove();
    await go.run(instance);
  } catch (e) {
    if (!loading.isConnected) document.body.appendChild(loading);
    loading.className = 'failed';
    line.textContent = 'Sunlight could not start: ' + (e && e.message ? e.message : e);
  }
})();
