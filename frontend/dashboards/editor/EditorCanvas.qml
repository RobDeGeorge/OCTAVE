// EditorCanvas.qml
//
// Interactive grid canvas for the dashboard editor (Phase 3 Milestone B).
// Renders the working spec's cells as live widgets and layers the editing
// affordances on top:
//
//   - free grid cells show a [+] tap target        → emptyCellTapped(col, row)
//   - tapping a placed widget selects it           → cellTapped(index)
//   - dragging a placed widget moves it, snapping
//     to the grid on release                       → cellMoveRequested(index, col, row)
//   - tapping the background clears the selection  → backgroundTapped()
//
// The canvas never mutates `cells` itself — DashboardEditor owns the working
// spec and always reassigns the whole array, which rebuilds the delegates here
// (and snaps a rejected drag back to its origin for free, since the dragged
// delegate's broken x/y bindings die with it).
//
// Grid geometry mirrors DashboardRenderer's defaults (20dp margin, 16dp
// spacing) so the layout you edit is the layout that renders.

import QtQuick 2.15
import "../.." as App

Item {
    id: canvas

    // Local dp wrapper — mirrors DashboardEditor's Android singleton-function
    // bug workaround.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    property int gridColumns: 12
    property int gridRows: 6
    property var cells: []
    property int selectedIndex: -1

    signal emptyCellTapped(int col, int row)
    signal cellTapped(int index)
    signal cellMoveRequested(int index, int col, int row)
    signal backgroundTapped()

    // ── Grid geometry (matches DashboardRenderer defaults) ──────────────
    // Optional per-spec overrides (dp, same keys as the JSON spec); undefined
    // keeps the renderer defaults.
    property var margins: undefined
    property var spacing: undefined
    readonly property real _margin:
        (margins !== undefined && margins !== null) ? dp(margins) : dp(20)
    readonly property real _spacing:
        (spacing !== undefined && spacing !== null) ? dp(spacing) : dp(16)
    readonly property real _cellW: Math.max(1,
        (width  - 2 * _margin - _spacing * (gridColumns - 1)) / gridColumns)
    readonly property real _cellH: Math.max(1,
        (height - 2 * _margin - _spacing * (gridRows - 1)) / gridRows)

    function _cellX(col) { return _margin + col * (_cellW + _spacing) }
    function _cellY(row) { return _margin + row * (_cellH + _spacing) }
    function _spanW(colSpan) { return colSpan * _cellW + (colSpan - 1) * _spacing }
    function _spanH(rowSpan) { return rowSpan * _cellH + (rowSpan - 1) * _spacing }

    // True if a cell of the given span fits at (col, row) without leaving the
    // grid or overlapping any placed cell (other than `exceptIndex`, used
    // while moving/resizing an existing cell).
    function canPlace(col, row, colSpan, rowSpan, exceptIndex) {
        if (col < 0 || row < 0) return false
        if (col + colSpan > gridColumns || row + rowSpan > gridRows) return false
        for (var i = 0; i < cells.length; i++) {
            if (i === exceptIndex) continue
            var c = cells[i]
            var cc = c.col     !== undefined ? c.col     : 0
            var cr = c.row     !== undefined ? c.row     : 0
            var cs = c.colSpan !== undefined ? c.colSpan : 1
            var rs = c.rowSpan !== undefined ? c.rowSpan : 1
            if (col < cc + cs && cc < col + colSpan &&
                row < cr + rs && cr < row + rowSpan)
                return false
        }
        return true
    }

    // Occupancy map: index (row * gridColumns + col) → true when covered by a
    // placed cell. Drives the [+] targets. Recomputed whenever `cells` is
    // reassigned (DashboardEditor never mutates in place).
    readonly property var _occupied: {
        var occ = []
        for (var i = 0; i < gridColumns * gridRows; i++) occ.push(false)
        for (var n = 0; n < cells.length; n++) {
            var c = cells[n]
            var cc = c.col     !== undefined ? c.col     : 0
            var cr = c.row     !== undefined ? c.row     : 0
            var cs = c.colSpan !== undefined ? c.colSpan : 1
            var rs = c.rowSpan !== undefined ? c.rowSpan : 1
            for (var r = cr; r < Math.min(cr + rs, gridRows); r++)
                for (var col = cc; col < Math.min(cc + cs, gridColumns); col++)
                    occ[r * gridColumns + col] = true
        }
        return occ
    }

    // ── Drag preview state (written by the dragging delegate) ──────────
    property bool dragActive: false
    property int dragCol: 0
    property int dragRow: 0
    property int dragColSpan: 1
    property int dragRowSpan: 1
    property bool dragValid: false

    function _coerce(v) {
        if (v === "NaN") return NaN
        return v
    }

    // ── Background tap = deselect ───────────────────────────────────────
    MouseArea {
        anchors.fill: parent
        onClicked: canvas.backgroundTapped()
    }

    // ── Free-cell [+] targets ───────────────────────────────────────────
    Repeater {
        model: canvas.gridColumns * canvas.gridRows
        delegate: Rectangle {
            readonly property int _c: index % canvas.gridColumns
            readonly property int _r: Math.floor(index / canvas.gridColumns)
            objectName: "editorEmptyCell_" + _c + "_" + _r

            visible: !canvas._occupied[index]
            x: canvas._cellX(_c)
            y: canvas._cellY(_r)
            width: canvas._cellW
            height: canvas._cellH
            radius: canvas.dp(4)
            color: plusMouse.pressed
                   ? Qt.lighter(App.Style.obdBoxBackground, 1.25)
                   : "transparent"
            border.color: Qt.darker(App.Style.obdBarColor, 1.8)
            border.width: 1
            opacity: canvas.dragActive ? 0.25 : 0.55

            Text {
                anchors.centerIn: parent
                text: "+"
                color: App.Style.obdLabelColor
                font.family: App.Style.fontFamily
                font.pixelSize: Math.max(8, Math.min(parent.width, parent.height) * 0.5)
            }

            MouseArea {
                id: plusMouse
                anchors.fill: parent
                enabled: !canvas.dragActive
                onClicked: canvas.emptyCellTapped(parent._c, parent._r)
            }
        }
    }

    // ── Drop-target highlight while dragging ────────────────────────────
    Rectangle {
        visible: canvas.dragActive
        x: canvas._cellX(canvas.dragCol)
        y: canvas._cellY(canvas.dragRow)
        width: canvas._spanW(canvas.dragColSpan)
        height: canvas._spanH(canvas.dragRowSpan)
        radius: canvas.dp(4)
        color: canvas.dragValid
               ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.18)
               : Qt.rgba(App.Style.statusDanger.r, App.Style.statusDanger.g, App.Style.statusDanger.b, 0.18)
        border.color: canvas.dragValid ? App.Style.accent : App.Style.statusDanger
        border.width: 2
    }

    // ── Placed widgets ──────────────────────────────────────────────────
    Repeater {
        model: canvas.cells
        delegate: Item {
            id: cellRoot
            objectName: "editorCell_" + index

            readonly property int cellIndex: index
            readonly property int _c:  modelData.col     !== undefined ? modelData.col     : 0
            readonly property int _r:  modelData.row     !== undefined ? modelData.row     : 0
            readonly property int _cs: modelData.colSpan !== undefined ? modelData.colSpan : 1
            readonly property int _rs: modelData.rowSpan !== undefined ? modelData.rowSpan : 1
            readonly property bool selected: canvas.selectedIndex === cellIndex

            x: canvas._cellX(_c)
            y: canvas._cellY(_r)
            width: canvas._spanW(_cs)
            height: canvas._spanH(_rs)
            z: dragArea.drag.active ? 10 : 1
            opacity: dragArea.drag.active ? 0.85 : 1.0

            function _updateDragCandidate() {
                var col = Math.round((x - canvas._margin) / (canvas._cellW + canvas._spacing))
                var row = Math.round((y - canvas._margin) / (canvas._cellH + canvas._spacing))
                col = Math.max(0, Math.min(canvas.gridColumns - _cs, col))
                row = Math.max(0, Math.min(canvas.gridRows - _rs, row))
                canvas.dragCol = col
                canvas.dragRow = row
                canvas.dragValid = canvas.canPlace(col, row, _cs, _rs, cellIndex)
            }

            // Live widget preview — same load/bind logic as DashboardRenderer.
            // Synchronous so prop tweaks from the panel (which rebuild the
            // whole Repeater) don't flicker. Clipped so widget content can
            // never paint outside its cell (mirrors the renderer backstop).
            Loader {
                id: widgetLoader
                anchors.fill: parent
                clip: true
                source: App.WidgetCatalog.urlFor(modelData.type)

                onLoaded: {
                    if (!item) return
                    item.width  = Qt.binding(function() { return widgetLoader.width  })
                    item.height = Qt.binding(function() { return widgetLoader.height })
                    if (modelData.paramId !== undefined && modelData.paramId !== ""
                            && item.paramId !== undefined)
                        item.paramId = modelData.paramId
                    var props = modelData.props || {}
                    for (var key in props) {
                        if (!props.hasOwnProperty(key)) continue
                        if (item[key] === undefined) continue
                        item[key] = canvas._coerce(props[key])
                    }
                }
            }

            // "No PID yet" hint over freshly-placed widgets (suppressed for
            // self-binding widgets that don't take a PID).
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: canvas.dp(4)
                visible: {
                    if (modelData.paramId && modelData.paramId !== "") return false
                    var m = App.WidgetCatalog.metaFor(modelData.type)
                    return m !== null && m.supportedKinds.length > 0
                }
                text: "no PID"
                color: App.Style.statusDanger
                font.family: App.Style.fontFamily
                font.pixelSize: canvas.dp(11)
                opacity: 0.9
            }

            // Selection / outline frame.
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                radius: canvas.dp(4)
                border.color: cellRoot.selected
                              ? App.Style.accent
                              : Qt.darker(App.Style.obdBarColor, 1.6)
                border.width: cellRoot.selected ? 2 : 1
                opacity: cellRoot.selected ? 1.0 : 0.35
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
                drag.target: cellRoot
                drag.axis: Drag.XAndYAxis

                onPressed: canvas.cellTapped(cellRoot.cellIndex)

                drag.onActiveChanged: {
                    if (dragArea.drag.active) {
                        canvas.dragColSpan = cellRoot._cs
                        canvas.dragRowSpan = cellRoot._rs
                        canvas.dragActive = true
                        cellRoot._updateDragCandidate()
                    }
                }

                onPositionChanged: {
                    if (dragArea.drag.active)
                        cellRoot._updateDragCandidate()
                }

                onReleased: {
                    if (canvas.dragActive) {
                        canvas.dragActive = false
                        canvas.cellMoveRequested(cellRoot.cellIndex,
                                                 canvas.dragCol, canvas.dragRow)
                    }
                }

                onCanceled: canvas.dragActive = false
            }
        }
    }
}
