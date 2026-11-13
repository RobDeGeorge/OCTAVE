// Relative URLs throughout: on a head unit they are OCTAVE's own port, in the
// browser demo the folder the frame's <base> names. retro_cluster.js is
// wasm-bindgen's loader (build_web.sh); init() runs the program's main().
import init from './retro_cluster.js';

init().catch((e) => {
  // eframe throws one exception on purpose to leave main(): not a failure
  if (String(e && e.message).includes('Using exceptions for control flow')) return;
  const loading = document.getElementById('loading') ?? document.body.appendChild(document.createElement('div'));
  loading.id = 'loading';
  loading.className = 'failed';
  loading.textContent = 'Retro Cluster could not start: ' + (e && e.message ? e.message : e);
});
