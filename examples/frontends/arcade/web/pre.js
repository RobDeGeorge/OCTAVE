// The page's half of the browser build, placed in front of Emscripten's
// loader (emcc --pre-js): which canvas, how big on the screen, and what to
// say when the program cannot start. No storage, no cookies, nothing of the
// page's origin: the same file runs served by OCTAVE and inside the OCTAVE
// browser demo's sandbox.
(function () {
  var W = 320, H = 200;
  var canvas = document.getElementById('canvas');
  var status = document.getElementById('status');
  function say(text) { if (status) status.textContent = text; }

  // The picture scaled up whole and centred, black around it. A whole
  // number of screen pixels per picture pixel when that costs under a tenth
  // of the size; on a phone held upright it would, so there the picture
  // fills the width instead (nearest neighbour either way: CSS, pixelated).
  function fit() {
    var w = document.documentElement.clientWidth || window.innerWidth;
    var h = document.documentElement.clientHeight || window.innerHeight;
    if (!w || !h) return;
    var dpr = window.devicePixelRatio || 1;
    var exact = Math.min(w / W, h / H);
    var k = Math.floor(exact * dpr) / dpr;
    if (k <= 0 || k < exact * 0.9) k = exact;
    canvas.style.width = W * k + 'px';
    canvas.style.height = H * k + 'px';
  }
  fit();
  window.addEventListener('resize', fit);
  window.addEventListener('orientationchange', fit);
  if (window.visualViewport) window.visualViewport.addEventListener('resize', fit);

  // Keys go to the window; inside a frame that takes focus, which a tap gives
  canvas.addEventListener('pointerdown', function () { try { window.focus(); } catch (e) { /* nothing to do */ } });
  canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); });

  Module.canvas = canvas;
  Module.print = function (text) { console.log(text); };
  Module.printErr = function (text) { console.warn(text); };
  Module.onRuntimeInitialized = function () { say(''); };
  Module.onAbort = function () { say('OCTAVE ARCADE STOPPED: RELOAD THE PAGE'); };
  if (typeof WebAssembly !== 'object') say('THIS BROWSER HAS NO WEBASSEMBLY');
  window.addEventListener('error', function () { if (status && status.textContent === 'LOADING') say('OCTAVE ARCADE COULD NOT START'); });
})();
