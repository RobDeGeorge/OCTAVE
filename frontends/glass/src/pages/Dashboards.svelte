<script lang="ts">
  // Dashboards: the chooser, a full-screen view, and the editor.
  import { octave } from '../lib/octave'
  import { act, toast, catalog, catalogOrder, obd } from '../lib/stores'
  import { go } from '../lib/router'
  import { widgets, metaFor, acceptsKind, colorSwatches, paramUnitProps, type Spec, type Cell, type WidgetMeta } from '../lib/widgets'
  import DashboardRenderer from '../gauges/DashboardRenderer.svelte'
  import Modal from '../ui/Modal.svelte'
  import Icon from '../ui/Icon.svelte'
  import Toggle from '../ui/Toggle.svelte'
  import Slider from '../ui/Slider.svelte'

  let { arg = '' }: { arg?: string } = $props()
  interface Entry { id: string; label: string; builtIn: boolean; path: string }
  const list = octave.prop<Entry[]>('dashboardManager', 'dashboards', [])
  const hiddenBuiltIns = octave.prop<number>('dashboardManager', 'hiddenBuiltInCount', 0)
  const shareError = octave.prop<string>('dashboardManager', 'lastShareError', '')

  // mode: chooser | view/<id> | edit/<id|new>
  const mode = $derived(arg.startsWith('view/') ? 'view' : arg.startsWith('edit/') ? 'edit' : 'chooser')
  const id = $derived(arg.split('/')[1] ?? '')
  let spec = $state<Spec | null>(null)
  let error = $state('')
  $effect(() => {
    if (mode === 'chooser') { spec = null; return }
    error = ''
    if (id === 'new') { spec = { schema: 1, label: 'New dashboard', gridColumns: 12, gridRows: 6, cells: [] }; return }
    spec = null
    octave.call<Spec>('dashboardManager', 'loadDashboard', id).then((s) => {
      if (!s || !s.cells) error = 'This dashboard could not be loaded.'
      else spec = s
    }).catch((e) => (error = e.message))
  })

  // ── chooser actions ────────────────────────────────────────────────────
  let askImport = $state(false), importText = $state('')
  let askDelete = $state<Entry | null>(null)
  let askCopy = $state<Entry | null>(null), copyName = $state('')
  async function share(e: Entry) {
    const p = await act(octave.call<string>('dashboardManager', 'exportDashboard', e.id))
    if (p) toast(`Exported to ${p}`)
    else if ($shareError) toast($shareError, 'bad')
  }
  async function importNow() {
    const r = await act(octave.call<string>('dashboardManager', 'importDashboardFromText', importText))
    if (r) { toast('Imported'); askImport = false; importText = '' } else toast($shareError || 'Nothing imported', 'bad')
  }
  async function copy() {
    if (!askCopy) return
    const r = await act(octave.call<string>('dashboardManager', 'duplicateDashboard', askCopy.id, copyName || `${askCopy.label} copy`))
    if (r) { toast('Copied'); askCopy = null }
  }

  // ── editor ─────────────────────────────────────────────────────────────
  let sel = $state(-1)
  let placing = $state<WidgetMeta | null>(null)
  let pickPid = $state(false), pidQ = $state('')
  let dirty = $state(false)
  let canvas = $state<HTMLElement | null>(null)
  const cell = $derived(spec && sel >= 0 ? spec.cells[sel] : null)
  const meta = $derived(cell ? metaFor(cell.type) : null)
  const cols = $derived(spec?.gridColumns ?? 12), rows = $derived(spec?.gridRows ?? 6)
  function cellAt(e: PointerEvent | MouseEvent): [number, number] {
    const r = canvas!.getBoundingClientRect()
    return [Math.floor(((e.clientX - r.left) / r.width) * cols), Math.floor(((e.clientY - r.top) / r.height) * rows)]
  }
  function clamp(c: Cell): Cell {
    const cs = Math.max(1, Math.min(c.colSpan ?? 1, cols)), rs = Math.max(1, Math.min(c.rowSpan ?? 1, rows))
    return { ...c, colSpan: cs, rowSpan: rs, col: Math.max(0, Math.min(c.col, cols - cs)), row: Math.max(0, Math.min(c.row, rows - rs)) }
  }
  function overlaps(a: Cell, b: Cell) {
    return a.col < b.col + (b.colSpan ?? 1) && b.col < a.col + (a.colSpan ?? 1) && a.row < b.row + (b.rowSpan ?? 1) && b.row < a.row + (a.rowSpan ?? 1)
  }
  function free(c: Cell, except: number) { return !spec!.cells.some((o, i) => i !== except && overlaps(c, o)) }
  function update(i: number, c: Cell) {
    const n = clamp(c)
    if (!free(n, i)) { toast('That spot is taken', 'bad'); return false }
    spec!.cells[i] = n; dirty = true; return true
  }
  async function placeAt(col: number, row: number) {
    if (!placing || !spec) return
    const m = placing
    const want: Cell = { type: m.type, paramId: '', col, row, colSpan: m.defaultColSpan, rowSpan: m.defaultRowSpan, props: {} }
    let c = clamp(want)
    if (!free(c, -1)) {
      // let the manager find the nearest fit
      const fit = await octave.call<Cell | null>('dashboardManager', 'fitAt', spec.cells, cols, rows, col, row, c.colSpan, c.rowSpan).catch(() => null)
      if (!fit || fit.col === undefined) { toast('No room there', 'bad'); return }
      c = { ...c, ...fit }
    }
    if (m.supportedKinds.length) {
      const first = $catalogOrder.find((pid) => acceptsKind(m, $catalog.get(pid)?.kind ?? '') && ($obd.vehicleSupportedParameters.length === 0 || $obd.vehicleSupportedParameters.includes(pid)))
      c.paramId = first ?? 'RPM'
    }
    spec.cells.push(c); sel = spec.cells.length - 1; placing = null; dirty = true
  }
  // drag to move, grip to resize
  let drag = $state<{ i: number; mode: 'move' | 'size'; dc: number; dr: number } | null>(null)
  function down(e: PointerEvent, i: number, m: 'move' | 'size') {
    if (!spec) return
    const [c, r] = cellAt(e); const cl = spec.cells[i]
    drag = { i, mode: m, dc: c - cl.col, dr: r - cl.row }
    sel = i; (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId)
  }
  function move(e: PointerEvent) {
    if (!drag || !spec) return
    const [c, r] = cellAt(e); const cl = spec.cells[drag.i]
    if (drag.mode === 'move') {
      const n = clamp({ ...cl, col: c - drag.dc, row: r - drag.dr })
      if ((n.col !== cl.col || n.row !== cl.row) && free(n, drag.i)) { spec.cells[drag.i] = n; dirty = true }
    } else {
      // grow up to the grid's edge, never past it (clamp would shift the cell instead)
      const n = clamp({ ...cl, colSpan: Math.max(1, Math.min(c - cl.col + 1, cols - cl.col)), rowSpan: Math.max(1, Math.min(r - cl.row + 1, rows - cl.row)) })
      if ((n.colSpan !== cl.colSpan || n.rowSpan !== cl.rowSpan) && free(n, drag.i)) { spec.cells[drag.i] = n; dirty = true }
    }
  }
  function up() { drag = null }
  function setProp(key: string, v: string | number | boolean | null) {
    if (!cell) return
    const props = { ...(cell.props ?? {}) }
    if (v === null || v === '' || v === undefined) delete props[key]; else props[key] = v
    spec!.cells[sel] = { ...cell, props }; dirty = true
  }
  // paramUnit props are shown in display units and stored native
  const pinfo = $derived(cell?.paramId ? $catalog.get(cell.paramId) : undefined)
  const toNative = (v: number) => (pinfo ? (v - pinfo.displayOffset) / (pinfo.displayScale || 1) : v)
  const toDisp = (v: number) => (pinfo ? v * pinfo.displayScale + pinfo.displayOffset : v)
  const unitProps = $derived(cell ? paramUnitProps(cell.type) : [])
  function propVal(key: string): string | number | boolean | undefined {
    const v = cell?.props?.[key]
    return typeof v === 'number' && unitProps.includes(key) ? Number(toDisp(v).toFixed(3)) : v
  }
  async function save() {
    if (!spec) return
    if (!spec.id) spec.id = await octave.call<string>('dashboardManager', 'uniqueIdForLabel', spec.label || 'dashboard').catch(() => '')
    const r = await act(octave.call<string>('dashboardManager', 'saveDashboard', $state.snapshot(spec)))
    if (r) { toast('Saved'); dirty = false; spec.id = r; if (id !== r) go('dashboards', `edit/${r}`) }
    else toast($shareError || 'Could not save', 'bad')
  }
  const pidCandidates = $derived(meta ? $catalogOrder.filter((pid) => acceptsKind(meta, $catalog.get(pid)?.kind ?? '')).filter((pid) => !pidQ || ($catalog.get(pid)?.title ?? pid).toLowerCase().includes(pidQ.toLowerCase())) : [])
