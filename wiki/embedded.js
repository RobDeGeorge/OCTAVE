/* Touch reader enhancements. Injected by WikiWebView, never by the website. */
(function () {
  'use strict';
  if (!document.querySelector('.wiki-layout') || window.octaveWiki) return;
  const root = document.documentElement;
  root.classList.add('octave-reader');
  const style = document.createElement('link');
  style.rel = 'stylesheet';
  style.href = new URL('embedded.css', window.location.href).href;
  document.head.appendChild(style);

  const sidebar = document.getElementById('sidebar');
  const input = document.getElementById('search-input');
  const backdrop = document.createElement('button');
  backdrop.className = 'octave-topics-backdrop';
  backdrop.setAttribute('aria-label', 'Close topics');
  document.body.appendChild(backdrop);
  const close = document.createElement('button');
  close.className = 'octave-topics-close';
  close.textContent = 'Back to article';
  sidebar.prepend(close);
  sidebar.setAttribute('aria-label', 'Wiki topics');
  function topics(open) {
    sidebar.classList.toggle('open', open);
    sidebar.inert = !open;
    root.classList.toggle('octave-topics-open', open);
    if (!open) {
      document.getElementById('search-overlay').classList.remove('visible');
      if (sidebar.contains(document.activeElement)) document.activeElement.blur();
    }
  }
  topics(false);
  backdrop.addEventListener('click', () => topics(false));
  close.addEventListener('click', () => topics(false));
  sidebar.addEventListener('click', e => { if (e.target.closest('.sidebar-link')) topics(false); });
  document.addEventListener('keydown', e => {
    if (e.key === '/' && !/input|textarea/i.test(document.activeElement.tagName)) topics(true);
    if (e.key === 'Escape') topics(false);
  });
  document.querySelectorAll('table').forEach(table => {
    const wrap = document.createElement('div');
    wrap.className = 'octave-table-scroll';
    wrap.tabIndex = 0;
    wrap.setAttribute('role', 'region');
    wrap.setAttribute('aria-label', 'Scrollable table');
    table.before(wrap); wrap.appendChild(table);
  });

  window.octaveWiki = {
    toggleTopics: () => topics(!sidebar.classList.contains('open')),
    search: () => { topics(true); input.focus(); },
    configure: config => {
      for (const [key, value] of Object.entries(config)) root.style.setProperty('--reader-' + key, value);
    }
  };
  window.octaveWiki.configure(window.__octaveReaderConfig || {});

  // Native touch panning belongs to Chromium. Add drag scrolling only for
  // mouse-emulating touchscreens; leave code selection and form editing alone.
  let drag = null;
  let suppressClick = false;
  function scrollTarget(target, horizontal) {
    for (let el = target; el && el !== document.body; el = el.parentElement) {
      const css = getComputedStyle(el);
      if (horizontal ? /auto|scroll/.test(css.overflowX) && el.scrollWidth > el.clientWidth
                     : /auto|scroll/.test(css.overflowY) && el.scrollHeight > el.clientHeight) return el;
    }
    return document.scrollingElement;
  }
  document.addEventListener('pointerdown', e => {
    suppressClick = false;
    if (e.pointerType !== 'mouse' || e.button !== 0 || e.ctrlKey || e.shiftKey || e.altKey || e.metaKey
        || e.target.closest('input,textarea,button,pre,code,[contenteditable="true"]')) return;
    drag = { id: e.pointerId, x: e.clientX, y: e.clientY, target: e.target, scrolling: false };
  });
  document.addEventListener('pointermove', e => {
    if (!drag || drag.id !== e.pointerId) return;
    const dx = drag.x - e.clientX, dy = drag.y - e.clientY;
    if (!drag.scrolling) {
      if (Math.hypot(dx, dy) < 8) return;
      drag.horizontal = Math.abs(dx) > Math.abs(dy);
      drag.scroller = scrollTarget(drag.target, drag.horizontal);
      drag.scrolling = true;
      root.classList.add('octave-dragging');
    }
    e.preventDefault();
    if (drag.horizontal) drag.scroller.scrollLeft += dx;
    else drag.scroller.scrollTop += dy;
    drag.x = e.clientX; drag.y = e.clientY;
    suppressClick = true;
  }, { passive: false });
  function endDrag() { drag = null; root.classList.remove('octave-dragging'); }
  document.addEventListener('pointerup', endDrag);
  document.addEventListener('pointercancel', endDrag);
  window.addEventListener('blur', endDrag);
  document.addEventListener('click', e => {
    if (suppressClick) { e.preventDefault(); e.stopImmediatePropagation(); suppressClick = false; }
  }, true);
})();
