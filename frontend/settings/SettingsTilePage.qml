import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as App

Item {
    id: tilePage

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    property var tileModel: []
    // 0 = automatic (2x2 for 4 tiles, otherwise square-ish).
    property int columns: 0
    // Non-zero: fill top to bottom, this many tiles per column, so a column
    // can hold one section. Overrides columns.
    property int rows: 0

    signal tileSelected(string cardId, var originRect)

    // Rect of a tile in tilePage.parent coordinates (as tileSelected
    // reports it), or null if this grid has no such tile.
    function tileRect(cardId) {
        for (var i = 0; i < tileRepeater.count; i++) {
            var t = tileRepeater.itemAt(i)
            if (t && t.cardId === cardId) {
                var pt = t.mapToItem(tilePage.parent, 0, 0)
                return Qt.rect(pt.x, pt.y, t.width, t.height)
            }
        }
        return null
    }

    GridLayout {
        id: grid
        anchors.fill: parent
        anchors.margins: App.Spacing.settingsHubGridSpacing
        columnSpacing: App.Spacing.settingsHubGridSpacing
        rowSpacing: App.Spacing.settingsHubGridSpacing

        flow: tilePage.rows > 0 ? GridLayout.TopToBottom : GridLayout.LeftToRight
        rows: Math.max(1, tilePage.rows)
        // 2x2 grid for 4 tiles; otherwise square-ish auto-fit.
        columns: tilePage.columns > 0 ? tilePage.columns
            : tilePage.tileModel && tilePage.tileModel.length === 4
            ? 2
            : Math.max(2, Math.ceil(Math.sqrt(tilePage.tileModel ? tilePage.tileModel.length : 0)))

        // Split the height the grid is given evenly between its rows, so the
        // tiles always fit above the bottom bar rather than overflowing it.
        readonly property int tileCount: tilePage.tileModel ? tilePage.tileModel.length : 0
        readonly property int rowCount: tilePage.rows > 0 ? tilePage.rows
            : Math.max(1, Math.ceil(tileCount / Math.max(1, columns)))
        readonly property real tileHeight: Math.max(0, (height - (rowCount - 1) * rowSpacing) / rowCount)

        Repeater {
            id: tileRepeater
            model: tilePage.tileModel

            SettingsTile {
                id: tileDelegate
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                // Tiles may span several rows (rowSpan in the model entry).
                readonly property int span: modelData && modelData.rowSpan > 0 ? modelData.rowSpan : 1
                readonly property real spanHeight: span * grid.tileHeight + (span - 1) * grid.rowSpacing
                Layout.rowSpan: span
                Layout.preferredHeight: spanHeight
                Layout.maximumHeight: spanHeight
                cardId: modelData && modelData.cardId !== undefined ? modelData.cardId : ""
                title: modelData && modelData.title !== undefined ? modelData.title : ""
                // `summary` is a function on the page's tileModel entry, so
                // calling it here makes this binding track whatever manager
                // properties it reads — the tile stays live without the page
                // rebuilding its tileModel.
                readonly property var info: modelData && typeof modelData.summary === "function"
                    ? (modelData.summary() || {}) : {}
                stateText: info.state !== undefined ? String(info.state) : ""
                active: info.active === true
                statusColor: info.status !== undefined ? info.status : "transparent"
                statusVisible: info.status !== undefined
                onTileClicked: function(id) {
                    // Map this tile's geometry into the parent of tilePage
                    // (the contentArea), so the popup can zoom from this rect.
                    var pt = tileDelegate.mapToItem(tilePage.parent, 0, 0)
                    tilePage.tileSelected(id, Qt.rect(pt.x, pt.y, tileDelegate.width, tileDelegate.height))
                }
            }
        }
    }
}