</script>

{#if mode === 'chooser'}
  <div class="page">
    <div class="page-head">
      <h1>Dashboards</h1>
      <div class="wrap">
        <button class="btn primary" onclick={() => go('dashboards', 'edit/new')}><Icon name="plus" />New</button>
        <button class="btn" onclick={() => (askImport = true)}><Icon name="download" />Import</button>
        {#if $hiddenBuiltIns}<button class="btn" onclick={() => act(octave.call('dashboardManager', 'restoreBuiltInDashboards'), 'Built-ins restored')}>Restore {$hiddenBuiltIns} built-in</button>{/if}
      </div>
    </div>
    <div class="cards">
      {#each $list as e (e.id)}
        <div class="glass panel card">
          <button class="open" onclick={() => go('dashboards', `view/${e.id}`)}>
            <span class="name">{e.label}</span>
            <span class="small muted">{e.builtIn ? 'Built in' : 'Yours'} · {e.id}</span>
          </button>
          <div class="acts">
            {#if !e.builtIn}<button class="btn icon" aria-label="Edit {e.label}" onclick={() => go('dashboards', `edit/${e.id}`)}><Icon name="edit" /></button>{/if}
            <button class="btn icon" aria-label="Copy {e.label}" onclick={() => { askCopy = e; copyName = `${e.label} copy` }}><Icon name="copy" /></button>
            <button class="btn icon" aria-label="Share {e.label}" onclick={() => share(e)}><Icon name="share" /></button>
            {#if !e.builtIn}<button class="btn icon danger" aria-label="Delete {e.label}" onclick={() => (askDelete = e)}><Icon name="trash" /></button>{/if}
          </div>
        </div>
      {/each}
    </div>
  </div>

{:else if mode === 'view'}
  <div class="view">
    <div class="viewbar">
      <button class="btn icon" aria-label="Back" onclick={() => go('dashboards')}><Icon name="back" /></button>
      <span class="name">{spec?.label ?? ''}</span>
      {#if !$list.find((e) => e.id === id)?.builtIn}<button class="btn icon" aria-label="Edit" onclick={() => go('dashboards', `edit/${id}`)}><Icon name="edit" /></button>{/if}
    </div>
    <div class="stage" style="--cols:{cols};--rows:{rows}">
      {#if error}<div class="glass panel bad">{error}</div>
      {:else if spec}<DashboardRenderer {spec} consumer="glass-dashboard" />
      {:else}<div class="muted">Loading…</div>{/if}
    </div>
  </div>

{:else}
  <div class="view editor">
    <div class="viewbar">
      <button class="btn icon" aria-label="Back" onclick={() => go('dashboards')}><Icon name="back" /></button>
      {#if spec}<input class="field name-field" bind:value={spec.label} aria-label="Dashboard name" oninput={() => (dirty = true)} />{/if}
      <button class="btn primary" disabled={!spec || !dirty} onclick={save}><Icon name="check" />Save</button>
    </div>
    {#if error}<div class="glass panel bad">{error}</div>
    {:else if spec}
      <div class="edit-layout">
        <div class="glass palette">
          <div class="small muted hint">Tap a widget, then tap the grid</div>
          {#each $widgets as w}
            <button class="pal" class:on={placing?.type === w.type} onclick={() => (placing = placing?.type === w.type ? null : w)}><span class="glyph">{w.glyph}</span>{w.displayName}</button>
          {/each}
        </div>

        <div class="stage grid-stage" style="--cols:{cols};--rows:{rows}" bind:this={canvas} role="grid" tabindex="-1" class:placing
             onclick={(e) => { if (placing) { const [c, r] = cellAt(e); placeAt(c, r) } }}
             onkeydown={(e) => { if (e.key === 'Escape') { placing = null; sel = -1 } }}
             onpointermove={move} onpointerup={up} onpointercancel={up}>
          <div class="gridlines" style="--cols:{cols};--rows:{rows}"></div>
          <DashboardRenderer {spec} consumer="glass-editor" editing selected={sel} onselect={(i) => { if (!placing) sel = i }} />
          {#each spec.cells as c, i (i)}
            {#if i === sel}
              <div class="handle" style="left:calc({(c.col / cols) * 100}%);top:calc({(c.row / rows) * 100}%);width:calc({((c.colSpan ?? 1) / cols) * 100}%);height:calc({((c.rowSpan ?? 1) / rows) * 100}%)">
                <button class="mover" aria-label="Move" onpointerdown={(e) => down(e, i, 'move')}><Icon name="drag" size={20} /></button>
                <button class="grip" aria-label="Resize" onpointerdown={(e) => down(e, i, 'size')}></button>
              </div>
            {/if}
          {/each}
        </div>

        <div class="glass props">
          {#if cell && meta}
            <div class="ph"><strong>{meta.displayName}</strong>
              <button class="btn icon danger" aria-label="Remove widget" onclick={() => { spec!.cells.splice(sel, 1); sel = -1; dirty = true }}><Icon name="trash" /></button></div>
            {#if meta.supportedKinds.length}
              <div class="row"><span class="small">Reading</span><button class="btn" onclick={() => (pickPid = true)}>{$catalog.get(cell.paramId ?? '')?.title ?? cell.paramId ?? 'Pick'}</button></div>
            {/if}
            {#if meta.labelProps}
              <div class="row"><span class="small">Label</span><input class="field" value={propVal('label') ?? ''} placeholder={pinfo?.title ?? ''} onchange={(e) => setProp('label', (e.target as HTMLInputElement).value)} /></div>
              <div class="row"><span class="small">Unit</span><input class="field tiny" value={propVal('unit') ?? ''} placeholder={pinfo?.unit ?? ''} onchange={(e) => setProp('unit', (e.target as HTMLInputElement).value)} /></div>
            {/if}
            {#if meta.rangeProps}
              <div class="row"><span class="small">Min</span><input class="field tiny num" type="number" value={propVal('min') ?? ''} placeholder={String(pinfo?.min ?? '')} onchange={(e) => { const v = (e.target as HTMLInputElement).value; setProp('min', v === '' ? null : toNative(Number(v))) }} /></div>
              <div class="row"><span class="small">Max</span><input class="field tiny num" type="number" value={propVal('max') ?? ''} placeholder={String(pinfo?.max ?? '')} onchange={(e) => { const v = (e.target as HTMLInputElement).value; setProp('max', v === '' ? null : toNative(Number(v))) }} /></div>
            {/if}
            {#each meta.editableProps as ep}
              <div class="row" class:stack={ep.kind === 'int' || ep.kind === 'real'}>
                <span class="small">{ep.label}{ep.paramUnit && pinfo?.unit ? ` (${pinfo.unit})` : ''}</span>
                {#if ep.kind === 'bool'}
                  <Toggle on={Boolean(propVal(ep.key) ?? ep.def)} label={ep.label} onchange={(v) => setProp(ep.key, v)} />
                {:else if ep.kind === 'enum'}
                  <div class="seg">{#each ep.options ?? [] as o}<button class:on={(propVal(ep.key) ?? ep.def) === o} onclick={() => setProp(ep.key, o)}>{o}</button>{/each}</div>
                {:else if ep.kind === 'string'}
                  <input class="field" value={propVal(ep.key) ?? ''} onchange={(e) => setProp(ep.key, (e.target as HTMLInputElement).value)} />
                {:else if ep.min !== undefined && ep.max !== undefined && !ep.paramUnit}
                  <Slider value={Number(propVal(ep.key) ?? ep.def ?? ep.min)} min={ep.min} max={ep.max} step={ep.kind === 'int' ? 1 : (ep.max - ep.min) / 100} label={ep.label}
                          format={(v) => ep.kind === 'int' ? String(Math.round(v)) : v.toFixed(2)} onchange={(v) => setProp(ep.key, ep.kind === 'int' ? Math.round(v) : v)} />
                {:else}
                  <input class="field tiny num" type="number" value={propVal(ep.key) ?? ''} placeholder="unset" onchange={(e) => { const v = (e.target as HTMLInputElement).value; setProp(ep.key, v === '' ? null : ep.paramUnit ? toNative(Number(v)) : Number(v)) }} />
                {/if}
              </div>
            {/each}
            {#each meta.colorProps as cp}
              <div class="row stack"><span class="small">{cp.replace(/Color$/, '').replace(/^./, (c) => c.toUpperCase())} colour</span>
                <div class="swatches">
                  <button class="sw theme" class:on={!propVal(cp)} aria-label="Theme colour" onclick={() => setProp(cp, null)}>T</button>
                  {#each $colorSwatches as c}<button class="sw" class:on={String(propVal(cp) ?? '').toUpperCase() === c} style="background:{c}" aria-label={c} onclick={() => setProp(cp, c)}></button>{/each}
                </div></div>
            {/each}
          {:else}
            <p class="muted small">Select a widget to set its reading, range, options and colours. Drag the handle to move it, the corner to resize.</p>
            <div class="row"><span class="small">Columns</span><input class="field tiny num" type="number" min="1" max="48" bind:value={spec.gridColumns} onchange={() => (dirty = true)} /></div>
            <div class="row"><span class="small">Rows</span><input class="field tiny num" type="number" min="1" max="48" bind:value={spec.gridRows} onchange={() => (dirty = true)} /></div>
          {/if}
        </div>
      </div>
    {:else}<div class="muted">Loading…</div>{/if}
  </div>
{/if}

{#if askImport}
  <Modal title="Import a dashboard" onclose={() => (askImport = false)}>
    <p class="muted small">Paste a dashboard file (schema 1). It gets a new id.</p>
    <textarea class="field ta" rows="8" bind:value={importText} placeholder={'{"schema": 1, "label": "...", "cells": [...]}'}></textarea>
    <button class="btn primary" disabled={!importText.trim()} onclick={importNow}>Import</button>
  </Modal>
{/if}
{#if askDelete}
  <Modal title="Delete {askDelete.label}?" onclose={() => (askDelete = null)}>
    <p class="muted">The file is removed. This cannot be undone.</p>
    <button class="btn danger on" onclick={async () => { const ok = await act(octave.call<boolean>('dashboardManager', 'deleteDashboard', askDelete!.id)); askDelete = null; toast(ok ? 'Deleted' : 'Could not delete', ok ? 'ok' : 'bad') }}>Delete</button>
  </Modal>
{/if}
{#if askCopy}
  <Modal title="Copy {askCopy.label}" onclose={() => (askCopy = null)}>
    <input class="field" bind:value={copyName} aria-label="Name" />
    <button class="btn primary" onclick={copy}>Make a copy</button>
  </Modal>
{/if}
{#if pickPid && meta}
  <Modal title="Reading" onclose={() => (pickPid = false)}>
    <input class="field" placeholder="Find a reading" bind:value={pidQ} />
    <div class="wrap">{#each pidCandidates as pid}<button class="btn" class:on={cell?.paramId === pid} onclick={() => { spec!.cells[sel] = { ...cell!, paramId: pid }; dirty = true; pickPid = false }}>{$catalog.get(pid)?.title ?? pid}</button>{/each}</div>
  </Modal>
{/if}

<style>
  .wrap { display: flex; flex-wrap: wrap; gap: 8px; }
  .cards { display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: var(--gap); }
  .card { display: flex; flex-direction: column; gap: 12px; }
  .open { display: flex; flex-direction: column; gap: 4px; text-align: left; }
  .name { font-size: 22px; font-weight: 700; }
  .acts { display: flex; gap: 6px; }
  .bad { color: var(--bad); }
  .view { display: flex; flex-direction: column; height: 100%; padding: var(--gap); gap: var(--gap); }
  .viewbar { display: flex; align-items: center; gap: 12px; }
  .viewbar .name { flex: 1; }
  .name-field { flex: 1; }
  .stage { flex: 1; min-height: 0; position: relative; }
  .edit-layout { flex: 1; min-height: 0; display: grid; grid-template-columns: 170px 1fr 280px; gap: var(--gap); }
  .palette, .props { padding: 14px; display: flex; flex-direction: column; gap: 8px; overflow-y: auto; min-width: 0; }
  .pal { display: flex; align-items: center; gap: 10px; min-height: 48px; padding: 0 12px; border-radius: 14px; font-weight: 600; color: var(--ink-2); text-align: left; }
  .pal.on { background: var(--control-on); color: var(--control-on-ink); }
  .glyph { width: 24px; text-align: center; font-size: 18px; }
  .grid-stage { touch-action: none; }
  .grid-stage.placing { cursor: crosshair; }
  .gridlines { position: absolute; inset: 0; pointer-events: none; border-radius: var(--radius-s);
    background-image: linear-gradient(to right, var(--glass-edge) 1px, transparent 1px), linear-gradient(to bottom, var(--glass-edge) 1px, transparent 1px);
    background-size: calc(100% / var(--cols)) calc(100% / var(--rows)); }
  .handle { position: absolute; pointer-events: none; }
  .mover { pointer-events: auto; position: absolute; top: 6px; left: 6px; width: 40px; height: 40px; border-radius: 12px; background: var(--control-on); color: var(--control-on-ink); display: grid; place-items: center; cursor: grab; touch-action: none; }
  .grip { pointer-events: auto; position: absolute; right: 4px; bottom: 4px; width: 32px; height: 32px; border-radius: 10px; background: var(--accent); cursor: nwse-resize; touch-action: none; }
  .ph { display: flex; justify-content: space-between; align-items: center; }
  .props .row { min-height: 52px; }
  .props .row.stack { flex-direction: column; align-items: stretch; gap: 4px; }
  .tiny { width: 110px; }
  .props .field:not(.tiny) { max-width: 160px; }
  .swatches { display: flex; gap: 6px; flex-wrap: wrap; }
  .sw { width: 32px; height: 32px; border-radius: 50%; border: 3px solid transparent; } .sw.on { border-color: var(--ink); }
  .sw.theme { background: var(--control); color: var(--ink); font-weight: 700; }
  .ta { min-height: 160px; padding: 12px 16px; font-family: ui-monospace, monospace; font-size: 14px; }
  @media (max-width: 900px) {
    .edit-layout { grid-template-columns: minmax(0, 1fr); grid-template-rows: auto auto auto; }
    .palette { flex-direction: row; flex-wrap: nowrap; overflow-x: auto; scrollbar-width: none; }
    .palette .pal { flex: none; }
  }
  @media (max-aspect-ratio: 1/1) {
    /* dashboards are laid out for landscape: keep their shape, letterbox the rest */
    .view { height: auto; min-height: 100%; }
    .stage { flex: none; aspect-ratio: var(--cols, 12) / var(--rows, 6); width: 100%; min-height: calc(var(--rows, 6) * 52px); }
    .palette .hint { display: none; }
  }
</style>
