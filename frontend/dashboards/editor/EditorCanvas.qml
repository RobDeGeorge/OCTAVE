// EditorCanvas.qml
//
// Interactive grid canvas for the dashboard editor. Renders the working
// spec's cells as live widgets on a faint dot grid and layers the editing
// gestures on top (see docs/DASHBOARD_EDITOR_REVIEW.md §4.2):
//
//   - tap a widget                                 → cellTapped(index)
//   - press and hold a widget (selects it to drag) → cellHeld(index)
//   - tap free grid space                          → emptyCellTapped(col, row)
//   - tap outside the grid                         → backgroundTapped()
//   - drag the selected widget (after ~8 dp; press
//     and hold drags an unselected one): it lifts
//     and follows the finger, a ghost shows the
//     drop cell and the neighbours slide to where
//     the drop would put them, or the ghost turns
//     red and says why it can't go there          → cellMoveRequested(index, col, row)
//   - drag the selected widget's corner grip       → cellResizeRequested(index, colSpan, rowSpan)
//
// An unselected widget never moves under a passing finger, so scrolling and
// tapping can't rearrange the dashboard by accident. `interactive` false
// (the settings sheet is open) keeps taps and drops drags.
//
// The canvas never mutates the spec itself; DashboardEditor owns it. The
// widgets come from `model`, a ListModel the editor keeps in step with its
// `cells` one row at a time, so a move or resize only repositions a widget
// and a data or option change reloads only that widget. `cells` is the
// same list as a plain array, for the occupancy map and placement checks.
//
// Grid geometry mirrors DashboardRenderer's defaults (20dp margin, 16dp
// spacing) so the layout you edit is the layout that renders.

import QtQuick 2.15
import QtQuick.Window 2.15
import "../.." as App

