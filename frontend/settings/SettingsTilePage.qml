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

        // 2x2 grid for 4 tiles; otherwise square-ish auto-fit.
        columns: tilePage.columns > 0 ? tilePage.columns
            : tilePage.tileModel && tilePage.tileModel.length === 4
            ? 2
            : Math.max(2, Math.ceil(Math.sqrt(tilePage.tileModel ? tilePage.tileModel.length : 0)))

        Repeater {
            id: tileRepeater
            model: tilePage.tileModel

            SettingsTile {
                id: tileDelegate
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: tilePage.dp(100)
                cardId: modelData && modelData.cardId !== undefined ? modelData.cardId : ""
                title: modelData && modelData.title !== undefined ? modelData.title : ""
                icon: modelData && modelData.icon !== undefined ? modelData.icon : ""
                iconSource: modelData && modelData.iconSource !== undefined ? modelData.iconSource : ""
                statusColor: modelData && modelData.statusColor !== undefined
                    ? modelData.statusColor : "transparent"
                statusVisible: modelData && modelData.statusVisible === true
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
