// Dashboard clock independent of the bottom bar's showClock setting.
import QtQuick 2.15
import "../.." as App

Item {
    id: root

    readonly property var octaveSupportedKinds: []
    property bool showDate: true
    property bool showSeconds: false
    property color valueColor: App.Style.obdValueColor
    property color labelColor: App.Style.obdLabelColor
    readonly property bool _use24Hour: (typeof settingsManager !== "undefined" && settingsManager)
        ? settingsManager.clockFormat24Hour : true
    property date _now: new Date()
    readonly property string _timeText: Qt.formatTime(_now,
        (_use24Hour ? "HH" : "hh") + ":mm" + (showSeconds ? ":ss" : "") + (_use24Hour ? "" : " AP"))

    Timer {
        interval: 1000
        running: root.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: root._now = new Date()
    }

    Column {
        anchors.centerIn: parent
        width: parent.width
        spacing: Math.min(App.Spacing.dp(4), root.height * 0.04)

        Text {
            objectName: "dashboardClockTime"
            width: parent.width
            height: Math.max(0, root.height * (root.showDate ? 0.65 : 0.95))
            text: root._timeText
            color: root.valueColor
            font.family: App.Style.fontFamily
            font.bold: true
            font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 3.5, height * 0.8))
            fontSizeMode: Text.Fit
            minimumPixelSize: 1
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            visible: root.showDate
            width: parent.width
            height: Math.max(0, root.height * 0.23)
            text: Qt.formatDate(root._now, "ddd, MMM d")
            color: root.labelColor
            font.family: App.Style.fontFamily
            font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 0.85, height * 0.8))
            fontSizeMode: Text.Fit
            minimumPixelSize: 1
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
