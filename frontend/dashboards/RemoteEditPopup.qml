// RemoteEditPopup.qml
//
// "Edit in a browser" in the dashboard chooser: where the browser
// dashboard editor (sdk/web/dashboards.html, GET /dashboards) is reached
// this run, as a link and a QR code to scan, with a pairing code for a
// phone or laptop on the network. What it edits shows live on this screen
// (dashboardManager.preview). The addresses come from servicePairing
// (serviceUrls / onNetwork): with the service off, or listening on
// loopback only, it says how to get there instead.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as App

Popup {
    id: popup
    objectName: "remoteEditPopup"

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    readonly property var _pairing: typeof servicePairing !== "undefined" ? servicePairing : null
    readonly property var urls: _pairing ? _pairing.serviceUrls : []
    readonly property bool serving: urls.length > 0
    readonly property bool onNetwork: !!_pairing && _pairing.onNetwork
    readonly property string editorUrl: serving ? urls[0] + "/dashboards" : ""
    readonly property bool codeShowing: !!_pairing && _pairing.pairingCode !== ""

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(parent.width - dp(24), dp(760))
    height: Math.min(parent.height - dp(24), implicitHeight)
    modal: true
    focus: true
    padding: dp(20)

    background: Rectangle {
        color: App.Style.obdBoxBackground
        radius: popup.dpMin(12, 4)
        border.color: Qt.darker(App.Style.obdBarColor, 1.5)
        border.width: 1
    }

    component Body: Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: App.Style.primaryTextColor
        font.family: App.Style.fontFamily
        font.pixelSize: App.Spacing.overallText * 0.9
    }

    component PillButton: Rectangle {
        id: pill
        property string text: ""
        property bool primary: false
        signal clicked()
        Layout.preferredHeight: popup.dp(48)
        Layout.preferredWidth: pillText.implicitWidth + popup.dp(32)
        radius: height / 2
        color: primary ? App.Style.accent
               : pillMouse.pressed ? Qt.lighter(App.Style.obdBoxBackground, 1.3)
               : Qt.darker(App.Style.obdBoxBackground, 1.25)
        border.width: primary ? 0 : 1
        border.color: Qt.darker(App.Style.obdBarColor, 1.5)
        Text {
            id: pillText
            anchors.centerIn: parent
            text: pill.text
            color: pill.primary ? App.Style.obdBoxBackground : App.Style.primaryTextColor
            font.family: App.Style.fontFamily
            font.bold: pill.primary
            font.pixelSize: App.Spacing.overallText * 0.95
        }
        MouseArea { id: pillMouse; anchors.fill: parent; onClicked: pill.clicked() }
    }

    contentItem: ColumnLayout {
        spacing: popup.dp(12)

        Text {
            text: "Edit in a browser"
            color: App.Style.primaryTextColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 1.2
            font.bold: true
        }

        Body {
            visible: !popup.serving
            objectName: "remoteEditOff"
            text: "The service is off, so no browser can reach OCTAVE. Turn it on in Settings > Device > Service; "
                  + "it starts with the next start of OCTAVE."
        }

        RowLayout {
            visible: popup.serving
            Layout.fillWidth: true
            spacing: popup.dp(18)

            // The link as a QR code, for a phone's camera
            Rectangle {
                id: qrBox
                visible: popup.onNetwork
                readonly property var rows: popup.visible && popup.onNetwork && popup._pairing
                                            ? popup._pairing.qrRows(popup.editorUrl) : []
                readonly property int modules: rows.length + 8
                Layout.preferredWidth: popup.dp(200)
                Layout.preferredHeight: popup.dp(200)
                Layout.alignment: Qt.AlignTop
                color: "white"
                radius: popup.dpMin(6, 2)
                Canvas {
                    id: qrCanvas
                    objectName: "remoteEditQr"
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        var rows = qrBox.rows
                        if (!rows || rows.length === 0) return
                        var side = Math.min(width, height)
                        var cell = Math.floor(side / qrBox.modules)
                        var off = Math.floor((side - cell * rows.length) / 2)
                        ctx.fillStyle = "black"
                        for (var y = 0; y < rows.length; ++y)
                            for (var x = 0; x < rows[y].length; ++x)
                                if (rows[y].charAt(x) === "1")
                                    ctx.fillRect(off + x * cell, off + y * cell, cell, cell)
                    }
                }
                onRowsChanged: qrCanvas.requestPaint()
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: popup.dp(10)
                Body {
                    text: popup.onNetwork
                          ? "Scan the code with a phone, or open this on a laptop on the same network:"
                          : "Open this in a browser on this computer:"
                }
                Text {
                    objectName: "remoteEditUrl"
                    Layout.fillWidth: true
                    text: popup.editorUrl
                    wrapMode: Text.WrapAnywhere
                    color: App.Style.accent
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 1.05
                    font.bold: true
                }
                Body {
                    visible: !popup.onNetwork
                    color: App.Style.obdLabelColor
                    text: "To edit from a phone or laptop, OCTAVE has to listen on your network: start it with "
                          + "--service-bind 0.0.0.0 (the wiki's Writing a frontend page says what that opens up)."
                }
                Body {
                    text: "What you change there shows here as you go. Save it there; it then is in this list."
                    color: App.Style.obdLabelColor
                }
                RowLayout {
                    visible: popup.onNetwork
                    spacing: popup.dp(12)
                    PillButton {
                        objectName: "remoteEditPair"
                        visible: !popup.codeShowing
                        text: "Show a pairing code"
                        onClicked: popup._pairing.startPairing()
                    }
                    Text {
                        objectName: "remoteEditCode"
                        visible: popup.codeShowing
                        text: popup.codeShowing ? popup._pairing.pairingCode.substr(0, 3) + " "
                                                  + popup._pairing.pairingCode.substr(3) : ""
                        color: App.Style.primaryTextColor
                        font.family: App.Style.fontFamily
                        font.pixelSize: App.Spacing.overallText * 1.6
                        font.bold: true
                        font.letterSpacing: popup.dp(2)
                    }
                    Text {
                        visible: popup.codeShowing
                        text: popup.codeShowing ? popup._pairing.pairingSecondsLeft + " s" : ""
                        color: App.Style.obdLabelColor
                        font.family: App.Style.fontFamily
                        font.pixelSize: App.Spacing.overallText * 0.85
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PillButton {
                objectName: "remoteEditDone"
                text: "Done"
                primary: true
                onClicked: popup.close()
            }
        }
    }
}
