// DashboardEditor.qml
//
// The dashboard editor: a full-screen page (no nav bar; left through Done
// or Close), StackView-pushed from the chooser in OBDMenu.qml. Design and
// rationale: docs/DASHBOARD_EDITOR_REVIEW.md §4.2.
//
// Interaction model (touch first, select then manipulate):
//   - tap a widget to select it: it lifts and a floating toolbar appears
//     beside it (Data · Settings · S/M/L · Duplicate · Delete);
//   - drag the selected widget to move it (press and hold drags any
//     widget): neighbours swap or slide aside, live, as you go;
//   - with a widget selected, tap another one to Swap with it, or tap free
//     space to Move it there; with nothing selected, tap free space to Add
//     a widget there (or + Add, which finds room by itself);
//   - Settings opens a side sheet (bottom sheet on a portrait screen) and
//     zooms the dashboard to fit beside it;
//   - Delete can be undone from the snackbar, and every edit from Undo.
//
// This page owns the working spec. Every mutation builds a new `cells` array
// and goes through _apply(), which records it for undo and updates the
// canvas's ListModel only where cells changed (see EditorCanvas). Moves and
// resizes go through dashboardManager.arrange(): the widgets they land on
// swap or move aside. DashboardManager provides save/delete/duplicate, so
// this page is pure QML.
// saveDashboard() requires a unique `id`; built-in ids are rejected and user
// ids overwrite, so a new dashboard takes its id from
// dashboardManager.uniqueIdForLabel().

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import "../.." as App

