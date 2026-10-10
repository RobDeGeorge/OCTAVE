import QtQuick 2.15
import QtQuick.Layouts 1.15
import "." as App

// Connect a phone over Wi-Fi by QR code: the phone's Wireless debugging >
// "Pair device with QR code" scanner reads WIFI:T:ADB;S:<name>;P:<password>;;
// and phoneMirrorManager pairs and connects once the phone shows up over
// mDNS (startQrPairing). Shows the code while qrPairingActive, otherwise a
// button for a new one. Used by PhoneMirrorView (no phone yet) and the
// Phone Dock settings card.
RowLayout {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: panel

    // Side of the white code box; size it from the screen, not dp, so the
    // code can be scanned from a phone held at arm's length in a car
    property real qrSize: dp(300)
    property bool showCancel: true
    property string fontFamily: App.Style.fontFamily
    signal cancelled()

    readonly property var manager: typeof phoneMirrorManager !== "undefined" ? phoneMirrorManager : null
    readonly property bool active: manager ? manager.qrPairingActive : false
    property string message: ""
    property bool messageOk: true

    function start() {
        message = ""
        if (manager && !active)
            manager.startQrPairing()
    }
    function cancel() {
        if (manager && active)
            manager.cancelQrPairing()
    }

    spacing: dp(24)

    Connections {
        target: panel.manager
        function onWirelessResult(ok, text) {
            panel.messageOk = ok
            panel.message = text
        }
    }

    // The code: black on white with a 4-module quiet zone whatever the
    // theme, so phone cameras lock on. A placeholder of the same size with
    // a button while no code is shown keeps the layout still.
    Rectangle {
        id: qrBox
        objectName: "phoneMirrorQrCode"
        readonly property var rows: panel.manager ? panel.manager.qrPairingRows : []
        readonly property int modules: rows.length + 8
        Layout.preferredWidth: panel.qrSize
        Layout.preferredHeight: panel.qrSize
        Layout.alignment: Qt.AlignVCenter
        color: panel.active ? "white" : Qt.rgba(1, 1, 1, 0.06)
        radius: panel.dpMin(6, 2)

        Canvas {
            id: qrCanvas
            anchors.fill: parent
            visible: panel.active
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var rows = qrBox.rows
                if (!rows || rows.length === 0)
                    return
                // Whole pixels per module keep the edges sharp
                var side = Math.min(width, height)
                var cell = Math.floor(side / qrBox.modules)
                var off = Math.floor((side - cell * rows.length) / 2)
                ctx.fillStyle = "black"
                for (var y = 0; y < rows.length; ++y)
                    for (var x = 0; x < rows[y].length; ++x)
                        if (rows[y].charAt(x) === "1")
                            ctx.fillRect(off + x * cell, off + y * cell, cell, cell)
            }
            Connections {
                target: qrBox
                function onRowsChanged() { qrCanvas.requestPaint() }
            }
            onWidthChanged: requestPaint()
        }

        Rectangle {
            objectName: "phoneMirrorQrShowButton"
            anchors.centerIn: parent
            visible: !panel.active
            width: showLabel.implicitWidth + panel.dp(40)
            height: panel.dp(56)
            radius: panel.dpMin(8, 2)
            color: showMouse.pressed ? Qt.darker(App.Style.accent, 1.3) : App.Style.accent
            Text {
                id: showLabel
                anchors.centerIn: parent
                text: "Show QR code"
                color: "white"
                font.pixelSize: App.Spacing.overallText
                font.family: panel.fontFamily
                font.bold: true
            }
            MouseArea {
                id: showMouse
                anchors.fill: parent
                onClicked: panel.start()
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        spacing: panel.dp(12)

        Text {
            Layout.fillWidth: true
            text: "Connect your phone"
            color: App.Style.primaryTextColor
            font.pixelSize: App.Spacing.overallText * 1.4
            font.family: panel.fontFamily
            font.bold: true
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: "1. Put the phone on the same Wi-Fi network as OCTAVE.\n"
                  + "2. On the phone: Settings > Developer options > Wireless debugging > Pair device with QR code.\n"
                  + "3. Point the phone's camera at this code. OCTAVE pairs, connects and remembers the phone."
            color: App.Style.secondaryTextColor
            font.pixelSize: App.Spacing.overallText
            font.family: panel.fontFamily
            wrapMode: Text.WordWrap
            lineHeight: 1.15
        }

        Text {
            Layout.fillWidth: true
            visible: panel.message !== ""
            text: panel.message
            color: panel.messageOk ? App.Style.statusConnected : App.Style.statusError
            font.pixelSize: App.Spacing.overallText * 0.9
            font.family: panel.fontFamily
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: panel.active
            text: "The code expires after two minutes."
            color: App.Style.secondaryTextColor
            opacity: 0.7
            font.pixelSize: App.Spacing.overallText * 0.85
            font.family: panel.fontFamily
            wrapMode: Text.WordWrap
        }

        Rectangle {
            objectName: "phoneMirrorQrCancel"
            visible: panel.showCancel && panel.active
            Layout.preferredWidth: cancelLabel.implicitWidth + panel.dp(64)
            Layout.preferredHeight: panel.dp(56)
            radius: panel.dpMin(8, 2)
            color: "transparent"
            border.color: App.Style.statusError
            border.width: 2
            Text {
                id: cancelLabel
                anchors.centerIn: parent
                text: "Cancel"
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText
                font.family: panel.fontFamily
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    panel.cancel()
                    panel.cancelled()
                }
            }
        }
    }
}
