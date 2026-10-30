// DashboardEditor.qml
//
// Phase 3 "create-a-park" dashboard editor — full-screen page, StackView-pushed
// from the chooser in OBDMenu.qml. See TODO/dashboards-roadmap.md.
//
// Interaction model (hybrid, decided 2026-06-10): tap an empty grid cell to
// place a widget from the palette; drag a placed widget to move it (snaps to
// the grid, invalid drops bounce back); tap a widget to select it and edit
// PID / position / size / curated props in the side panel.
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
import "../.." as App

Item {
    id: editor
    objectName: "dashboardEditor"

    // Local dp/dpMin wrappers — mirror OBDMenu's Android singleton-function bug
    // workaround so this page scales identically to the one that pushes it.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    // Injected by StackView.push() from OBDMenu.qml.
    property StackView stackView: null
    property var callerPage: null          // OBDMenu page — for setActiveDashboard after save

    // Non-empty when editing an existing user dashboard (keeps its id on save).
    // Empty = creating a new dashboard (id generated from the label).
    property string editingId: ""

    // Grid resolution of the dashboard being edited.
    property int gridColumns: 12
    property int gridRows: 6

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

    // Unsaved-changes flag — gates the two-tap "Discard?" on Cancel and the
    // draft rescue on destruction.
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

    // Cell the next palette pick lands in (set by emptyCellTapped).
    property int _pendingCol: 0
    property int _pendingRow: 0

    Component.onCompleted: {
        _load()
        _resetHistory()
    }

    function _load() {
        if (draftJson.length > 0 && _restoreDraft(draftJson)) return
        if (editingId.length === 0) return
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
        // Already normalised by the manager: in the grid, no overlaps
        gridColumns = spec.gridColumns
        gridRows = spec.gridRows
        specMargins = spec.margins
        specSpacing = spec.spacing
        _show(spec.cells)
        nameField.text = spec.label ? spec.label : ""
    }

    // ── Draft rescue ─────────────────────────────────────────────────────
    // Switching menus mid-build destroys this page (BottomBar pops the whole
    // StackView), so unsaved work is stashed as a draft on destruction and
    // offered back via "Resume draft" in the chooser. Normal exits (Save,
    // confirmed Discard) clear `dirty` first, so they don't leave a draft.
    Component.onDestruction: {
        _pushDemand([])
        // Never let editor demo data leak into the real OBD views.
        App.OBDParameterModel.simulationActive = false
        if (!dirty) return
        if (typeof settingsManager === "undefined" || !settingsManager) return
        settingsManager.save_setting("dashboardEditorDraft", JSON.stringify(_withGridGeometry({
            "editingId": editingId,
            "label": nameField.text,
            "gridColumns": gridColumns,
            "gridRows": gridRows,
            "cells": cells
        })))
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
                "gridColumns": draft.gridColumns, "gridRows": draft.gridRows,
                "margins": draft.margins, "spacing": draft.spacing,
                "cells": draft.cells || []
            })
            if (!spec.cells) return false
            editingId = draft.editingId ? draft.editingId : ""
            gridColumns = spec.gridColumns
            gridRows = spec.gridRows
            specMargins = spec.margins
            specSpacing = spec.spacing
            _show(spec.cells)
            nameField.text = draft.label ? draft.label : ""
            dirty = true
            return true
        } catch (e) {
            console.warn("DashboardEditor: could not restore draft:", e)
            return false
        }
    }

    // Clear the stored draft — only when this session owned one (resumed it),
    // so opening and immediately cancelling a fresh editor can't wipe a draft
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
    // Snapshots of the cell list (JSON), newest last; _historyIndex is the
    // one on screen. A new edit drops anything after it.
    readonly property int maxUndo: 50
    property var _history: []
    property int _historyIndex: -1
    readonly property bool canUndo: _historyIndex > 0
    readonly property bool canRedo: _historyIndex >= 0 && _historyIndex < _history.length - 1

    function _resetHistory() {
        _history = [JSON.stringify(cells)]
        _historyIndex = 0
    }

    // Every edit lands here: show it, mark the work unsaved, record it
    function _apply(next) {
        _show(next)
        dirty = true
        var h = _history.slice(0, _historyIndex + 1)
        h.push(JSON.stringify(next))
        while (h.length > maxUndo + 1) h.shift()
        _history = h
        _historyIndex = h.length - 1
    }

    function _restore(index) {
        _historyIndex = index
        _show(JSON.parse(_history[index]))
        dirty = true
        if (selectedIndex >= cells.length) selectedIndex = -1
    }

    function undo() { if (canUndo) _restore(_historyIndex - 1) }
    function redo() { if (canRedo) _restore(_historyIndex + 1) }

    Shortcut { sequences: [StandardKey.Undo]; onActivated: editor.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; onActivated: editor.redo() }

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

    function addCellAt(type, col, row) {
        var meta = App.WidgetCatalog.metaFor(type)
        if (!meta) return
        // Shrunk toward 1×1 until it fits the free space around the tapped
        // cell (the tapped cell itself is guaranteed free)
        var fit = dashboardManager.fitAt(cells, gridColumns, gridRows, col, row,
                                         meta.defaultColSpan, meta.defaultRowSpan)
        var cs = fit.colSpan
        var rs = fit.rowSpan
        var next = cells.slice()
        next.push({
            "type": type, "paramId": "",
            "col": col, "row": row,
            "colSpan": cs, "rowSpan": rs,
            "props": {}
        })
        _apply(next)
        selectedIndex = next.length - 1
        // A gauge without a PID is useless — pick one now. Self-binding
        // widgets (supportedKinds: []) skip this.
        if (meta.supportedKinds.length > 0)
            openPidPicker()
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

    function resizeCell(index, dColSpan, dRowSpan) {
        var c = cells[index]
        if (!c) return
        var col = c.col !== undefined ? c.col : 0
        var row = c.row !== undefined ? c.row : 0
        var cs = Math.max(1, Math.min(gridColumns - col, (c.colSpan !== undefined ? c.colSpan : 1) + dColSpan))
        var rs = Math.max(1, Math.min(gridRows - row, (c.rowSpan !== undefined ? c.rowSpan : 1) + dRowSpan))
        _arrangeCell(index, col, row, cs, rs)
    }

    // Absolute resize (drag handles), by the same rules as the steppers
    function resizeCellTo(index, colSpan, rowSpan) {
        var c = cells[index]
        if (!c) return
        var col = c.col !== undefined ? c.col : 0
        var row = c.row !== undefined ? c.row : 0
        var cs = Math.max(1, Math.min(gridColumns - col, colSpan))
        var rs = Math.max(1, Math.min(gridRows - row, rowSpan))
        _arrangeCell(index, col, row, cs, rs)
    }

    function removeCell(index) {
        var next = cells.slice()
        next.splice(index, 1)
        _apply(next)
        selectedIndex = -1
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

    function clearSelection() { selectedIndex = -1 }

    function openPidPicker() {
        if (!selectedCell) return
        var meta = App.WidgetCatalog.metaFor(selectedCell.type)
        pidPicker.supportedKinds = meta ? meta.supportedKinds : ["*"]
        pidPicker.currentParamId = selectedCell.paramId ? selectedCell.paramId : ""
        pidPicker.open()
    }

    function save() {
        if (typeof dashboardManager === "undefined" || !dashboardManager) {
            _showError("No dashboard backend available")
            return
        }
        var label = nameField.text.trim().replace(/\s+/g, " ")   // as the manager stores it
        if (label.length === 0) label = "Untitled"

        var id = editingId.length > 0 ? editingId : dashboardManager.uniqueIdForLabel(label)
        var spec = _withGridGeometry({
            "schema": 1,
            "id": id,
            "label": label,
            "gridColumns": gridColumns,
            "gridRows": gridRows,
            "cells": cells
        })

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

    // Two-tap discard guard: first tap arms "Discard?", second tap (within
    // 3 s) actually leaves. Editing in a car — accidental data loss hurts.
    property bool confirmingCancel: false
    Timer {
        id: cancelConfirmTimer
        interval: 3000
        onTriggered: editor.confirmingCancel = false
    }

    function cancel() {
        if (dirty && !confirmingCancel) {
            confirmingCancel = true
            cancelConfirmTimer.restart()
            return
        }
        dirty = false              // confirmed discard — don't rescue a draft
        _clearStoredDraft()
        if (stackView) stackView.pop()
    }

    function _showError(msg) {
        errorText.text = msg
        errorBanner.visible = true
        errorTimer.restart()
    }

    // No anchors here on purpose: StackView sizes pushed items itself and
    // warns ("conflicting anchors") if the page anchors to its parent.

    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor
    }

    // ── Top bar ─────────────────────────────────────────────────────────
    Rectangle {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: App.Spacing.bottomBarNavButtonHeight + dp(20)
        color: App.Style.obdBoxBackground

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: dp(16)
            anchors.rightMargin: dp(16)
            spacing: dp(12)

            Button {
                objectName: "editorCancelButton"
                text: editor.confirmingCancel ? "Discard?" : "Cancel"
                font.family: App.Style.fontFamily
                onClicked: editor.cancel()
            }

            Button {
                objectName: "editorUndoButton"
                text: "Undo"
                enabled: editor.canUndo
                font.family: App.Style.fontFamily
                onClicked: editor.undo()
            }

            Button {
                objectName: "editorRedoButton"
                text: "Redo"
                enabled: editor.canRedo
                font.family: App.Style.fontFamily
                onClicked: editor.redo()
            }

            TextField {
                id: nameField
                objectName: "editorNameField"
                Layout.fillWidth: true
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
            }

            // Fake live values for every PID so widgets can be laid out and
            // sized without a real OBD connection. Editor-scoped: force-disabled
            // when this page is destroyed.
            Button {
                objectName: "editorDemoButton"
                text: checked ? "Demo data: ON" : "Demo data"
                checkable: true
                checked: App.OBDParameterModel.simulationActive
                font.family: App.Style.fontFamily
                onToggled: App.OBDParameterModel.simulationActive = checked
            }

            Button {
                objectName: "editorSaveButton"
                text: "Save"
                font.family: App.Style.fontFamily
                enabled: nameField.text.trim().length > 0
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
        anchors.margins: dp(16)
        color: App.Style.obdBoxBackground
        radius: dpMin(10, 3)
        border.color: Qt.darker(App.Style.obdBarColor, 1.6)
        border.width: 1
        clip: true

        EditorCanvas {
            id: editorCanvas
            objectName: "editorCanvas"
            anchors.fill: parent
            gridColumns: editor.gridColumns
            gridRows: editor.gridRows
            margins: editor.specMargins
            spacing: editor.specSpacing
            cells: editor.cells
            model: cellModel
            selectedIndex: editor.selectedIndex

            onEmptyCellTapped: function(col, row) {
                editor._pendingCol = col
                editor._pendingRow = row
                palette.open()
            }
            onCellTapped: function(index) { editor.selectedIndex = index }
            onCellMoveRequested: function(index, col, row) { editor.moveCell(index, col, row) }
            onCellResizeRequested: function(index, cs, rs) { editor.resizeCellTo(index, cs, rs) }
            onBackgroundTapped: editor.clearSelection()
        }

        // Empty-state hint.
        Text {
            anchors.centerIn: parent
            visible: editor.cells.length === 0
            text: "Tap any  +  cell to place your first widget"
            horizontalAlignment: Text.AlignHCenter
            color: App.Style.obdLabelColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText
            opacity: 0.7
        }
    }

    // ── Properties panel ────────────────────────────────────────────────
    // Floats OVER the canvas rather than docking beside it — a docked panel
    // resized the canvas every time the selection changed, which rescaled
    // the whole dashboard mid-edit and made drag snapping unstable. It docks
    // to whichever side is away from the selected widget so the widget being
    // configured stays visible.
    PropertiesPanel {
        id: sidePanel
        objectName: "editorPanel"
        parent: canvasFrame
        z: 20
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: dp(10)
        anchors.bottomMargin: dp(10)
        width: dp(300)
        visible: editor.selectedCell !== null
        editorPage: editor
        cellIndex: editor.selectedIndex
        cell: editor.selectedCell

        // Dock right when the selected widget sits in the left half of the
        // grid, and vice versa.
        readonly property bool dockRight: {
            var c = editor.selectedCell
            if (!c) return true
            var center = (c.col !== undefined ? c.col : 0)
                       + (c.colSpan !== undefined ? c.colSpan : 1) / 2
            return center < editor.gridColumns / 2
        }
        x: dockRight ? parent.width - width - dp(10) : dp(10)
        Behavior on x {
            enabled: sidePanel.visible
            NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
        }
    }

    // ── Popups ──────────────────────────────────────────────────────────
    PalettePopup {
        id: palette
        onWidgetPicked: function(type) {
            editor.addCellAt(type, editor._pendingCol, editor._pendingRow)
        }
    }

    PIDPicker {
        id: pidPicker
        onPidPicked: function(paramId) {
            if (editor.selectedIndex >= 0)
                editor.setCellParam(editor.selectedIndex, paramId)
        }
    }

    // ── Error banner ────────────────────────────────────────────────────
    Rectangle {
        id: errorBanner
        objectName: "editorErrorBanner"
        visible: false
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: dp(24)
        width: errorText.implicitWidth + dp(32)
        height: errorText.implicitHeight + dp(20)
        radius: dpMin(8, 2)
        color: App.Style.statusDanger

        Text {
            id: errorText
            anchors.centerIn: parent
            color: "white"
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText
        }

        Timer {
            id: errorTimer
            interval: 3000
            onTriggered: errorBanner.visible = false
        }
    }
}