Item {
    id: editor
    objectName: "dashboardEditor"

    // Main.qml hides the nav bar while this page is on top
    readonly property bool hidesNavigation: true

    // Local dp/dpMin wrappers — mirror OBDMenu's Android singleton-function bug
    // workaround so this page scales identically to the one that pushes it.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    readonly property real touchSize: editorCanvas.touchSize

    // Injected by StackView.push() from OBDMenu.qml.
    property StackView stackView: null
    property var callerPage: null          // OBDMenu page — for setActiveDashboard after save

    // Non-empty when editing an existing user dashboard (keeps its id on save).
    // Empty = creating a new dashboard (id generated from the label).
    property string editingId: ""

    // Grid resolution of the dashboard being edited.
    property int gridColumns: 12
    property int gridRows: 6

    // A new dashboard starts at the grid density that suits this screen
    // (defaultDensity()); false keeps gridColumns / gridRows as given
    property bool autoDensity: true
    // Open on the portrait layout when the screen is upright; false keeps
    // landscape until setLayoutMode()
    property bool autoLayoutMode: true

    // A new dashboard that starts from a spec instead of an empty grid
    // (a template, or a built-in being customized): its label, grid and
    // cells, not saved until Done. fitToScreen also lays it out again on
    // this screen's default density (a template, written for 6 x 4).
    property var initialSpec: null
    property bool fitToScreen: false

    // Optional grid geometry (dp) carried through from a loaded spec so a
    // hand-authored margins/spacing survives a save. undefined = not in the
    // spec; the renderer (and canvas) then fall back to their 20/16 defaults.
    property var specMargins: undefined
    property var specSpacing: undefined

    // Working cell list. Never mutated in place: _apply() a new array.
    property var cells: []

    property int selectedIndex: -1
    readonly property var selectedCell:
        (selectedIndex >= 0 && selectedIndex < cells.length) ? cells[selectedIndex] : null
    readonly property var selectedMeta: selectedCell ? App.WidgetCatalog.metaFor(selectedCell.type) : null

    // The settings sheet for the selected widget
    property bool sheetOpen: false
    readonly property bool _sheetShown: sheetOpen && selectedCell !== null

    // Unsaved-changes flag — gates the "Save changes?" question on Close and
    // the draft rescue on destruction.
    property bool dirty: false

    // Serialized working state injected by the chooser's "Resume draft"
    // button. Non-empty = restore it instead of loading editingId.
    property string draftJson: ""

    // Live preview: ask obdManager to poll the PIDs on the canvas while the
    // editor is open (polling is demand-driven; the OBD page withdraws its
    // own demand while this page is pushed over it).
    readonly property var _demandIds: {
        var seen = {}, out = []
        for (var i = 0; i < cells.length; i++) {
            var id = cells[i].paramId
            if (id && !seen[id]) { seen[id] = true; out.push(id) }
        }
        return out
    }
    function _pushDemand(ids) {
        if (typeof obdManager !== "undefined" && obdManager && obdManager.setParameterDemand)
            obdManager.setParameterDemand("editor", ids)
    }
    on_DemandIdsChanged: _pushDemand(_demandIds)

    // ── Polling cost ─────────────────────────────────────────────────────
    // An ELM327 answers about 15 requests a second in all, shared by every
    // value on the screen: the status line says what each gets
    readonly property real elmRequestsPerSecond: 15
    readonly property int polledCount: _demandIds.filter(function(id) {
        return !App.OBDParameterModel.isSensorParameter(id) }).length
    readonly property real updatesPerValue: polledCount > 0 ? elmRequestsPerSecond / polledCount : 0

    // ── Driving lock ─────────────────────────────────────────────────────
    // Editing pauses while the car moves (above about 5 km/h, back under
    // about 2.5): the dashboard stays visible, nothing can be changed, and
    // Done / Close still work. "I'm a passenger" lifts it for this editor;
    // the dashboardEditWhileDriving setting (Grid · Demo menu) for good.
    // Read from the real store, so the editor's demo data never locks it.
    readonly property real _speedMph: (typeof paramStore !== "undefined" && paramStore
                                       && typeof paramStore.SPEED === "number") ? paramStore.SPEED : 0
    property bool moving: false
    on_SpeedMphChanged: moving = _speedMph > (moving ? 1.5 : 3)
    property bool passengerOverride: false
    property bool editWhileDriving: (typeof settingsManager !== "undefined" && settingsManager
                                     && settingsManager.get_setting_with_default)
                                    ? settingsManager.get_setting_with_default("dashboardEditWhileDriving", false) === true
                                    : false
    function setEditWhileDriving(on) {
        editWhileDriving = on
        if (typeof settingsManager !== "undefined" && settingsManager && settingsManager.save_setting)
            settingsManager.save_setting("dashboardEditWhileDriving", on)
    }
    readonly property bool drivingLocked: moving && !passengerOverride && !editWhileDriving
    onDrivingLockedChanged: {
        if (!drivingLocked) return
        clearSelection()
        widgetPalette.close()
        pidPicker.close()
    }

    // Where the next palette pick lands: a grid cell, or -1 for "find room"
    property int _pendingCol: -1
    property int _pendingRow: -1

    // A tap on free space: {col, row} while its Add here / Move here is shown
    property var _tapCell: null
    // A tap on another widget while one is selected: its Swap / Select
    property int _swapTarget: -1

    property bool _densityPending: false

    Component.onCompleted: {
        _load()
        _resetHistory()
        _densityPending = autoDensity && editingId.length === 0 && draftJson.length === 0
                          && (cells.length === 0 || fitToScreen)
        Qt.callLater(_pickDefaultDensity)
    }

    function _load() {
        if (draftJson.length > 0 && _restoreDraft(draftJson)) return
        if (editingId.length === 0) {
            if (initialSpec && initialSpec.cells) _loadSpec(initialSpec)
            return
        }
        if (typeof dashboardManager === "undefined" || !dashboardManager) return
        var spec = dashboardManager.loadDashboard(editingId)
        if (!spec || !spec.cells) {
            _showError("Could not load dashboard \"" + editingId + "\"")
            return
        }
        // A newer build's file: saving it from here would strip what this
        // build doesn't know
        if (spec.schema > dashboardManager.supportedSchema) {
            _showError("\"" + (spec.label || editingId) + "\" needs a newer version of OCTAVE to edit")
            editingId = ""
            return
        }
        _loadSpec(spec)
    }

    function _loadSpec(spec) {
        // Already normalised by the manager: in the grid, no overlaps
        gridColumns = spec.gridColumns
        gridRows = spec.gridRows
        specMargins = spec.margins
        specSpacing = spec.spacing
        var land = _withUids(spec.cells)
        _otherLayout = null
        if (spec.layouts && spec.layouts.portrait) {
            // The same cells (same uids) where the portrait layout puts them
            var o = dashboardManager.orientedSpec(spec, true)
            _otherLayout = { "cols": o.gridColumns, "rows": o.gridRows,
                             "cells": o.cells.map(function(c, i) {
                                 var m = Object.assign({}, c); m._uid = land[i]._uid; return m }) }
        }
        layoutMode = "landscape"
        _show(land)
        nameField.text = spec.label ? spec.label : ""
        showWhen = spec.showWhen ? Object.assign({}, spec.showWhen) : null
        alertBanner = spec.alertBanner !== false
    }

    // ── Dashboard options (the options popup) ────────────────────────────
    // showWhen {paramId, above, below} (limits in the PID's own unit): the
    // OBD page brings this dashboard up while it holds. alertBanner: the
    // renderer's red strip naming widgets at their critical limit. Not in
    // undo; a change marks the work unsaved.
    property var showWhen: null
    property bool alertBanner: true

    function setShowWhen(when) {
        if (drivingLocked) return
        var w = null
        if (when && when.paramId) {
            w = { "paramId": when.paramId }
            if (typeof when.above === "number" && isFinite(when.above)) w.above = when.above
            if (typeof when.below === "number" && isFinite(when.below)) w.below = when.below
        }
        if (JSON.stringify(w) === JSON.stringify(showWhen)) return
        showWhen = w
        dirty = true
    }

    function setAlertBanner(on) {
        if (drivingLocked || on === alertBanner) return
        alertBanner = on
        dirty = true
    }

    // ── Landscape / portrait ─────────────────────────────────────────────
    // A dashboard has a landscape layout (the spec's cells) and may have a
    // portrait one (schema 2 layouts.portrait: a position per cell). The
    // editor shows one, `layoutMode`, as `cells` / gridColumns / gridRows,
    // and keeps the other aside in `_otherLayout` ({cols, rows, cells}).
    // Cells carry an editor-only `_uid` (dropped on save) so the two stay
    // matched through adds and deletes: on a switch the other layout takes
    // every cell's data and options from the one on screen and keeps its own
    // positions; a cell it lacks goes to the nearest free spot. The first
    // switch to portrait derives that layout (dashboardManager.
    // derivePortrait); from then on it is the user's, saved with the rest.
    property string layoutMode: "landscape"
    property var _otherLayout: null
    property int _nextUid: 1

    function _withUids(list) {
        var out = []
        for (var i = 0; i < list.length; i++) {
            var c = Object.assign({}, list[i])
            c._uid = _nextUid++
            out.push(c)
        }
        return out
    }

    // `target`'s positions for `current`'s cells (in current's order), on
    // target's grid, rows added only if a new cell finds no room at all
    function _reconcile(target, current) {
        var byUid = {}
        for (var t = 0; t < target.cells.length; t++) byUid[target.cells[t]._uid] = target.cells[t]
        var cols = target.cols, rows = target.rows
        var out = [], pending = []
        for (var i = 0; i < current.length; i++) {
            var c = Object.assign({}, current[i])
            var g = byUid[c._uid]
            if (g) {
                c.col = g.col; c.row = g.row; c.colSpan = g.colSpan; c.rowSpan = g.rowSpan
            } else {
                pending.push({ "index": i, "cs": c.colSpan || 1, "rs": c.rowSpan || 1 })
                c.col = 0; c.row = 0; c.colSpan = 0; c.rowSpan = 0   // overlaps nothing until placed
            }
            out.push(c)
        }
        for (var n = 0; n < pending.length; n++) {
            var p = pending[n], placed = null
            while (!placed) {
                var tries = [[Math.min(cols, p.cs), Math.min(rows, p.rs)], [1, 1]]
                for (var k = 0; k < tries.length && !placed; k++) {
                    var nf = dashboardManager.nearestFree(out, cols, rows, 0, 0, tries[k][0], tries[k][1], p.index)
                    if (nf.ok) placed = nf
                }
                if (!placed) {
                    if (rows >= 48) { placed = { "col": 0, "row": 0, "colSpan": 1, "rowSpan": 1 }; break }
                    rows++
                }
            }
            out[p.index].col = placed.col; out[p.index].row = placed.row
            out[p.index].colSpan = placed.colSpan; out[p.index].rowSpan = placed.rowSpan
        }
        return { "cols": cols, "rows": rows, "cells": out }
    }

    function setLayoutMode(mode) {
        if (mode === layoutMode || (mode !== "landscape" && mode !== "portrait") || drivingLocked) return
        var target = _otherLayout
        var created = false
        if (!target) {
            created = true
            if (mode === "portrait") {
                var d = dashboardManager.derivePortrait(cells, gridColumns, gridRows)
                target = { "cols": d.gridColumns, "rows": d.gridRows, "cells": cells.map(function(c, i) {
                    var m = Object.assign({}, c)
                    m.col = d.cells[i].col; m.row = d.cells[i].row
                    m.colSpan = d.cells[i].colSpan; m.rowSpan = d.cells[i].rowSpan
                    return m }) }
            } else {
                var g = _densities[defaultDensity()]
                target = { "cols": g[0], "rows": g[1], "cells": [] }
            }
        }
        var r = _reconcile(target, cells)
        _hideChoosers()
        var wasDirty = dirty
        _otherLayout = { "cols": gridColumns, "rows": gridRows, "cells": cells }
        layoutMode = mode
        _apply(r.cells, r.cols, r.rows)
        // Only a layout made just now is new work; a plain switch isn't
        dirty = wasDirty || (created && cells.length > 0)
    }

    // Both layouts as they would be saved: {land, port}, each {cols, rows,
    // cells} in the same cell order; port null when there is none
    function _layouts() {
        var cur = { "cols": gridColumns, "rows": gridRows, "cells": cells }
        if (layoutMode === "landscape")
            return { "land": cur, "port": _otherLayout ? _reconcile(_otherLayout, cells) : null }
        var g = _densities[defaultDensity()]
        return { "land": _reconcile(_otherLayout || { "cols": g[0], "rows": g[1], "cells": [] }, cells),
                 "port": cur }
    }

    // The spec to save (or keep as a draft): the landscape cells, and the
    // portrait layout when there is one (schema 2). No id or label.
    function specToSave() {
        var L = _layouts()
        function plain(c) {
            var o = Object.assign({}, c)
            delete o._uid
            return o
        }
        var spec = { "schema": L.port ? 2 : 1, "gridColumns": L.land.cols, "gridRows": L.land.rows,
                     "cells": L.land.cells.map(plain) }
        if (showWhen && (showWhen.above !== undefined || showWhen.below !== undefined))
            spec.showWhen = Object.assign({}, showWhen)
        if (!alertBanner)
            spec.alertBanner = false
        if (L.port)
            spec.layouts = { "portrait": { "gridColumns": L.port.cols, "gridRows": L.port.rows,
                "cells": L.port.cells.map(function(c) {
                    return { "col": c.col, "row": c.row, "colSpan": c.colSpan, "rowSpan": c.rowSpan } }) } }
        return _withGridGeometry(spec)
    }

    // ── Grid density ─────────────────────────────────────────────────────
    // Coarse / Normal / Fine, per dashboard (review §5 decision 1). The
    // grids are for a landscape screen and turned for a portrait one. A
    // dashboard on some other grid (hand-written, imported) reads "custom"
    // until a density is picked.
    readonly property var _densities: ({
        "coarse": [4, 3], "normal": [6, 4], "fine": [12, 8]
    })
    readonly property bool _portrait: layoutMode === "portrait"
    function densityGrid(name) {
        var d = _densities[name]
        if (!d) return null
        return _portrait ? { "cols": d[1], "rows": d[0] } : { "cols": d[0], "rows": d[1] }
    }
    readonly property string density: {
        for (var name in _densities) {
            var d = _densities[name]
            if ((gridColumns === d[0] && gridRows === d[1]) || (gridColumns === d[1] && gridRows === d[0]))
                return name
        }
        return "custom"
    }

    // The finest density whose cells are at least 12 mm on this screen
    // (normal where the screen doesn't say how big it is)
    function defaultDensity() {
        var mm = Screen.pixelDensity
        if (!(mm > 1) || canvasFrame.designW <= 0 || canvasFrame.designH <= 0) return "normal"
        var names = ["fine", "normal", "coarse"]
        var m = editorCanvas._margin, s = editorCanvas._spacing
        for (var i = 0; i < names.length; i++) {
            var g = densityGrid(names[i])
            var w = (canvasFrame.designW - 2 * m - s * (g.cols - 1)) / g.cols
            var h = (canvasFrame.designH - 2 * m - s * (g.rows - 1)) / g.rows
            if (Math.min(w, h) / mm >= 12) return names[i]
        }
        return "coarse"
    }

    // Once the canvas has a size: open on the layout for this screen's
    // shape (portrait on an upright one; derived there if the dashboard has
    // none, saved only if the user saves), then a new dashboard's density
    property bool _settled: false
    function _pickDefaultDensity() {
        if (_settled || canvasFrame.width <= 0 || canvasFrame.height <= 0) return
        _settled = true
        if (autoLayoutMode && canvasFrame.height > canvasFrame.width && layoutMode !== "portrait") {
            var wasDirty = dirty
            setLayoutMode("portrait")
            dirty = wasDirty
        }
        if (_densityPending) {
            _densityPending = false
            var g = densityGrid(defaultDensity())
            if (cells.length > 0) {
                // A template: the same layout on this screen's grid, when it fits
                if (fitToScreen && (g.cols !== gridColumns || g.rows !== gridRows)) {
                    var r = dashboardManager.rescale(cells, gridColumns, gridRows, g.cols, g.rows)
                    if (r.ok) {
                        gridColumns = g.cols
                        gridRows = g.rows
                        _show(r.cells)
                    }
                }
            } else {
                gridColumns = g.cols
                gridRows = g.rows
            }
        }
        _resetHistory()
    }

    function setDensity(name) {
        var g = densityGrid(name)
        if (!g || (g.cols === gridColumns && g.rows === gridRows)) return
        var r = dashboardManager.rescale(cells, gridColumns, gridRows, g.cols, g.rows)
        if (!r.ok) {
            _showError(r.reason)
            return
        }
        _hideChoosers()
        _apply(r.cells, g.cols, g.rows)
    }

    // ── Draft rescue ─────────────────────────────────────────────────────
    // Leaving without Done / Close (the app closing, a page change from
    // outside) destroys this page, so unsaved work is stashed as a draft on
    // destruction and offered back via "Resume draft" in the chooser. Normal
    // exits (Done, Discard) clear `dirty` first, so they don't leave a draft.
    Component.onDestruction: {
        _pushDemand([])
        // Never let editor demo data leak into the real OBD views.
        App.OBDParameterModel.simulationActive = false
        if (!dirty) return
        if (typeof settingsManager === "undefined" || !settingsManager) return
        var draft = specToSave()
        draft.editingId = editingId
        draft.label = nameField.text
        settingsManager.save_setting("dashboardEditorDraft", JSON.stringify(draft))
    }

    // Add margins/spacing to a spec-shaped object only when the loaded spec
    // had them, so a dashboard that relies on the renderer defaults keeps
    // relying on them (and its JSON stays minimal).
    function _withGridGeometry(obj) {
        if (specMargins !== undefined && specMargins !== null) obj["margins"] = specMargins
        if (specSpacing !== undefined && specSpacing !== null) obj["spacing"] = specSpacing
        return obj
    }

    function _restoreDraft(json) {
        try {
            var draft = JSON.parse(json)
            // The same rules a save applies; the label is kept as typed
            var spec = dashboardManager.normalizedSpec({
                "schema": draft.schema, "gridColumns": draft.gridColumns, "gridRows": draft.gridRows,
                "margins": draft.margins, "spacing": draft.spacing,
                "cells": draft.cells || [], "layouts": draft.layouts
            })
            if (!spec.cells) return false
            editingId = draft.editingId ? draft.editingId : ""
            _loadSpec(spec)
            nameField.text = draft.label ? draft.label : ""
            dirty = true
            return true
        } catch (e) {
            console.warn("DashboardEditor: could not restore draft:", e)
            return false
        }
    }

    // Clear the stored draft — only when this session owned one (resumed it),
    // so opening and immediately closing a fresh editor can't wipe a draft
    // from an earlier session.
    function _clearStoredDraft() {
        if (draftJson.length === 0) return
        if (typeof settingsManager !== "undefined" && settingsManager)
            settingsManager.save_setting("dashboardEditorDraft", "")
    }

    // ── The canvas model ─────────────────────────────────────────────────
    // One row per cell, in `cells` order. _show() brings it in line with a
    // new array touching only rows that differ, so the canvas keeps every
    // other widget as it is.
    ListModel { id: cellModel }

    function _row(c) {
        return {
            "type": c.type !== undefined ? String(c.type) : "",
            "paramId": c.paramId ? String(c.paramId) : "",
            // Not "row"/"column": views give delegates those names themselves
            "cellCol": c.col !== undefined ? c.col : 0,
            "cellRow": c.row !== undefined ? c.row : 0,
            "cellColSpan": c.colSpan !== undefined ? c.colSpan : 1,
            "cellRowSpan": c.rowSpan !== undefined ? c.rowSpan : 1,
            "propsJson": JSON.stringify(c.props || {})
        }
    }

    // A plain row (from _row) against a model element
    function _sameRow(row, element) {
        for (var k in row) if (row[k] !== element[k]) return false
        return true
    }

    function _show(next) {
        var rows = next.map(_row)
        var n = cellModel.count
        var i
        if (rows.length === n + 1 || rows.length === n - 1) {
            // One cell added or removed: find where, then check the rest lines up
            var longer = rows.length > n ? rows : null
            var first = 0
            while (first < Math.min(n, rows.length) && _sameRow(rows[first], cellModel.get(first))) ++first
            var ok = true
            if (longer) {
                for (i = first; i < n && ok; ++i) ok = _sameRow(rows[i + 1], cellModel.get(i))
                if (ok) cellModel.insert(first, rows[first])
            } else {
                for (i = first; i < rows.length && ok; ++i) ok = _sameRow(rows[i], cellModel.get(i + 1))
                if (ok) cellModel.remove(first)
            }
            if (ok) { cells = next; return }
        }
        if (rows.length === n) {
            for (i = 0; i < n; ++i) {
                var cur = cellModel.get(i)
                for (var k in rows[i])
                    if (cur[k] !== rows[i][k]) cellModel.setProperty(i, k, rows[i][k])
            }
        } else {
            cellModel.clear()
            for (i = 0; i < rows.length; ++i) cellModel.append(rows[i])
        }
        cells = next
    }

    // ── Undo / redo ──────────────────────────────────────────────────────
    // Snapshots of the grid and its cells (JSON), newest last; _historyIndex
    // is the one on screen. A new edit drops anything after it.
    readonly property int maxUndo: 50
    property var _history: []
    property int _historyIndex: -1
    readonly property bool canUndo: _historyIndex > 0
    readonly property bool canRedo: _historyIndex >= 0 && _historyIndex < _history.length - 1

    function _snapshot() {
        return JSON.stringify({ "mode": layoutMode, "cols": gridColumns, "rows": gridRows, "cells": cells,
                                "other": _otherLayout })
    }

    function _resetHistory() {
        _history = [_snapshot()]
        _historyIndex = 0
    }

    // Every edit lands here: show it (on a new grid size when given), mark
    // the work unsaved, record it
    function _apply(next, cols, rows) {
        if (drivingLocked) {
            _showError("Editing is paused while the car is moving")
            return
        }
        if (cols !== undefined) {
            gridColumns = cols
            gridRows = rows
        }
        _show(next)
        dirty = true
        var h = _history.slice(0, _historyIndex + 1)
        h.push(_snapshot())
        while (h.length > maxUndo + 1) h.shift()
        _history = h
        _historyIndex = h.length - 1
    }

    function _restore(index) {
        _historyIndex = index
        var snap = JSON.parse(_history[index])
        layoutMode = snap.mode || "landscape"
        _otherLayout = snap.other || null
        gridColumns = snap.cols
        gridRows = snap.rows
        _show(snap.cells)
        dirty = true
        _hideChoosers()
        if (selectedIndex >= cells.length) clearSelection()
    }

    function undo() { if (canUndo && !drivingLocked) _restore(_historyIndex - 1) }
    function redo() { if (canRedo && !drivingLocked) _restore(_historyIndex + 1) }

    Shortcut { sequences: [StandardKey.Undo]; onActivated: editor.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; onActivated: editor.redo() }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (editor._tapCell || editor._swapTarget >= 0) editor._hideChoosers()
            else if (editor.sheetOpen) editor.closeSheet()
            else editor.clearSelection()
        }
    }
    Shortcut {
        sequences: [StandardKey.Delete]
        enabled: editor.selectedCell !== null && !nameField.activeFocus
        onActivated: editor.deleteCell(editor.selectedIndex)
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: editor.selectedCell !== null
        onActivated: editor.duplicateCell(editor.selectedIndex)
    }
    Shortcut { sequence: "Left";  enabled: editor.selectedCell !== null && !nameField.activeFocus; onActivated: editor.nudgeCell(editor.selectedIndex, -1, 0) }
    Shortcut { sequence: "Right"; enabled: editor.selectedCell !== null && !nameField.activeFocus; onActivated: editor.nudgeCell(editor.selectedIndex, 1, 0) }
    Shortcut { sequence: "Up";    enabled: editor.selectedCell !== null && !nameField.activeFocus; onActivated: editor.nudgeCell(editor.selectedIndex, 0, -1) }
    Shortcut { sequence: "Down";  enabled: editor.selectedCell !== null && !nameField.activeFocus; onActivated: editor.nudgeCell(editor.selectedIndex, 0, 1) }

    // ── Sizes ────────────────────────────────────────────────────────────
    // From the catalog's minSize / aspect and this canvas's cell size

    function minSpanFor(type) {
        return dashboardManager.minSpan(type, gridColumns, gridRows, editorCanvas.cellWidthDp,
                                        editorCanvas.cellHeightDp, editorCanvas.spacingDp)
    }
    function sizeOptionsFor(type) {
        return dashboardManager.sizeOptions(type, gridColumns, gridRows, editorCanvas.cellWidthDp,
                                            editorCanvas.cellHeightDp, editorCanvas.spacingDp)
    }

    // Spans to try for a new widget, best first: its catalog default (given
    // for a 12 x 6 grid) scaled to this one, then S, then the smallest
    // readable; none below that
    function _newSpans(type) {
        var meta = App.WidgetCatalog.metaFor(type)
        var least = minSpanFor(type)
        var out = []
        function add(cs, rs) {
            cs = Math.max(least.colSpan, Math.min(gridColumns, cs))
            rs = Math.max(least.rowSpan, Math.min(gridRows, rs))
            for (var i = 0; i < out.length; i++)
                if (out[i].colSpan === cs && out[i].rowSpan === rs) return
            out.push({ "colSpan": cs, "rowSpan": rs })
        }
        add(Math.max(1, Math.round(meta.defaultColSpan * gridColumns / 12)),
            Math.max(1, Math.round(meta.defaultRowSpan * gridRows / 6)))
        var options = sizeOptionsFor(type)
        if (options.length > 0) add(options[0].colSpan, options[0].rowSpan)
        add(least.colSpan, least.rowSpan)
        return out
    }

    // ── Cell mutations (single owner of the working spec) ───────────────

    // Replace cells[index] with a shallow-merged copy
    function _patchCell(index, patch) {
        var next = cells.slice()
        var merged = {}
        for (var k in next[index]) merged[k] = next[index][k]
        for (var p in patch) merged[p] = patch[p]
        next[index] = merged
        _apply(next)
    }

    // A new widget at grid cell (col, row), or wherever there is room when
    // col < 0. At the tapped cell it shrinks (not below readable) before
    // going elsewhere. Returns whether it was placed.
    function addCellAt(type, col, row) { return _addCell(type, col, row, "") }

    // The same, bound to `paramId` when given (data-first add)
    function _addCell(type, col, row, paramId, props) {
        var meta = App.WidgetCatalog.metaFor(type)
        if (!meta) return false
        var spans = _newSpans(type)
        var place = null
        var i
        if (col >= 0 && row >= 0) {
            // Shifted left / up to stay on the grid, still over the tapped cell
            for (i = 0; i < spans.length && !place; i++) {
                var c0 = Math.min(col, gridColumns - spans[i].colSpan)
                var r0 = Math.min(row, gridRows - spans[i].rowSpan)
                if (c0 >= 0 && r0 >= 0 && editorCanvas.canPlace(c0, r0, spans[i].colSpan, spans[i].rowSpan, -1))
                    place = { "col": c0, "row": r0, "colSpan": spans[i].colSpan, "rowSpan": spans[i].rowSpan }
            }
            if (!place) {
                var least = spans[spans.length - 1]
                var fit = dashboardManager.fitAt(cells, gridColumns, gridRows, col, row,
                                                 spans[0].colSpan, spans[0].rowSpan)
                if (fit.colSpan >= least.colSpan && fit.rowSpan >= least.rowSpan
                        && editorCanvas.canPlace(fit.col, fit.row, fit.colSpan, fit.rowSpan, -1))
                    place = fit
            }
        }
        for (i = 0; i < spans.length && !place; i++) {
            var nf = dashboardManager.nearestFree(cells, gridColumns, gridRows, Math.max(0, col), Math.max(0, row),
                                                  spans[i].colSpan, spans[i].rowSpan, -1)
            if (nf.ok) place = nf
        }
        if (!place) {
            _showError("No room for a " + meta.displayName + ": make a widget smaller or remove one")
            return false
        }
        var next = cells.slice()
        var bound = (paramId && meta.supportedKinds.length > 0) ? paramId : ""
        next.push({
            "_uid": _nextUid++,
            "type": type, "paramId": bound,
            "col": place.col, "row": place.row,
            "colSpan": place.colSpan, "rowSpan": place.rowSpan,
            "props": props ? Object.assign({}, props) : {}
        })
        _apply(next)
        selectedIndex = next.length - 1
        // A gauge without a PID is useless — pick one now. Self-binding
        // widgets (supportedKinds: []) skip this.
        if (meta.supportedKinds.length > 0 && bound === "")
            openPidPicker()
        return true
    }

    // Cell `index` to (col, row, colSpan, rowSpan); the widgets it lands on
    // swap with it or move aside. Refused: nothing changes and the reason
    // is shown (the canvas has already put the dragged widget back).
    function _arrangeCell(index, col, row, cs, rs) {
        var c = cells[index]
        if (!c) return false
        if (c.col === col && c.row === row && c.colSpan === cs && c.rowSpan === rs) return true
        var result = dashboardManager.arrange(cells, gridColumns, gridRows, index, col, row, cs, rs)
        if (!result.ok) {
            _showError(result.reason)
            return false
        }
        _apply(result.cells)
        return true
    }

    function moveCell(index, col, row) {
        var c = cells[index]
        if (!c) return
        var cs = c.colSpan !== undefined ? c.colSpan : 1
        var rs = c.rowSpan !== undefined ? c.rowSpan : 1
        col = Math.max(0, Math.min(gridColumns - cs, col))
        row = Math.max(0, Math.min(gridRows - rs, row))
        _arrangeCell(index, col, row, cs, rs)
    }

    function nudgeCell(index, dCol, dRow) {
        var c = cells[index]
        if (!c) return
        moveCell(index, (c.col !== undefined ? c.col : 0) + dCol,
                        (c.row !== undefined ? c.row : 0) + dRow)
    }

    // The smallest a cell may be made: its readable minimum, or its current
    // size if that is already smaller (a dashboard from a bigger screen)
    function _floorSpan(c) {
        var least = minSpanFor(c.type)
        return { "colSpan": Math.min(least.colSpan, c.colSpan !== undefined ? c.colSpan : 1),
                 "rowSpan": Math.min(least.rowSpan, c.rowSpan !== undefined ? c.rowSpan : 1) }
    }

    function resizeCell(index, dColSpan, dRowSpan) {
        var c = cells[index]
        if (!c) return
        resizeCellTo(index, (c.colSpan !== undefined ? c.colSpan : 1) + dColSpan,
                            (c.rowSpan !== undefined ? c.rowSpan : 1) + dRowSpan)
    }

    // Absolute resize (drag handles, steppers), from the top-left corner
    function resizeCellTo(index, colSpan, rowSpan) {
        var c = cells[index]
        if (!c) return
        var col = c.col !== undefined ? c.col : 0
        var row = c.row !== undefined ? c.row : 0
        var floor = _floorSpan(c)
        var cs = Math.max(floor.colSpan, Math.min(gridColumns - col, colSpan))
        var rs = Math.max(floor.rowSpan, Math.min(gridRows - row, rowSpan))
        _arrangeCell(index, col, row, cs, rs)
    }

    // A size chip: the new size from the same top-left, moved in from the
    // edge when it would pass it
    function setCellSize(index, colSpan, rowSpan) {
        var c = cells[index]
        if (!c) return
        var col = Math.max(0, Math.min(c.col !== undefined ? c.col : 0, gridColumns - colSpan))
        var row = Math.max(0, Math.min(c.row !== undefined ? c.row : 0, gridRows - rowSpan))
        _arrangeCell(index, col, row, colSpan, rowSpan)
    }

    // Tap-tap swap: `index` goes where `other` is; `other` takes its place
    // when it fits there, else moves aside (DashboardGrid::arrange)
    function swapCells(index, other) {
        var o = cells[other]
        if (!o || !cells[index]) return
        moveCell(index, o.col !== undefined ? o.col : 0, o.row !== undefined ? o.row : 0)
    }

    // A copy beside the original (the nearest free spot of its size)
    function duplicateCell(index) {
        var c = cells[index]
        if (!c) return false
        var cs = c.colSpan !== undefined ? c.colSpan : 1
        var rs = c.rowSpan !== undefined ? c.rowSpan : 1
        var nf = dashboardManager.nearestFree(cells, gridColumns, gridRows,
                                              Math.min(gridColumns - cs, (c.col || 0) + cs), c.row || 0, cs, rs, -1)
        if (!nf.ok) {
            _showError("No room for a copy: make something smaller first")
            return false
        }
        var copy = JSON.parse(JSON.stringify(c))
        copy._uid = _nextUid++
        copy.col = nf.col
        copy.row = nf.row
        var next = cells.slice()
        next.push(copy)
        _apply(next)
        selectedIndex = next.length - 1
        return true
    }

    function removeCell(index) {
        var next = cells.slice()
        next.splice(index, 1)
        _apply(next)
        clearSelection()
    }

    // Delete from the toolbar / sheet / keyboard: undoable from the snackbar
    function deleteCell(index) {
        var c = cells[index]
        if (!c) return
        var meta = App.WidgetCatalog.metaFor(c.type)
        removeCell(index)
        _notify((meta ? meta.displayName : "Widget") + " removed", "Undo", editor.undo)
    }

    function setCellParam(index, paramId) {
        _patchCell(index, { "paramId": paramId })
    }

    function setCellProp(index, key, value) {
        var c = cells[index]
        if (!c) return
        var props = {}
        var existing = c.props || {}
        for (var k in existing) props[k] = existing[k]
        props[key] = value
        _patchCell(index, { "props": props })
    }

    // Set several props to one value in a single mutation (the Color row
    // writes e.g. fillColor + needleColor). `value === undefined` clears them.
    function setCellProps(index, keys, value) {
        var c = cells[index]
        if (!c) return
        var props = {}
        var existing = c.props || {}
        for (var k in existing) props[k] = existing[k]
        for (var i = 0; i < keys.length; i++) {
            if (value === undefined) delete props[keys[i]]
            else props[keys[i]] = value
        }
        _patchCell(index, { "props": props })
    }

    function clearCellProp(index, key) {
        var c = cells[index]
        if (!c || !c.props || c.props[key] === undefined) return
        var props = {}
        for (var k in c.props) {
            if (k === key) continue
            props[k] = c.props[k]
        }
        _patchCell(index, { "props": props })
    }

    // ── My widgets (dashboardManager.widgetPresets) ─────────────────────
    // A preset places its base type with its options, bound to its value
    // when it has one; "Save as my widget" keeps the selected widget's
    // type, value and options under a name.
    function addPreset(preset, col, row) {
        return _addCell(preset.baseType, col, row, preset.paramId || "", preset.props || {})
    }

    function saveAsPreset(index, label) {
        var c = cells[index]
        var name = String(label || "").trim()
        if (!c || name === "") return ""
        var id = dashboardManager.saveWidgetPreset({ "label": name, "baseType": c.type,
                                                     "paramId": c.paramId || "", "props": c.props || {} })
        if (id) _notify("Saved to My widgets as \"" + name + "\"")
        else _showError(dashboardManager.lastError || "Could not save the widget")
        return id
    }

    // ── Copy / paste style, apply to all ────────────────────────────────
    // A style is a widget's look without its data: its styleKeys props and
    // its colour (the first colorProps entry; set on every colour prop of
    // the target, as the Color row does). Pasting onto another type keeps
    // the keys that type has; the colour carries across types.
    property var copiedStyle: null

    function styleOf(index) {
        var c = cells[index]
        if (!c) return null
        var meta = App.WidgetCatalog.metaFor(c.type)
        var props = c.props || {}
        var keys = App.WidgetCatalog.styleKeys(c.type)
        var style = { "type": c.type, "props": {}, "color": undefined }
        for (var i = 0; i < keys.length; i++)
            if (props[keys[i]] !== undefined) style.props[keys[i]] = props[keys[i]]
        var colors = (meta && meta.colorProps) || []
        if (colors.length > 0 && props[colors[0]] !== undefined) style.color = props[colors[0]]
        return style
    }

    // cell `c` with `style` in place of its own (a new object)
    function _styled(c, style) {
        var meta = App.WidgetCatalog.metaFor(c.type)
        var keys = App.WidgetCatalog.styleKeys(c.type)
        var colors = (meta && meta.colorProps) || []
        var props = Object.assign({}, c.props || {})
        for (var i = 0; i < keys.length; i++) {
            delete props[keys[i]]
            if (style.props[keys[i]] !== undefined) props[keys[i]] = style.props[keys[i]]
        }
        for (var j = 0; j < colors.length; j++) {
            delete props[colors[j]]
            if (style.color !== undefined) props[colors[j]] = style.color
        }
        var out = Object.assign({}, c)
        out.props = props
        return out
    }

    function copyStyle(index) {
        var s = styleOf(index)
        if (!s) return
        copiedStyle = s
        var meta = App.WidgetCatalog.metaFor(s.type)
        _notify("Style copied from " + (meta ? meta.displayName : s.type))
    }

    function pasteStyle(index) {
        if (!copiedStyle || !cells[index]) return
        var next = cells.slice()
        next[index] = _styled(next[index], copiedStyle)
        _apply(next)
        _notify("Style pasted", "Undo", editor.undo)
    }

    // Every other widget of the same type takes this one's style, in one
    // undo step. Returns how many changed.
    function applyStyleToAll(index) {
        var src = cells[index]
        if (!src) return 0
        var style = styleOf(index)
        var next = cells.slice()
        var n = 0
        for (var i = 0; i < next.length; i++) {
            if (i === index || next[i].type !== src.type) continue
            var styled = _styled(next[i], style)
            if (JSON.stringify(styled.props) === JSON.stringify(next[i].props || {})) continue
            next[i] = styled
            n++
        }
        var meta = App.WidgetCatalog.metaFor(src.type)
        var name = meta ? meta.displayName : src.type
        if (n === 0) {
            _notify("Every " + name + " already looks like this")
            return 0
        }
        _apply(next)
        _notify("Style applied to " + n + " more " + name + (n === 1 ? "" : "s"), "Undo", editor.undo)
        return n
    }

    // How many other widgets share the selected one's type ("apply to all")
    readonly property int sameTypeCount: {
        if (!selectedCell) return 0
        var n = 0
        for (var i = 0; i < cells.length; i++)
            if (i !== selectedIndex && cells[i].type === selectedCell.type) n++
        return n
    }

    // ── Test values ──────────────────────────────────────────────────────
    // cell index → a value its widget shows instead of the live one (the
    // sheet's Test value slider). Not part of the spec: never saved, not in
    // undo, cleared when the sheet closes or the cells change.
    property var testValues: ({})
    function setTestValue(index, v) {
        var t = Object.assign({}, testValues)
        t[index] = v
        testValues = t
    }
    function clearTestValue(index) {
        if (testValues[index] === undefined) return
        var t = Object.assign({}, testValues)
        delete t[index]
        testValues = t
    }
    function _clearTestValues() { if (Object.keys(testValues).length > 0) testValues = ({}) }
    onSheetOpenChanged: if (!sheetOpen) _clearTestValues()
    onSelectedIndexChanged: _clearTestValues()
    onCellsChanged: _clearTestValues()

    // The sheet's JSON section: the cell's props replaced by `text` (an
    // object), held to the spec rules as a save would. Returns "" when
    // applied as written, else what went wrong or what was dropped.
    function setCellPropsJson(index, text) {
        var c = cells[index]
        if (!c) return "No widget selected"
        var obj
        try {
            obj = JSON.parse(text)
        } catch (e) {
            return "Not valid JSON: " + e.message
        }
        if (obj === null || typeof obj !== "object" || Array.isArray(obj))
            return "Options are an object: { \"key\": value, ... }"
        var probe = Object.assign({}, c)
        probe.props = obj
        var spec = dashboardManager.normalizedSpec({ "gridColumns": gridColumns, "gridRows": gridRows,
                                                     "cells": [probe] })
        var kept = (spec.cells && spec.cells.length > 0 && spec.cells[0].props) ? spec.cells[0].props : {}
        _patchCell(index, { "props": kept })
        var dropped = Object.keys(obj).filter(function(k) { return kept[k] === undefined })
        return dropped.length > 0 ? "Not kept (unknown or not a plain value): " + dropped.join(", ") : ""
    }

    function _hideChoosers() {
        _tapCell = null
        _swapTarget = -1
    }

    function clearSelection() {
        _hideChoosers()
        sheetOpen = false
        selectedIndex = -1
    }

    function select(index) {
        _hideChoosers()
        selectedIndex = index
    }

    function openSheet() {
        if (!selectedCell) return
        _hideChoosers()
        sheetOpen = true
    }
    function closeSheet() { sheetOpen = false }

    // "bind": the picked PID goes to the selected widget; "add": the
    // widget the catalog suggests for it is placed (data-first add)
    property string _pidMode: "bind"

    function openPidPicker() {
        if (!selectedCell) return
        var meta = App.WidgetCatalog.metaFor(selectedCell.type)
        _pidMode = "bind"
        pidPicker.title = "Choose data"
        pidPicker.supportedKinds = meta ? meta.supportedKinds : ["*"]
        pidPicker.currentParamId = selectedCell.paramId ? selectedCell.paramId : ""
        pidPicker.open()
    }

    function openDataFirst() {
        _pidMode = "add"
        pidPicker.title = "What do you want to see?"
        pidPicker.supportedKinds = ["*"]
        pidPicker.currentParamId = ""
        pidPicker.open()
    }

    // Data-first: the suggested widget for this PID, placed where Add was asked
    function addForParam(paramId) {
        var p = App.OBDParameterModel.displayParameters.filter(function(x) { return x.id === paramId })[0]
        var type = App.WidgetCatalog.suggestFor(paramId, p ? p.kind : "numeric")
        return _addCell(type, _pendingCol, _pendingRow, paramId)
    }

    function openPalette(col, row) {
        _hideChoosers()
        _pendingCol = col
        _pendingRow = row
        widgetPalette.open()
    }

    function _pidTitle(paramId) {
        if (!paramId) return ""
        var info = App.OBDParameterModel.getParamInfo(paramId)
        return info && info.title ? info.title : paramId
    }

    // Done: a picture of the canvas first (the dashboard's `thumbnail`,
    // carried by export), taken with nothing selected; saved without one if
    // the grab can't happen or takes too long
    property bool _saving: false
    Timer { id: grabFallback; interval: 1500; onTriggered: editor._finishSave("") }

    function save() {
        if (typeof dashboardManager === "undefined" || !dashboardManager) {
            _showError("No dashboard backend available")
            return
        }
        if (_saving) return
        _saving = true
        clearSelection()
        _hideChoosers()
        closeSheet()
        var w = 640, h = Math.max(1, Math.round(640 * editorCanvas.height / Math.max(1, editorCanvas.width)))
        var started = editor.visible && editorCanvas.width > 0 && editorCanvas.grabToImage(function(result) {
            editor._finishSave(dashboardManager.thumbnailFromImage(result.image))
        }, Qt.size(w, h))
        if (started) grabFallback.restart()
        else _finishSave("")
    }

    function _finishSave(thumbnail) {
        if (!_saving) return
        _saving = false
        grabFallback.stop()
        var label = nameField.text.trim().replace(/\s+/g, " ")   // as the manager stores it
        if (label.length === 0) label = "Untitled"

        var id = editingId.length > 0 ? editingId : dashboardManager.uniqueIdForLabel(label)
        var spec = specToSave()
        spec.id = id
        spec.label = label
        if (thumbnail) spec.thumbnail = thumbnail

        var savedId = dashboardManager.saveDashboard(spec)
        if (savedId && savedId.length > 0) {
            dirty = false
            _clearStoredDraft()
            if (callerPage && callerPage.setActiveDashboard)
                callerPage.setActiveDashboard(savedId)
            if (stackView) stackView.pop()
        } else {
            _showError("Save failed (the id is a built-in's or has unusual characters)")
        }
    }

    // Close: straight out when nothing changed, else ask once
    function close() {
        if (dirty) {
            closeDialog.open()
            return
        }
        discard()
    }

    function discard() {
        dirty = false              // confirmed discard — don't rescue a draft
        _clearStoredDraft()
        if (stackView) stackView.pop()
    }

    // ── Snackbar ─────────────────────────────────────────────────────────
    function _notify(msg, actionText, action) {
        snackText.text = msg
        snackbar.danger = false
        snackbar.actionText = actionText || ""
        snackbar.action = action || null
        snackbar.visible = true
        snackTimer.interval = actionText ? 5000 : 3000
        snackTimer.restart()
    }

    function _showError(msg) {
        _notify(msg)
        snackbar.danger = true
    }

    // ── Pieces ──────────────────────────────────────────────────────────
    // A touch-sized pill button for the toolbar, choosers and top bar
    component Pill: Rectangle {
        id: pill
        property string text: ""
        property bool checked: false
        property bool danger: false
        property bool primary: false
        property real maxLabelWidth: editor.dp(220)
        signal clicked()
        implicitHeight: editor.touchSize
        implicitWidth: Math.max(editor.touchSize, Math.min(pillText.implicitWidth, maxLabelWidth) + editor.dp(24))
        radius: height / 2
        opacity: enabled ? 1.0 : 0.4
        color: pillMouse.pressed ? Qt.lighter(_base, 1.25) : _base
        readonly property color _base: primary || checked ? App.Style.accent
                                       : Qt.darker(App.Style.obdBoxBackground, 1.25)
        border.width: (primary || checked) ? 0 : 1
        border.color: danger ? App.Style.statusDanger : Qt.darker(App.Style.obdBarColor, 1.5)

        Text {
            id: pillText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, pill.maxLabelWidth)
            text: pill.text
            elide: Text.ElideRight
            color: pill.primary || pill.checked ? App.Style.obdBoxBackground
                   : pill.danger ? App.Style.statusDanger : App.Style.primaryTextColor
            font.family: App.Style.fontFamily
            font.bold: pill.primary || pill.checked
            font.pixelSize: App.Spacing.overallText * 0.95
        }
        MouseArea {
            id: pillMouse
            anchors.fill: parent
            enabled: pill.enabled
            onClicked: pill.clicked()
        }
    }

    // A grid density in the More menu
    component DensityItem: MenuItem {
        property string density: ""
        objectName: "editorDensity_" + density
        implicitHeight: editor.touchSize
        checkable: true
        checked: editor.density === density
        enabled: !editor.drivingLocked
        font.family: App.Style.fontFamily
        onTriggered: editor.setDensity(density)
    }

    // No anchors here on purpose: StackView sizes pushed items itself and
    // warns ("conflicting anchors") if the page anchors to its parent.

    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor
    }

    // ── Top bar ─────────────────────────────────────────────────────────
    // Close · Undo · Redo · name · More (grid density, demo data) · Done
    Rectangle {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: editor.touchSize + dp(16)
        color: App.Style.obdBoxBackground

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: dp(10)
            anchors.rightMargin: dp(10)
            spacing: dp(8)

            Pill {
                objectName: "editorCloseButton"
                text: "Close"
                onClicked: editor.close()
            }
            Pill {
                objectName: "editorUndoButton"
                text: "Undo"
                enabled: editor.canUndo
                onClicked: editor.undo()
            }
            Pill {
                objectName: "editorRedoButton"
                text: "Redo"
                enabled: editor.canRedo
                onClicked: editor.redo()
            }

            TextField {
                id: nameField
                objectName: "editorNameField"
                Layout.fillWidth: true
                Layout.preferredHeight: editor.touchSize
                placeholderText: "Dashboard name"
                placeholderTextColor: Qt.darker(App.Style.obdLabelColor, 1.4)
                background: Rectangle {
                    radius: editor.dpMin(6, 2)
                    color: Qt.darker(App.Style.obdBoxBackground, 1.25)
                    border.color: nameField.activeFocus ? App.Style.accent : Qt.darker(App.Style.obdBarColor, 1.6)
                    border.width: 1
                }
                font.family: App.Style.fontFamily
                font.pixelSize: App.Spacing.overallText * 1.1
                color: App.Style.primaryTextColor
                onTextEdited: editor.dirty = true
                onAccepted: focus = false
            }

            // Which layout is on the canvas: the portrait one is made the
            // first time (derived from landscape) and kept from then on
            Pill {
                objectName: "editorPortraitButton"
                text: "Portrait"
                checked: editor.layoutMode === "portrait"
                enabled: !editor.drivingLocked
                onClicked: editor.setLayoutMode(checked ? "landscape" : "portrait")
            }

            Pill {
                id: moreButton
                objectName: "editorMoreButton"
                text: "Grid · Demo"
                onClicked: moreMenu.open()

                Menu {
                    id: moreMenu
                    y: moreButton.height
                    DensityItem {
                        density: "coarse"
                        text: "Coarse grid (" + editor.densityGrid("coarse").cols + " × " + editor.densityGrid("coarse").rows + ")"
                    }
                    DensityItem {
                        density: "normal"
                        text: "Normal grid (" + editor.densityGrid("normal").cols + " × " + editor.densityGrid("normal").rows + ")"
                    }
                    DensityItem {
                        density: "fine"
                        text: "Fine grid (" + editor.densityGrid("fine").cols + " × " + editor.densityGrid("fine").rows + ")"
                    }
                    MenuSeparator {}
                    MenuItem {
                        objectName: "editorOptionsButton"
                        implicitHeight: editor.touchSize
                        enabled: !editor.drivingLocked
                        text: "Alerts and auto-show…"
                        font.family: App.Style.fontFamily
                        onTriggered: optionsPopup.open()
                    }
                    MenuItem {
                        objectName: "editorEditWhileDriving"
                        implicitHeight: editor.touchSize
                        text: "Allow editing while driving (passenger)"
                        checkable: true
                        checked: editor.editWhileDriving
                        font.family: App.Style.fontFamily
                        onTriggered: editor.setEditWhileDriving(checked)
                    }
                    // Fake live values for every PID so widgets can be laid
                    // out and sized without a real OBD connection.
                    // Editor-scoped: force-disabled when this page is destroyed.
                    MenuItem {
                        objectName: "editorDemoButton"
                        implicitHeight: editor.touchSize
                        text: "Demo data"
                        checkable: true
                        checked: App.OBDParameterModel.simulationActive
                        font.family: App.Style.fontFamily
                        onTriggered: App.OBDParameterModel.simulationActive = checked
                    }
                }
            }

            Pill {
                objectName: "editorSaveButton"
                text: "Done"
                primary: true
                onClicked: editor.save()
            }
        }
    }

    // ── Canvas ──────────────────────────────────────────────────────────
    Rectangle {
        id: canvasFrame
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: dp(8)
        color: App.Style.obdBoxBackground
        radius: dpMin(10, 3)
        border.color: Qt.darker(App.Style.obdBarColor, 1.6)
        border.width: 1
        clip: true
        onWidthChanged: editor._pickDefaultDensity()
        onHeightChanged: editor._pickDefaultDensity()

        // The settings sheet: beside the dashboard on a landscape screen,
        // under it on a portrait one
        readonly property bool sheetSide: width >= height
        readonly property real sheetSize: sheetSide ? Math.min(dp(380), width * 0.45)
                                                    : Math.min(dp(420), height * 0.5)
        readonly property real availW: editor._sheetShown && sheetSide ? width - sheetSize : width
        readonly property real availH: editor._sheetShown && !sheetSide ? height - sheetSize : height
        // The layout being edited is the other shape than this screen's (a
        // portrait layout on a landscape screen, or the reverse): the canvas
        // is that screen turned, fitted into the frame
        readonly property bool rotated: (editor.layoutMode === "portrait") !== (height > width)
        readonly property real designW: !rotated ? width : (width > height ? height * height / Math.max(1, width) : width)
        readonly property real designH: !rotated ? height : (width > height ? height : width * width / Math.max(1, height))
        // The canvas keeps its design-size layout and is zoomed to fit what
        // the sheet leaves, so nothing reflows while it is open
        readonly property real zoom: Math.min(1, availW / Math.max(1, designW), availH / Math.max(1, designH))
        // A canvas rectangle in frame coordinates (toolbar, choosers)
        function toFrame(r) { return Qt.rect(editorCanvas.x + r.x * zoom, editorCanvas.y + r.y * zoom,
                                             r.width * zoom, r.height * zoom) }

        // The turned screen's outline
        Rectangle {
            visible: canvasFrame.rotated
            x: editorCanvas.x - 1
            y: editorCanvas.y - 1
            width: canvasFrame.designW * canvasFrame.zoom + 2
            height: canvasFrame.designH * canvasFrame.zoom + 2
            radius: editor.dpMin(10, 3)
            color: App.Style.backgroundColor
            border.color: Qt.darker(App.Style.obdBarColor, 1.3)
            border.width: 1
        }

        EditorCanvas {
            id: editorCanvas
            objectName: "editorCanvas"
            width: canvasFrame.designW
            height: canvasFrame.designH
            transformOrigin: Item.TopLeft
            scale: canvasFrame.zoom
            x: (canvasFrame.availW - width * scale) / 2
            y: (canvasFrame.availH - height * scale) / 2
            Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

            gridColumns: editor.gridColumns
            gridRows: editor.gridRows
            margins: editor.specMargins
            spacing: editor.specSpacing
            cells: editor.cells
            model: cellModel
            selectedIndex: editor.selectedIndex
            interactive: !editor._sheetShown && !editor.drivingLocked
            testValues: editor.testValues
            minSpanFor: function(index) {
                var c = editor.cells[index]
                return c ? editor.minSpanFor(c.type) : null
            }

            onEmptyCellTapped: function(col, row) {
                if (editor.drivingLocked) return
                if (editor._tapCell || editor._swapTarget >= 0 || editor._sheetShown) {
                    editor.clearSelection()
                    return
                }
                editor._swapTarget = -1
                editor._tapCell = { "col": col, "row": row }
            }
            onCellTapped: function(index) {
                if (editor.drivingLocked) return
                if (editor.selectedIndex >= 0 && index !== editor.selectedIndex && !editor._sheetShown) {
                    editor._tapCell = null
                    editor._swapTarget = editor._swapTarget === index ? -1 : index
                    return
                }
                editor.select(index)
            }
            onCellHeld: function(index) { if (!editor.drivingLocked) editor.select(index) }
            onCellMoveRequested: function(index, col, row) { editor.moveCell(index, col, row) }
            onCellResizeRequested: function(index, cs, rs) { editor.resizeCellTo(index, cs, rs) }
            onBackgroundTapped: editor.clearSelection()
            onDragActiveChanged: if (dragActive) editor._hideChoosers()
        }

        // Empty-state hint.
        Text {
            anchors.centerIn: parent
            width: parent.width * 0.8
            visible: editor.cells.length === 0 && !editor._tapCell
            text: "Tap anywhere on the grid, or + Add, to place your first widget"
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: App.Style.obdLabelColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText
            opacity: 0.7
        }

        // ── Free-space chooser: Add here / Move here ────────────────────
        Item {
            id: tapChooser
            objectName: "editorTapChooser"
            visible: editor._tapCell !== null && !editorCanvas.dragActive
            z: 30
            readonly property var _cell: editor._tapCell || { "col": 0, "row": 0 }
            readonly property rect _r: canvasFrame.toFrame(Qt.rect(editorCanvas._cellX(_cell.col), editorCanvas._cellY(_cell.row),
                                                                   editorCanvas._cellW, editorCanvas._cellH))
            anchors.fill: parent

            Rectangle {
                x: tapChooser._r.x
                y: tapChooser._r.y
                width: tapChooser._r.width
                height: tapChooser._r.height
                radius: editor.dp(6)
                color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.18)
                border.color: App.Style.accent
                border.width: editor.dp(2)
            }
            Row {
                id: tapButtons
                spacing: editor.dp(8)
                x: Math.max(editor.dp(6), Math.min(parent.width - width - editor.dp(6),
                            tapChooser._r.x + tapChooser._r.width / 2 - width / 2))
                y: {
                    var below = tapChooser._r.y + tapChooser._r.height + editor.dp(8)
                    return below + height <= parent.height - editor.dp(6)
                           ? below : Math.max(editor.dp(6), tapChooser._r.y - height - editor.dp(8))
                }
                Pill {
                    objectName: "editorAddHere"
                    text: "+ Add here"
                    primary: true
                    onClicked: editor.openPalette(tapChooser._cell.col, tapChooser._cell.row)
                }
                Pill {
                    objectName: "editorMoveHere"
                    visible: editor.selectedCell !== null
                    text: "Move here"
                    onClicked: {
                        var c = tapChooser._cell
                        editor._hideChoosers()
                        editor.moveCell(editor.selectedIndex, c.col, c.row)
                    }
                }
            }
        }

        // ── Tap-tap chooser on another widget: Swap / Select ────────────
        Row {
            id: swapChooser
            objectName: "editorSwapChooser"
            visible: editor._swapTarget >= 0 && editor._swapTarget < editor.cells.length && !editorCanvas.dragActive
            z: 30
            spacing: editor.dp(8)
            readonly property rect _r: canvasFrame.toFrame(editorCanvas.rectOf(editor.cells[editor._swapTarget]))
            x: Math.max(editor.dp(6), Math.min(parent.width - width - editor.dp(6),
                        _r.x + _r.width / 2 - width / 2))
            y: Math.max(editor.dp(6), Math.min(parent.height - height - editor.dp(6),
                        _r.y + _r.height / 2 - height / 2))
            Pill {
                objectName: "editorSwapButton"
                text: "Swap"
                primary: true
                onClicked: {
                    var a = editor.selectedIndex, b = editor._swapTarget
                    editor._hideChoosers()
                    editor.swapCells(a, b)
                }
            }
            Pill {
                objectName: "editorSelectButton"
                text: "Select"
                onClicked: editor.select(editor._swapTarget)
            }
        }

        // ── Floating toolbar for the selected widget ────────────────────
        // Above the widget when there is room, else below, else at the top
        // of the canvas; never over the resize grip.
        Rectangle {
            id: toolbar
            objectName: "editorToolbar"
            visible: editor.selectedCell !== null && !editor._sheetShown && !editorCanvas.dragActive
                     && editor._swapTarget < 0 && !editor._tapCell && !editor.drivingLocked
            z: 30
            readonly property rect _r: canvasFrame.toFrame(editorCanvas.selectedRect)
            readonly property real _gap: editor.touchSize / 2 + editor.dp(6)
            readonly property real _maxW: parent.width - editor.dp(12)
            width: Math.min(_maxW, toolFlow._natural + 2 * editor.dp(6))
            height: toolFlow.implicitHeight + 2 * editor.dp(6)
            radius: editor.dp(12)
            color: App.Style.obdBoxBackground
            border.color: Qt.darker(App.Style.obdBarColor, 1.4)
            border.width: 1
            x: Math.max(editor.dp(6), Math.min(parent.width - width - editor.dp(6),
                        _r.x + _r.width / 2 - width / 2))
            y: {
                if (_r.y - height - _gap >= editor.dp(6)) return _r.y - height - _gap
                if (_r.y + _r.height + _gap + height <= parent.height - editor.dp(6)) return _r.y + _r.height + _gap
                return editor.dp(6)
            }

            readonly property var _sizes: editor.selectedCell ? editor.sizeOptionsFor(editor.selectedCell.type) : []

            Flow {
                id: toolFlow
                anchors.fill: parent
                anchors.margins: editor.dp(6)
                spacing: editor.dp(6)
                // One row when it fits
                readonly property real _natural: {
                    var w = 0, n = 0
                    for (var i = 0; i < children.length; i++) {
                        var c = children[i]
                        if (!c.visible || c.implicitWidth <= 0) continue
                        w += c.implicitWidth
                        n++
                    }
                    return w + Math.max(0, n - 1) * spacing
                }

                Pill {
                    objectName: "editorToolData"
                    visible: editor.selectedMeta !== null && editor.selectedMeta.supportedKinds.length > 0
                    maxLabelWidth: editor.dp(160)
                    text: editor.selectedCell && editor.selectedCell.paramId
                          ? editor._pidTitle(editor.selectedCell.paramId) : "Choose data"
                    danger: !(editor.selectedCell && editor.selectedCell.paramId)
                    onClicked: editor.openPidPicker()
                }
                Pill {
                    objectName: "editorToolSettings"
                    text: "Settings"
                    onClicked: editor.openSheet()
                }
                Repeater {
                    model: toolbar._sizes
                    delegate: Pill {
                        objectName: "editorToolSize_" + modelData.label
                        text: modelData.label
                        checked: editor.selectedCell !== null
                                 && editor.selectedCell.colSpan === modelData.colSpan
                                 && editor.selectedCell.rowSpan === modelData.rowSpan
                        onClicked: editor.setCellSize(editor.selectedIndex, modelData.colSpan, modelData.rowSpan)
                    }
                }
                Pill {
                    id: styleButton
                    objectName: "editorToolStyle"
                    text: "Style"
                    onClicked: styleMenu.open()
                    Menu {
                        id: styleMenu
                        y: styleButton.height
                        MenuItem {
                            objectName: "editorStyleCopy"
                            implicitHeight: editor.touchSize
                            text: "Copy style"
                            font.family: App.Style.fontFamily
                            onTriggered: editor.copyStyle(editor.selectedIndex)
                        }
                        MenuItem {
                            objectName: "editorStylePaste"
                            implicitHeight: editor.touchSize
                            enabled: editor.copiedStyle !== null
                            text: "Paste style"
                            font.family: App.Style.fontFamily
                            onTriggered: editor.pasteStyle(editor.selectedIndex)
                        }
                        MenuItem {
                            objectName: "editorStyleApplyAll"
                            implicitHeight: editor.touchSize
                            enabled: editor.sameTypeCount > 0
                            text: editor.selectedMeta
                                  ? "Use this style on every " + editor.selectedMeta.displayName
                                    + (editor.sameTypeCount > 0 ? " (" + editor.sameTypeCount + " more)" : "")
                                  : "Use this style on all"
                            font.family: App.Style.fontFamily
                            onTriggered: editor.applyStyleToAll(editor.selectedIndex)
                        }
                        MenuItem {
                            objectName: "editorSaveAsPreset"
                            implicitHeight: editor.touchSize
                            text: "Save as my widget…"
                            font.family: App.Style.fontFamily
                            onTriggered: presetNameDialog.openFor(editor.selectedIndex)
                        }
                    }
                }
                Pill {
                    objectName: "editorToolDuplicate"
                    text: "Duplicate"
                    onClicked: editor.duplicateCell(editor.selectedIndex)
                }
                Pill {
                    objectName: "editorToolDelete"
                    text: "Delete"
                    danger: true
                    onClicked: editor.deleteCell(editor.selectedIndex)
                }
            }
        }

        // ── + Add: finds room by itself ─────────────────────────────────
        Pill {
            objectName: "editorAddButton"
            z: 25
            visible: !editor._sheetShown && !editorCanvas.dragActive && !editor.drivingLocked
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: editor.dp(12)
            text: "+ Add"
            primary: true
            onClicked: editor.openPalette(-1, -1)
        }

        // ── Polling cost ────────────────────────────────────────────────
        Text {
            objectName: "editorPollingStatus"
            z: 25
            visible: editor.polledCount > 0 && !editor._sheetShown
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: editor.dp(14)
            width: Math.min(implicitWidth, parent.width * 0.6)
            elide: Text.ElideRight
            text: editor.polledCount + (editor.polledCount === 1 ? " value" : " values")
                  + " from the car · about "
                  + editor.updatesPerValue.toFixed(editor.updatesPerValue < 10 ? 1 : 0)
                  + " updates a second each"
            color: editor.updatesPerValue < 1 ? App.Style.statusWarning : App.Style.obdLabelColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 0.8
        }

        // ── Driving lock banner ─────────────────────────────────────────
        Rectangle {
            objectName: "editorDrivingLock"
            visible: editor.drivingLocked
            z: 45
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: editor.dp(10)
            width: Math.min(parent.width - editor.dp(20), lockRow.implicitWidth + editor.dp(28))
            height: lockRow.implicitHeight + editor.dp(16)
            radius: editor.dpMin(10, 3)
            color: Qt.darker(App.Style.obdBoxBackground, 1.6)
            border.color: App.Style.statusWarning
            border.width: 1
            RowLayout {
                id: lockRow
                anchors.centerIn: parent
                width: parent.width - editor.dp(28)
                spacing: editor.dp(14)
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Editing is paused while the car is moving."
                    color: App.Style.primaryTextColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText
                }
                Pill {
                    objectName: "editorPassengerButton"
                    text: "I'm a passenger"
                    onClicked: editor.passengerOverride = true
                }
            }
        }

        // ── Settings sheet ──────────────────────────────────────────────
        PropertiesPanel {
            id: sidePanel
            objectName: "editorPanel"
            z: 40
            visible: editor._sheetShown
            x: canvasFrame.sheetSide ? canvasFrame.width - width : 0
            y: canvasFrame.sheetSide ? 0 : canvasFrame.height - height
            width: canvasFrame.sheetSide ? canvasFrame.sheetSize : canvasFrame.width
            height: canvasFrame.sheetSide ? canvasFrame.height : canvasFrame.sheetSize
            editorPage: editor
            cellIndex: editor.selectedIndex
            cell: editor.selectedCell
        }
    }

    // ── Popups ──────────────────────────────────────────────────────────
    PalettePopup {
        id: widgetPalette
        onWidgetPicked: function(type) {
            editor.addCellAt(type, editor._pendingCol, editor._pendingRow)
        }
        onDataFirstRequested: editor.openDataFirst()
        onPresetPicked: function(preset) {
            editor.addPreset(preset, editor._pendingCol, editor._pendingRow)
        }
    }

    PIDPicker {
        id: pidPicker
        onPidPicked: function(paramId) {
            if (editor._pidMode === "add")
                editor.addForParam(paramId)
            else if (editor._pidMode === "rule")
                optionsPopup.pickedParam(paramId)
            else if (editor.selectedIndex >= 0)
                editor.setCellParam(editor.selectedIndex, paramId)
        }
    }

    // ── Dashboard options: alert banner, show automatically when ... ────
    Popup {
        id: optionsPopup
        objectName: "editorOptionsPopup"
        parent: Overlay.overlay
        modal: true
        focus: true
        padding: editor.dp(20)
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent.width - editor.dp(24), editor.dp(560))

        // Being edited here; applied on Done
        property string paramId: ""
        property bool banner: true

        function pickedParam(id) { paramId = id }
        function _num(text) {
            var t = String(text).trim()
            if (t === "") return NaN
            var n = Number(t)
            return isFinite(n) ? n : NaN
        }
        function _shown(v) {
            if (v === undefined || !paramId) return ""
            return String(App.OBDParameterModel.toDisplay(paramId, v))
        }

        onAboutToShow: {
            var w = editor.showWhen
            paramId = w ? w.paramId : ""
            banner = editor.alertBanner
            aboveField.text = w ? _shown(w.above) : ""
            belowField.text = w ? _shown(w.below) : ""
        }

        function apply() {
            var a = _num(aboveField.text), b = _num(belowField.text)
            if (paramId === "" || (!isFinite(a) && !isFinite(b))) {
                editor.setShowWhen(null)
            } else {
                var w = { "paramId": paramId }
                if (isFinite(a)) w.above = App.OBDParameterModel.fromDisplay(paramId, a)
                if (isFinite(b)) w.below = App.OBDParameterModel.fromDisplay(paramId, b)
                editor.setShowWhen(w)
            }
            editor.setAlertBanner(banner)
            close()
        }

        background: Rectangle {
            color: App.Style.obdBoxBackground
            radius: editor.dpMin(12, 4)
            border.color: Qt.darker(App.Style.obdBarColor, 1.5)
            border.width: 1
        }

        component OptionLabel: Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: App.Style.obdLabelColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 0.85
        }

        component NumberField: TextField {
            Layout.preferredWidth: editor.dp(110)
            Layout.preferredHeight: editor.touchSize
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            color: App.Style.primaryTextColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText
            enabled: optionsPopup.paramId !== ""
            background: Rectangle {
                radius: editor.dpMin(6, 2)
                color: Qt.darker(App.Style.obdBoxBackground, 1.25)
                border.color: parent.activeFocus ? App.Style.accent : Qt.darker(App.Style.obdBarColor, 1.6)
                border.width: 1
            }
        }

        contentItem: ColumnLayout {
            spacing: editor.dp(12)
            Text {
                text: "Alerts and auto-show"
                color: App.Style.primaryTextColor
                font.family: App.Style.fontFamily
                font.pixelSize: App.Spacing.overallText * 1.2
                font.bold: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: editor.dp(10)
                OptionLabel {
                    text: "Alert banner: name any widget at its critical limit in a red strip at the top"
                }
                Pill {
                    objectName: "editorOptionBanner"
                    text: optionsPopup.banner ? "On" : "Off"
                    checked: optionsPopup.banner
                    onClicked: optionsPopup.banner = !optionsPopup.banner
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Qt.darker(App.Style.obdBarColor, 1.6) }

            OptionLabel {
                text: "Show this dashboard by itself when a value passes a limit (on the OBD page, with a car "
                      + "connected). The one you were on comes back when it stops."
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: editor.dp(8)
                Pill {
                    objectName: "editorOptionRulePid"
                    maxLabelWidth: editor.dp(220)
                    text: optionsPopup.paramId ? editor._pidTitle(optionsPopup.paramId) : "Choose a value"
                    onClicked: {
                        editor._pidMode = "rule"
                        pidPicker.title = "Show this dashboard when ..."
                        pidPicker.supportedKinds = ["*"]
                        pidPicker.currentParamId = optionsPopup.paramId
                        pidPicker.open()
                    }
                }
                Pill {
                    objectName: "editorOptionRuleOff"
                    visible: optionsPopup.paramId !== ""
                    text: "Off"
                    onClicked: { optionsPopup.paramId = ""; aboveField.text = ""; belowField.text = "" }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: editor.dp(8)
                OptionLabel { Layout.fillWidth: false; text: "at or above" }
                NumberField { id: aboveField; objectName: "editorOptionAbove" }
                OptionLabel { Layout.fillWidth: false; text: "or at or below" }
                NumberField { id: belowField; objectName: "editorOptionBelow" }
                OptionLabel {
                    text: optionsPopup.paramId ? App.OBDParameterModel.getParamInfo(optionsPopup.paramId).unit || "" : ""
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: editor.dp(8)
                layoutDirection: Qt.RightToLeft
                Pill {
                    objectName: "editorOptionsDone"
                    text: "Done"
                    primary: true
                    onClicked: optionsPopup.apply()
                }
                Pill {
                    text: "Cancel"
                    onClicked: optionsPopup.close()
                }
            }
        }
    }

    // "Save as my widget": a name for it
    Popup {
        id: presetNameDialog
        objectName: "editorPresetDialog"
        parent: Overlay.overlay
        modal: true
        focus: true
        padding: editor.dp(20)
        x: Math.round((parent.width - width) / 2)
        y: Math.round(Math.max(editor.dp(12), parent.height * 0.2))
        width: Math.min(parent.width - editor.dp(24), editor.dp(520))
        property int index: -1
        function openFor(i) {
            var c = editor.cells[i]
            if (!c) return
            index = i
            var meta = App.WidgetCatalog.metaFor(c.type)
            presetName.text = (c.props && c.props.title) || (c.paramId ? editor._pidTitle(c.paramId) + " " : "")
                              + (meta ? meta.displayName : c.type)
            open()
        }
        background: Rectangle {
            color: App.Style.obdBoxBackground
            radius: editor.dpMin(12, 4)
            border.color: Qt.darker(App.Style.obdBarColor, 1.5)
            border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: editor.dp(12)
            Text {
                Layout.fillWidth: true
                text: "Save as my widget"
                color: App.Style.primaryTextColor
                font.family: App.Style.fontFamily
                font.pixelSize: App.Spacing.overallText * 1.2
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Its type, value and options, ready to place again from Add (Mine) and to share as text."
                color: App.Style.obdLabelColor
                font.family: App.Style.fontFamily
                font.pixelSize: App.Spacing.overallText * 0.85
            }
            TextField {
                id: presetName
                objectName: "editorPresetName"
                Layout.fillWidth: true
                Layout.preferredHeight: editor.touchSize
                maximumLength: 80
                color: App.Style.primaryTextColor
                font.family: App.Style.fontFamily
                font.pixelSize: App.Spacing.overallText
                background: Rectangle {
                    radius: editor.dpMin(6, 2)
                    color: Qt.darker(App.Style.obdBoxBackground, 1.25)
                    border.color: presetName.activeFocus ? App.Style.accent : Qt.darker(App.Style.obdBarColor, 1.6)
                    border.width: 1
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: editor.dp(8)
                layoutDirection: Qt.RightToLeft
                Pill {
                    objectName: "editorPresetSave"
                    text: "Save"
                    primary: true
                    onClicked: {
                        if (editor.saveAsPreset(presetNameDialog.index, presetName.text) !== "")
                            presetNameDialog.close()
                    }
                }
                Pill {
                    text: "Cancel"
                    onClicked: presetNameDialog.close()
                }
            }
        }
    }

    // Close with unsaved changes
    Popup {
        id: closeDialog
        objectName: "editorCloseDialog"
        parent: Overlay.overlay
        modal: true
        focus: true
        padding: editor.dp(20)
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent.width - editor.dp(32), editor.dp(460))
        background: Rectangle {
            color: App.Style.obdBoxBackground
            radius: editor.dpMin(12, 4)
            border.color: Qt.darker(App.Style.obdBarColor, 1.5)
            border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: editor.dp(16)
            Text {
                Layout.fillWidth: true
                text: "Save your changes?"
                color: App.Style.primaryTextColor
                font.family: App.Style.fontFamily
                font.pixelSize: App.Spacing.overallText * 1.2
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Flow {
                Layout.fillWidth: true
                spacing: editor.dp(8)
                layoutDirection: Qt.RightToLeft
                Pill {
                    objectName: "editorCloseSave"
                    text: "Save"
                    primary: true
                    onClicked: { closeDialog.close(); editor.save() }
                }
                Pill {
                    objectName: "editorCloseDiscard"
                    text: "Discard"
                    danger: true
                    onClicked: { closeDialog.close(); editor.discard() }
                }
                Pill {
                    objectName: "editorCloseKeep"
                    text: "Keep editing"
                    onClicked: closeDialog.close()
                }
            }
        }
    }

    // ── Snackbar: errors, and "removed · Undo" ──────────────────────────
    Rectangle {
        id: snackbar
        objectName: "editorErrorBanner"
        visible: false
        z: 50
        property bool danger: false
        property string actionText: ""
        property var action: null
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: dp(24)
        width: Math.min(parent.width - dp(32), snackRow.implicitWidth + dp(32))
        height: Math.max(editor.touchSize, snackRow.implicitHeight + dp(16))
        radius: dpMin(10, 3)
        color: danger ? App.Style.statusDanger : Qt.darker(App.Style.obdBoxBackground, 1.6)
        border.color: Qt.darker(App.Style.obdBarColor, 1.4)
        border.width: danger ? 0 : 1

        RowLayout {
            id: snackRow
            anchors.centerIn: parent
            width: parent.width - dp(32)
            spacing: dp(16)
            Text {
                id: snackText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "white"
                font.family: App.Style.fontFamily
                font.pixelSize: App.Spacing.overallText
            }
            Pill {
                objectName: "editorSnackAction"
                visible: snackbar.actionText.length > 0
                text: snackbar.actionText
                primary: true
                onClicked: {
                    var act = snackbar.action
                    snackbar.visible = false
                    if (act) act()
                }
            }
        }

        Timer {
            id: snackTimer
            interval: 3000
            onTriggered: snackbar.visible = false
        }
    }
}
