import QtQuick 2.15
import "." as App
import OCTAVE.OBD 1.0

// "Connect Bluetooth OBD2": opens the adapter sheet, which scans, pairs and
// connects (OBDAdapterSheet.qml). Shown by the OBD pages while no adapter
// is connected (OBDHome.qml, the connect prompt in OBDMenu.qml).
Rectangle {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: obdConnectButton
    objectName: "obdConnectButton"

    // For the sheet's "OBD Settings" fallback
    property var mainWindow: null
    property string fontFamily: App.Style.fontFamily

    height: App.Spacing.bottomBarNavButtonHeight
    width: obdConnectLabel.implicitWidth + dp(32)
    radius: dpMin(8, 2)
    color: "transparent"
    border.color: App.Style.accent
    border.width: 1
    scale: obdConnectMouse.pressed ? 0.9 : 1.0
    Behavior on scale {
        NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
    }

    readonly property bool _connecting: typeof obdManager !== "undefined" && obdManager
                                        && obdManager.connectionState === OBDManager.Connecting

    Text {
        id: obdConnectLabel
        anchors.centerIn: parent
        text: obdConnectButton._connecting ? "Connecting…" : "Connect Bluetooth OBD2"
        color: App.Style.accent
        font.pixelSize: App.Spacing.overallText
        font.bold: true
        font.family: obdConnectButton.fontFamily
    }

    MouseArea {
        id: obdConnectMouse
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: obdAdapterSheet.open()
    }

    App.OBDAdapterSheet {
        id: obdAdapterSheet
        offerSettings: true
        onSettingsRequested: {
            if (obdConnectButton.mainWindow && typeof obdConnectButton.mainWindow.navigateToSettings === "function")
                obdConnectButton.mainWindow.navigateToSettings("obdSettings")
        }
    }
}
