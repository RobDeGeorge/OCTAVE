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
    // Non-zero: fill top to bottom in `rows` slots per column, each tile
    // taking its rowSpan (default 1), so a column can hold one section.
    // Overrides columns.
    property int rows: 0
    // Non-empty: a scrolling list instead of a grid (portrait), each
    // section ({name, tiles}) under its caption with its tiles
    // sectionColumns across. Overrides rows and columns.
    property var sections: []
    property int sectionColumns: 2
    readonly property bool _stacked: sections && sections.length > 0

    signal tileSelected(string cardId, var originRect)

    // Rect of a tile in tilePage.parent coordinates (as tileSelected
    // reports it), or null if this grid has no such tile.
    function tileRect(cardId) {
        var reps = []
        if (tilePage._stacked) {
            for (var s = 0; s < sectionRepeater.count; s++) {
                var sec = sectionRepeater.itemAt(s)
                if (sec) reps.push(sec.tileRepeater)
            }
        } else {
            reps.push(tilePage.rows > 0 ? columnRepeater : tileRepeater)
        }
        for (var r = 0; r < reps.length; r++) {
            var rep = reps[r]
            for (var i = 0; i < rep.count; i++) {
                var t = rep.itemAt(i)
                if (t && t.cardId === cardId) {
                    var pt = t.mapToItem(tilePage.parent, 0, 0)
                    return Qt.rect(pt.x, pt.y, t.width, t.height)
                }
            }
        }
        return null
    }

    component Tile: SettingsTile {
        id: tile
        required property var modelData
        required property int index
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
            var pt = tile.mapToItem(tilePage.parent, 0, 0)
            tilePage.tileSelected(id, Qt.rect(pt.x, pt.y, tile.width, tile.height))
        }
    }

    // rows > 0: tiles fill each column top to bottom, a tile spanning
    // rowSpan of the `rows` slots. Placed by hand: GridLayout shares row
    // heights across columns and, with different spans per column (3 tiles
    // of 4 next to 4 tiles of 3), sizes the rows unevenly and leaves gaps.
    Item {
        id: columnGrid
        visible: tilePage.rows > 0 && !tilePage._stacked
        anchors.fill: parent
        anchors.margins: App.Spacing.settingsHubGridSpacing

        readonly property real spacing: App.Spacing.settingsHubGridSpacing
        // {column, row} of each tile, in model order.
        readonly property var slots: {
            var out = [], col = 0, row = 0
            var model = tilePage.rows > 0 && tilePage.tileModel ? tilePage.tileModel : []
            for (var i = 0; i < model.length; i++) {
                var span = Math.min(tilePage.rows, Math.max(1, model[i].rowSpan || 1))
                if (row + span > tilePage.rows) { col++; row = 0 }
                out.push({ column: col, row: row, span: span })
                row += span
            }
            return out
        }
        readonly property int columnCount: slots.length > 0 ? slots[slots.length - 1].column + 1 : 1
        readonly property real columnWidth: Math.max(0, (width - (columnCount - 1) * spacing) / columnCount)
        readonly property real slotHeight: Math.max(0, (height - (tilePage.rows - 1) * spacing) / Math.max(1, tilePage.rows))

        Repeater {
            id: columnRepeater
            model: tilePage.rows > 0 && !tilePage._stacked ? tilePage.tileModel : []

            Tile {
                readonly property var slot: columnGrid.slots[index] || { column: 0, row: 0, span: 1 }
                readonly property real slotTop: slot.row * (columnGrid.slotHeight + columnGrid.spacing)
                readonly property real slotBottom: (slot.row + slot.span) * (columnGrid.slotHeight + columnGrid.spacing) - columnGrid.spacing
                x: Math.round(slot.column * (columnGrid.columnWidth + columnGrid.spacing))
                y: Math.round(slotTop)
                width: Math.round(columnGrid.columnWidth)
                height: Math.round(slotBottom) - Math.round(slotTop)
            }
        }
    }

    GridLayout {
        id: grid
        visible: tilePage.rows <= 0 && !tilePage._stacked
        anchors.fill: parent
        anchors.margins: App.Spacing.settingsHubGridSpacing
        columnSpacing: App.Spacing.settingsHubGridSpacing
        rowSpacing: App.Spacing.settingsHubGridSpacing

        // 2x2 grid for 4 tiles; otherwise square-ish auto-fit.
        columns: tilePage.columns > 0 ? tilePage.columns
            : tilePage.tileModel && tilePage.tileModel.length === 4
            ? 2
            : Math.max(2, Math.ceil(Math.sqrt(tilePage.tileModel ? tilePage.tileModel.length : 0)))

        // Split the height the grid is given evenly between its rows, so the
        // tiles always fit above the bottom bar rather than overflowing it.
        readonly property int tileCount: tilePage.tileModel ? tilePage.tileModel.length : 0
        readonly property int rowCount: Math.max(1, Math.ceil(tileCount / Math.max(1, columns)))
        readonly property real tileHeight: Math.max(0, (height - (rowCount - 1) * rowSpacing) / rowCount)

        Repeater {
            id: tileRepeater
            model: tilePage.rows <= 0 && !tilePage._stacked ? tilePage.tileModel : []

            Tile {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                Layout.preferredHeight: grid.tileHeight
                Layout.maximumHeight: grid.tileHeight
            }
        }
    }

    // sections: one scrolling column, a caption per section and its tiles
    // in a grid under it, so tiles keep a readable width on a phone.
    Flickable {
        id: sectionFlick
        visible: tilePage._stacked
        anchors.fill: parent
        anchors.margins: App.Spacing.settingsHubGridSpacing
        contentHeight: sectionColumn.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        readonly property real spacing: App.Spacing.settingsHubGridSpacing
        readonly property real tileWidth: Math.max(0,
            (width - (tilePage.sectionColumns - 1) * spacing) / Math.max(1, tilePage.sectionColumns))
        readonly property real tileHeight: Math.max(tilePage.dp(72), tileWidth * 0.5)

        Column {
            id: sectionColumn
            width: sectionFlick.width
            spacing: sectionFlick.spacing * 2

            Repeater {
                id: sectionRepeater
                model: tilePage._stacked ? tilePage.sections : []

                Column {
                    required property var modelData
                    readonly property alias tileRepeater: sectionTiles
                    width: sectionColumn.width
                    spacing: sectionFlick.spacing

                    Text {
                        visible: text !== ""
                        width: parent.width
                        text: modelData.name || ""
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText * 0.8
                        font.family: App.Style.fontFamily
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.5
                        font.capitalization: Font.AllUppercase
                        elide: Text.ElideRight
                    }

                    Grid {
                        columns: tilePage.sectionColumns
                        spacing: sectionFlick.spacing

                        Repeater {
                            id: sectionTiles
                            model: modelData.tiles

                            Tile {
                                width: Math.floor(sectionFlick.tileWidth)
                                height: Math.round(sectionFlick.tileHeight)
                            }
                        }
                    }
                }
            }
        }
    }
}
