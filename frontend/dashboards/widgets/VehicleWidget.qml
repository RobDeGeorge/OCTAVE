// VehicleWidget.qml
//
// The vehicle as a picture (App.VehiclePicture: vehicleManager's active
// vehicle), for a dashboard and for Home. With no vehicle chosen it says so;
// an added model whose picture has not been rendered yet shows its name.
//
// Self-binding widget: no paramId (WidgetCatalog supportedKinds: []).
import QtQuick 2.15
import "../.." as App

Item {
    id: root

    // ── Widget manifest (consumed by the dashboard editor) ──────────
    readonly property var octaveSupportedKinds: []   // self-binding, no PID

    // ── Features ────────────────────────────────────────────────────
    property bool showFrame: true    // the reading cards' box around it

    // ── Style ───────────────────────────────────────────────────────
    property color frameColor: App.Style.obdBarColor
    property color labelColor: App.Style.obdLabelColor
    property color valueColor: App.Style.obdValueColor

    readonly property real _fontFit: Math.max(0.1, Math.min(1,
        height / App.Spacing.dp(110), width / App.Spacing.dp(200)))

    Rectangle {
        visible: root.showFrame
        anchors.fill: parent
        radius: App.Spacing.dp(8)
        color: App.Style.obdBoxBackground
        border.color: Qt.darker(root.frameColor, 1.4)
        border.width: 1
    }

    App.VehiclePicture {
        id: picture
        objectName: "dashboardVehiclePicture"
        anchors.fill: parent
        anchors.margins: Math.min(App.Spacing.dp(12), Math.min(root.width, root.height) * 0.08)
    }

    Column {
        visible: !picture.hasPicture
        anchors.centerIn: parent
        width: parent.width - App.Spacing.dp(16)
        spacing: App.Spacing.dp(6) * root._fontFit
        readonly property bool none: !picture.spec || !picture.spec.id

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: parent.none ? "No vehicle selected" : (picture.spec.name || "")
            color: root.valueColor
            font.family: App.Style.fontFamily
            font.bold: true
            font.pixelSize: Math.max(1, App.Spacing.overallText * 1.1 * root._fontFit)
        }
        Text {
            width: parent.width
            visible: parent.none
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: "Choose one in Settings > Device > Vehicle"
            color: root.labelColor
            font.family: App.Style.fontFamily
            font.pixelSize: Math.max(1, App.Spacing.overallText * 0.8 * root._fontFit)
        }
    }
}
