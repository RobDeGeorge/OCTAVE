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
// This page owns the working spec. All mutations reassign `cells` wholesale
// (never in place) so the canvas Repeater rebuilds and every binding stays
// honest. The backend (DashboardManager, C++ + Python) already provides
// save/delete/duplicate with full parity, so this page is pure QML.
// saveDashboard() requires a unique `id`; built-in ids are rejected and user
// ids overwrite, so we slugify+uniquify the label here (mirrors the manager's
// internal logic) rather than adding a backend slot.

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

    // Working cell list. Reassign the whole array (never mutate in place) so
    // the canvas Repeater rebuilds.
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

    // Cell the next palette pick lands in (set by emptyCellTapped).
    property int _pendingCol: 0
    property int _pendingRow: 0

    Component.onCompleted: {
        if (draftJson.length > 0 && _restoreDraft(draftJson)) return
        if (editingId.length === 0) return
        if (typeof dashboardManager === "undefined" || !dashboardManager) return
        var spec = dashboardManager.loadDashboard(editingId)
        if (!spec || !spec.cells) {
            _showError("Could not load dashboard \"" + editingId + "\"")
            return
        }
        gridColumns = spec.gridColumns ? spec.gridColumns : 12
        gridRows = spec.gridRows ? spec.gridRows : 6
        specMargins = spec.margins
        specSpacing = spec.spacing
        cells = _sanitizeCells(spec.cells)
        nameField.text = spec.label ? spec.label : ""
    }

    // ── Draft rescue ─────────────────────────────────────────────────────
    // Switching menus mid-build destroys this page (BottomBar pops the whole
    // StackView), so unsaved work is stashed as a draft on destruction and
    // offered back via "Resume draft" in the chooser. Normal exits (Save,
    // confirmed Discard) clear `dirty` first, so they don't leave a draft.
    Component.onDestruction: {
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
            editingId = draft.editingId ? draft.editingId : ""
            gridColumns = draft.gridColumns ? draft.gridColumns : 12
            gridRows = draft.gridRows ? draft.gridRows : 6
            specMargins = draft.margins
            specSpacing = draft.spacing
            cells = _sanitizeCells(draft.cells || [])
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

    // Clamp loaded cells into the grid and drop unknown widget types, so a
    // hand-edited or older JSON file can't put the canvas math out of bounds.
    // Editor mutations already clamp; this covers what arrives from disk.
    function _sanitizeCells(rawCells) {
        var out = []
        for (var i = 0; i < rawCells.length; i++) {
            var c = rawCells[i]
            if (!c || !App.WidgetCatalog.isKnownType(c.type)) {
                console.warn("DashboardEditor: dropping cell with unknown type:",
                             c ? c.type : "<null>")
                continue
            }
            var col = Math.max(0, Math.min(gridColumns - 1, c.col !== undefined ? c.col : 0))
            var row = Math.max(0, Math.min(gridRows - 1, c.row !== undefined ? c.row : 0))
            var cs = Math.max(1, Math.min(gridColumns - col, c.colSpan !== undefined ? c.colSpan : 1))
            var rs = Math.max(1, Math.min(gridRows - row, c.rowSpan !== undefined ? c.rowSpan : 1))
            var pid = c.paramId !== undefined ? c.paramId : ""
            if (pid !== "" && !App.OBDParameterModel.hasParameter(pid)) {
                console.warn("DashboardEditor: dropping unknown paramId:", pid)
                pid = ""
            }
            out.push({
                "type": c.type,
                "paramId": pid,
                "col": col, "row": row,
                "colSpan": cs, "rowSpan": rs,
                "props": c.props || {}
            })
        }
        return out
    }

    // ── Cell mutations (single owner of the working spec) ───────────────

    // Replace cells[index] with a shallow-merged copy. Reassigns the array.
    function _patchCell(index, patch) {
        var next = cells.slice()
        var merged = {}
        for (var k in next[index]) merged[k] = next[index][k]
        for (var p in patch) merged[p] = patch[p]
        next[index] = merged
        cells = next
        dirty = true
    }

    function addCellAt(type, col, row) {
        var meta = App.WidgetCatalog.metaFor(type)
        if (!meta) return
        var cs = Math.min(meta.defaultColSpan, gridColumns - col)
        var rs = Math.min(meta.defaultRowSpan, gridRows - row)
        // Shrink toward 1×1 until the widget fits the free space around the
        // tapped cell (the tapped cell itself is guaranteed free).
        while (!editorCanvas.canPlace(col, row, cs, rs, -1) && (cs > 1 || rs > 1)) {
            if (cs >= rs && cs > 1) cs--
            else rs--
        }
        var next = cells.slice()
        next.push({
            "type": type, "paramId": "",
            "col": col, "row": row,
            "colSpan": cs, "rowSpan": rs,
            "props": {}
        })
        cells = next
        dirty = true
        selectedIndex = next.length - 1
        // A gauge without a PID is useless — pick one now. Self-binding
        // widgets (supportedKinds: []) skip this.
        if (meta.supportedKinds.length > 0)
            openPidPicker()
    }

    function moveCell(index, col, row) {
        var c = cells[index]
        if (!c) return
        var cs = c.colSpan !== undefined ? c.colSpan : 1
        var rs = c.rowSpan !== undefined ? c.rowSpan : 1
        col = Math.max(0, Math.min(gridColumns - cs, col))
        row = Math.max(0, Math.min(gridRows - rs, row))
        if (editorCanvas.canPlace(col, row, cs, rs, index)) {
            _patchCell(index, { "col": col, "row": row })
        } else {
            // Rejected drop: reassign anyway so the canvas rebuilds and the
            // dragged delegate snaps back to its origin.
            cells = cells.slice()
        }
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
        if (cs === c.colSpan && rs === c.rowSpan) return
        if (editorCanvas.canPlace(col, row, cs, rs, index))
            _patchCell(index, { "colSpan": cs, "rowSpan": rs })
    }

    function removeCell(index) {
        var next = cells.slice()
        next.splice(index, 1)
        cells = next
        dirty = true
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

    // ── id generation (mirrors DashboardManager::_slugify / _uniqueId) ──
    function _slugify(s) {
        var x = (s || "").toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "")
        return x.length > 0 ? x : "dashboard"
    }

    function _uniqueId(label) {
        var base = _slugify(label)
        var taken = { "grid": true }       // reserve the parameter-cards sentinel
        if (typeof dashboardManager !== "undefined" && dashboardManager) {
            var list = dashboardManager.dashboards
            for (var i = 0; i < list.length; i++)
                taken[list[i].id] = true
        }
        if (!taken[base]) return base
        var n = 2
        while (taken[base + "-" + n]) n++
        return base + "-" + n
    }

    function save() {
        if (typeof dashboardManager === "undefined" || !dashboardManager) {
            _showError("No dashboard backend available")
            return
        }
        var label = nameField.text.trim()
        if (label.length === 0) label = "Untitled"

        var id = editingId.length > 0 ? editingId : _uniqueId(label)
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
            _showError("Save failed (check the id isn't a built-in)")
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

            TextField {
                id: nameField
                objectName: "editorNameField"
                Layout.fillWidth: true
                placeholderText: "Dashboard name"
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
            selectedIndex: editor.selectedIndex

            onEmptyCellTapped: function(col, row) {
                editor._pendingCol = col
                editor._pendingRow = row
                palette.open()
            }
            onCellTapped: function(index) { editor.selectedIndex = index }
            onCellMoveRequested: function(index, col, row) { editor.moveCell(index, col, row) }
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