Item {
    id: canvas

    // Local dp wrapper — mirrors DashboardEditor's Android singleton-function
    // bug workaround.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    property int gridColumns: 12
    property int gridRows: 6
    property var cells: []
    property var model: null
    property int selectedIndex: -1
    property bool interactive: true

    // Smallest comfortable touch target: 9 mm where the screen reports its
    // size (Screen.pixelDensity is per logical pixel), never under 44 dp
    readonly property real touchSize:
        Math.max(dp(44), Screen.pixelDensity > 1 ? Screen.pixelDensity * 9 : 0)

    signal emptyCellTapped(int col, int row)
    signal cellTapped(int index)
    signal cellHeld(int index)
    signal cellMoveRequested(int index, int col, int row)
    signal cellResizeRequested(int index, int colSpan, int rowSpan)
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

    // The grid cell under a canvas point (gutters count to the nearer
    // cell), or null outside the grid
    function cellAt(px, py) {
        var col = Math.floor((px - _margin + _spacing / 2) / (_cellW + _spacing))
        var row = Math.floor((py - _margin + _spacing / 2) / (_cellH + _spacing))
        if (col < 0 || row < 0 || col >= gridColumns || row >= gridRows) return null
        return { "col": col, "row": row }
    }

    // A cell's spec rectangle on the canvas
    function rectOf(c) {
        if (!c) return Qt.rect(0, 0, 0, 0)
        var col = c.col !== undefined ? c.col : 0
        var row = c.row !== undefined ? c.row : 0
        return Qt.rect(_cellX(col), _cellY(row),
                       _spanW(c.colSpan !== undefined ? c.colSpan : 1),
                       _spanH(c.rowSpan !== undefined ? c.rowSpan : 1))
    }
    readonly property rect selectedRect:
        rectOf(selectedIndex >= 0 && selectedIndex < cells.length ? cells[selectedIndex] : null)

    // Cell size in dp, for DashboardManager.minSpan / sizeOptions
    readonly property real cellWidthDp: _cellW / (App.Spacing.effectiveScale || 1.0)
    readonly property real cellHeightDp: _cellH / (App.Spacing.effectiveScale || 1.0)
    readonly property real spacingDp: _spacing / (App.Spacing.effectiveScale || 1.0)

    // True if a cell of the given span fits at (col, row) without leaving the
    // grid or overlapping any placed cell (other than `exceptIndex`, used
    // while moving/resizing an existing cell). The rule lives in C++
    // (src/util/dashboardgrid.h), shared with save / load / import.
    function canPlace(col, row, colSpan, rowSpan, exceptIndex) {
        if (typeof dashboardManager === "undefined" || !dashboardManager) return false
        return dashboardManager.canPlace(cells, gridColumns, gridRows, col, row, colSpan, rowSpan, exceptIndex)
    }

    // Where everything goes if cell `index` goes to this rectangle, the
    // widgets it lands on swapping or moving aside (DashboardGrid::arrange,
    // as the drop does): {ok, cells, moved, swapped, reason}. Asked on every
    // pointer move, so the last answer is kept.
    property string _arrangeKey: ""
    property var _arrangement: null
    function arrangement(index, col, row, colSpan, rowSpan) {
        if (typeof dashboardManager === "undefined" || !dashboardManager) return null
        var key = [index, col, row, colSpan, rowSpan, gridColumns, gridRows].join(",")
        if (key !== _arrangeKey) {
            _arrangeKey = key
            _arrangement = dashboardManager.arrange(cells, gridColumns, gridRows, index,
                                                    col, row, colSpan, rowSpan)
        }
        return _arrangement
    }
    function canArrange(index, col, row, colSpan, rowSpan) {
        var a = arrangement(index, col, row, colSpan, rowSpan)
        return a !== null && a.ok === true
    }
    onCellsChanged: { _arrangeKey = ""; _arrangement = null }

    // Occupancy map: index (row * gridColumns + col) → true when covered by a
    // placed cell. Recomputed whenever `cells` is reassigned (DashboardEditor
    // never mutates in place).
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
    function isFree(col, row) { return !_occupied[row * gridColumns + col] }

    // ── Drag state (written by the dragging delegate) ──────────────────
    property bool dragActive: false
    property int dragIndex: -1
    property bool dragResizing: false
    property int dragCol: 0
    property int dragRow: 0
    property int dragColSpan: 1
    property int dragRowSpan: 1
    property bool dragValid: false
    property string dragReason: ""
    // The other cells where the drop would put them; null = where they are
    readonly property var _previewCells:
        (dragActive && _arrangement && _arrangement.ok) ? _arrangement.cells : null

    function _setCandidate(index, col, row, cs, rs) {
        dragCol = col
        dragRow = row
        dragColSpan = cs
        dragRowSpan = rs
        var a = arrangement(index, col, row, cs, rs)
        dragValid = a !== null && a.ok === true
        dragReason = (a && !a.ok) ? a.reason : ""
    }

    function _endDrag() {
        dragActive = false
        dragResizing = false
        dragIndex = -1
    }

    function _unitBinding(paramId, v) {
        return Qt.binding(function() { return App.OBDParameterModel.toDisplay(paramId, v) })
    }

    function _coerce(v) {
        if (v === "NaN") return NaN
        return v
    }

    // ── Background: taps on free space ──────────────────────────────────
    MouseArea {
        objectName: "editorCanvasBackground"
        anchors.fill: parent
        onClicked: function(mouse) {
            var c = canvas.cellAt(mouse.x, mouse.y)
            if (c && canvas.isFree(c.col, c.row)) canvas.emptyCellTapped(c.col, c.row)
            else canvas.backgroundTapped()
        }
    }

    // ── Dot grid ────────────────────────────────────────────────────────
    // A dot at every gutter crossing: shows the grid without boxing in the
    // widgets. Brighter while a drag or resize is snapping to it.
    Canvas {
        id: dots
        anchors.fill: parent
        opacity: canvas.dragActive ? 0.9 : 0.5
        property color dotColor: App.Style.obdLabelColor
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = dotColor
            var r = Math.max(1, canvas.dp(1.5))
            for (var c = 0; c <= canvas.gridColumns; c++) {
                var x = canvas._cellX(c) - canvas._spacing / 2
                for (var row = 0; row <= canvas.gridRows; row++) {
                    var y = canvas._cellY(row) - canvas._spacing / 2
                    ctx.beginPath()
                    ctx.arc(x, y, r, 0, 2 * Math.PI)
                    ctx.fill()
                }
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onDotColorChanged: requestPaint()
        Connections {
            target: canvas
            function onGridColumnsChanged() { dots.requestPaint() }
            function onGridRowsChanged() { dots.requestPaint() }
            function on_SpacingChanged() { dots.requestPaint() }
            function on_MarginChanged() { dots.requestPaint() }
        }
    }

    // ── Drop ghost while dragging or resizing ───────────────────────────
    Rectangle {
        id: ghost
        objectName: "editorDropGhost"
        visible: canvas.dragActive
        z: 2
        x: canvas._cellX(canvas.dragCol)
        y: canvas._cellY(canvas.dragRow)
        width: canvas._spanW(canvas.dragColSpan)
        height: canvas._spanH(canvas.dragRowSpan)
        radius: canvas.dp(6)
        color: canvas.dragValid
               ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.16)
               : Qt.rgba(App.Style.statusDanger.r, App.Style.statusDanger.g, App.Style.statusDanger.b, 0.22)
        border.color: canvas.dragValid ? App.Style.accent : App.Style.statusDanger
        border.width: 2

        // The size while resizing (the finger is on the corner, so the
        // middle is clear); the reason when the drop won't go
        Rectangle {
            anchors.centerIn: parent
            visible: ghostText.text.length > 0
            width: Math.min(parent.width - canvas.dp(8), ghostText.implicitWidth + canvas.dp(20))
            height: ghostText.implicitHeight + canvas.dp(12)
            radius: canvas.dp(6)
            color: canvas.dragValid ? App.Style.accent : App.Style.statusDanger
            Text {
                id: ghostText
                objectName: "editorDropGhostText"
                anchors.centerIn: parent
                width: Math.min(implicitWidth, ghost.width - canvas.dp(28))
                text: !canvas.dragValid ? canvas.dragReason
                      : canvas.dragResizing ? canvas.dragColSpan + " × " + canvas.dragRowSpan : ""
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: "white"
                font.family: App.Style.fontFamily
                font.bold: true
                font.pixelSize: App.Spacing.overallText * 0.85
            }
        }
    }

    // ── Placed widgets ──────────────────────────────────────────────────
    Repeater {
        model: canvas.model
        delegate: Item {
            id: cellRoot
            objectName: "editorCell_" + index

            readonly property int cellIndex: index
            readonly property int _c:  model.cellCol
            readonly property int _r:  model.cellRow
            readonly property int _cs: model.cellColSpan
            readonly property int _rs: model.cellRowSpan
            readonly property bool selected: canvas.selectedIndex === cellIndex
            readonly property bool dragging: dragArea.drag.active
            readonly property bool resizing: canvas.dragResizing && canvas.dragIndex === cellIndex
            // Held down long enough to drag without being selected first
            property bool _held: false

            // Where the drop being dragged would put this cell
            readonly property var _shown: (canvas._previewCells && canvas.dragIndex !== cellIndex)
                                          ? canvas._previewCells[cellIndex] : null
            readonly property int _showC:  _shown ? _shown.col : _c
            readonly property int _showR:  _shown ? _shown.row : _r

            // Bound until a drag takes over x / y; _rebind() restores them
            x: canvas._cellX(_showC)
            y: canvas._cellY(_showR)
            width: canvas._spanW(_cs)
            height: canvas._spanH(_rs)
            function _rebind() {
                x = Qt.binding(function() { return canvas._cellX(cellRoot._showC) })
                y = Qt.binding(function() { return canvas._cellY(cellRoot._showR) })
            }
            Behavior on x { enabled: !cellRoot.dragging; NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
            Behavior on y { enabled: !cellRoot.dragging; NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

            // Lift: the selected widget rises a little, the dragged one more
            z: dragging ? 10 : (selected ? 5 : 1)
            scale: dragging ? 1.05 : (selected && canvas.interactive ? 1.02 : 1.0)
            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

            function _updateDragCandidate() {
                var col = Math.round((x - canvas._margin) / (canvas._cellW + canvas._spacing))
                var row = Math.round((y - canvas._margin) / (canvas._cellH + canvas._spacing))
                col = Math.max(0, Math.min(canvas.gridColumns - _cs, col))
                row = Math.max(0, Math.min(canvas.gridRows - _rs, row))
                canvas._setCandidate(cellIndex, col, row, _cs, _rs)
            }

            // Shadow under a lifted widget (two soft layers, no effect pass)
            Rectangle {
                visible: cellRoot.selected || cellRoot.dragging
                anchors.fill: parent
                anchors.margins: -canvas.dp(2)
                anchors.topMargin: canvas.dp(cellRoot.dragging ? 8 : 4)
                anchors.bottomMargin: -canvas.dp(cellRoot.dragging ? 10 : 5)
                radius: canvas.dp(10)
                color: "#000000"
                opacity: cellRoot.dragging ? 0.28 : 0.16
            }
            Rectangle {
                visible: cellRoot.selected || cellRoot.dragging
                anchors.fill: parent
                anchors.topMargin: canvas.dp(2)
                anchors.bottomMargin: -canvas.dp(cellRoot.dragging ? 5 : 2)
                radius: canvas.dp(8)
                color: "#000000"
                opacity: cellRoot.dragging ? 0.3 : 0.18
            }
            Rectangle {
                anchors.fill: parent
                radius: canvas.dp(6)
                color: App.Style.obdBoxBackground
                opacity: (cellRoot.selected || cellRoot.dragging) ? 1.0 : 0.0
            }

            // Live widget preview, loaded and bound as DashboardRenderer does.
            // Synchronous so a reload doesn't flicker; clipped so content
            // never paints outside its cell (mirrors the renderer backstop).
            // Reloaded only when this cell's type, PID or props change.
            readonly property string _content: model.type + "\n" + model.paramId + "\n" + model.propsJson
            on_ContentChanged: {
                widgetLoader.source = ""
                widgetLoader.source = App.WidgetCatalog.urlFor(model.type)
            }

            Loader {
                id: widgetLoader
                objectName: "editorWidgetLoader"
                anchors.fill: parent
                clip: true
                source: App.WidgetCatalog.urlFor(model.type)

                onLoaded: {
                    if (!item) return
                    item.width  = Qt.binding(function() { return widgetLoader.width  })
                    item.height = Qt.binding(function() { return widgetLoader.height })
                    var paramId = model.paramId
                    // An unknown PID (a newer build's) stays in the spec
                    // but unbound, as in the renderer
                    if (paramId !== "" && item.paramId !== undefined
                            && App.OBDParameterModel.hasParameter(paramId))
                        item.paramId = paramId
                    // Range props (min, max, thresholds) are in the PID's own
                    // unit in the spec: bound through the gauges' unit, so
                    // they follow a unit setting change too
                    var props = JSON.parse(model.propsJson || "{}")
                    var unitKeys = paramId ? App.WidgetCatalog.paramUnitProps(model.type) : []
                    for (var key in props) {
                        if (!props.hasOwnProperty(key)) continue
                        if (item[key] === undefined) continue
                        var v = canvas._coerce(props[key])
                        if (unitKeys.indexOf(key) !== -1)
                            item[key] = canvas._unitBinding(paramId, v)
                        else
                            item[key] = v
                    }
                }
            }

            // "No data yet" hint over freshly-placed widgets (suppressed for
            // self-binding widgets that don't take a PID).
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: canvas.dp(4)
                visible: {
                    if (model.paramId !== "") return false
                    var m = App.WidgetCatalog.metaFor(model.type)
                    return m !== null && m.supportedKinds.length > 0
                }
                width: noPid.implicitWidth + canvas.dp(12)
                height: noPid.implicitHeight + canvas.dp(4)
                radius: height / 2
                color: App.Style.statusDanger
                Text {
                    id: noPid
                    anchors.centerIn: parent
                    text: "Choose data"
                    color: "white"
                    font.family: App.Style.fontFamily
                    font.pixelSize: Math.max(9, App.Spacing.overallText * 0.7)
                }
            }

            // Selection / outline frame.
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                radius: canvas.dp(6)
                border.color: cellRoot.selected
                              ? App.Style.accent
                              : Qt.darker(App.Style.obdBarColor, 1.6)
                border.width: cellRoot.selected ? canvas.dp(2) : 1
                opacity: cellRoot.selected ? 1.0 : 0.35
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
                // Only the selected widget drags (or one held down): a swipe
                // over the dashboard never moves anything
                drag.target: (canvas.interactive && (cellRoot.selected || cellRoot._held)) ? cellRoot : null
                drag.axis: Drag.XAndYAxis
                drag.threshold: canvas.dp(8)
                preventStealing: cellRoot.selected || cellRoot._held

                onClicked: canvas.cellTapped(cellRoot.cellIndex)
                onPressAndHold: {
                    if (!canvas.interactive || cellRoot.selected) return
                    cellRoot._held = true
                    canvas.cellHeld(cellRoot.cellIndex)
                }

                drag.onActiveChanged: {
                    if (dragArea.drag.active) {
                        canvas.dragIndex = cellRoot.cellIndex
                        canvas.dragResizing = false
                        canvas.dragActive = true
                        cellRoot._updateDragCandidate()
                    }
                }

                onPositionChanged: {
                    if (dragArea.drag.active)
                        cellRoot._updateDragCandidate()
                }

                onReleased: {
                    cellRoot._held = false
                    if (canvas.dragActive && canvas.dragIndex === cellRoot.cellIndex) {
                        var col = canvas.dragCol, row = canvas.dragRow
                        canvas._endDrag()
                        // Back on the grid: at the new cell if the drop is
                        // taken, else where it was
                        cellRoot._rebind()
                        canvas.cellMoveRequested(cellRoot.cellIndex, col, row)
                    }
                }

                onCanceled: {
                    cellRoot._held = false
                    if (canvas.dragIndex === cellRoot.cellIndex) canvas._endDrag()
                    cellRoot._rebind()
                }
            }

            // ── Resize grip (selected widget only) ──────────────────────
            // Bottom-right corner, drawn just outside the widget so it
            // doesn't cover the widget's own content. The drag snaps the far
            // corner to the grid cell under the finger, never below the
            // widget's readable minimum (`minColSpan` / `minRowSpan`).
            Item {
                id: resizeGrip
                objectName: "editorResizeGrip_" + cellRoot.cellIndex
                visible: cellRoot.selected && canvas.interactive && !cellRoot.dragging
                z: 20
                width: canvas.touchSize
                height: canvas.touchSize
                x: cellRoot.width - width / 2
                y: cellRoot.height - height / 2

                Rectangle {
                    anchors.centerIn: parent
                    width: canvas.dp(24)
                    height: canvas.dp(24)
                    radius: width / 2
                    color: App.Style.accent
                    border.color: App.Style.obdBoxBackground
                    border.width: canvas.dp(3)
                }

                MouseArea {
                    id: gripMouse
                    anchors.fill: parent
                    preventStealing: true
                    cursorShape: Qt.SizeFDiagCursor

                    function _update(mouse) {
                        var p = mapToItem(canvas, mouse.x, mouse.y)
                        var stepX = canvas._cellW + canvas._spacing
                        var stepY = canvas._cellH + canvas._spacing
                        var endCol = Math.floor((p.x - canvas._margin + canvas._spacing / 2) / stepX)
                        var endRow = Math.floor((p.y - canvas._margin + canvas._spacing / 2) / stepY)
                        var least = canvas.minSpanFor ? canvas.minSpanFor(cellRoot.cellIndex) : null
                        var minCs = least ? Math.min(cellRoot._cs, least.colSpan) : 1
                        var minRs = least ? Math.min(cellRoot._rs, least.rowSpan) : 1
                        var cs = Math.max(minCs, Math.min(canvas.gridColumns - cellRoot._c, endCol - cellRoot._c + 1))
                        var rs = Math.max(minRs, Math.min(canvas.gridRows - cellRoot._r, endRow - cellRoot._r + 1))
                        canvas._setCandidate(cellRoot.cellIndex, cellRoot._c, cellRoot._r, cs, rs)
                    }

                    onPressed: function(mouse) {
                        canvas.dragIndex = cellRoot.cellIndex
                        canvas.dragResizing = true
                        canvas.dragActive = true
                        _update(mouse)
                    }
                    onPositionChanged: function(mouse) { if (canvas.dragActive) _update(mouse) }
                    onReleased: {
                        if (!canvas.dragActive) return
                        var cs = canvas.dragColSpan, rs = canvas.dragRowSpan
                        canvas._endDrag()
                        canvas.cellResizeRequested(cellRoot.cellIndex, cs, rs)
                    }
                    onCanceled: canvas._endDrag()
                }
            }
        }
    }

    // The smallest span cell `index` may shrink to ({colSpan, rowSpan}), set
    // by the editor; null = any
    property var minSpanFor: null
}
