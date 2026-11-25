// HomeTelemetry.qml
//
// Home as a data logger: the last half-minute of revs, speed, throttle and
// engine load as four live traces, and beside them the speed in figures,
// the black box's state (recording, how long, how many samples), the
// G-force plot and three more readings. Upright, the traces are on top.
import QtQuick
import QtQuick.Controls
import ".." as App
import "../gauges" as Gauges

HomeScreen {
    id: home
    objectName: "homeTelemetry"

    paramIds: ["RPM", "SPEED", "THROTTLE_POS", "ENGINE_LOAD", "COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE", "INTAKE_TEMP"]
    usesImu: true

    readonly property string mono: "JetBrains Mono"
    readonly property real pad: 2.4 * u
    readonly property var traces: [
        { "id": "RPM", "color": App.Style.obdBarColor },
        { "id": "SPEED", "color": App.Style.accent },
        { "id": "THROTTLE_POS", "color": App.Style.statusConnected },
        { "id": "ENGINE_LOAD", "color": App.Style.statusWarning }
    ]
    readonly property var rec: (typeof tripRecorder !== "undefined" && tripRecorder) ? tripRecorder : null
    readonly property bool recording: !!rec && rec.recording
    readonly property color rule: Qt.rgba(App.Style.obdLabelColor.r, App.Style.obdLabelColor.g, App.Style.obdLabelColor.b, 0.22)

    // The two halves
    readonly property rect plotBox: portrait ? Qt.rect(pad, pad, width - 2 * pad, height * 0.52 - pad)
                                             : Qt.rect(pad, pad, width * 0.62 - pad, height - 2 * pad)
    readonly property rect sideBox: portrait ? Qt.rect(pad, height * 0.52 + pad, width - 2 * pad, height * 0.48 - 2 * pad)
                                             : Qt.rect(width * 0.62 + pad, pad, width * 0.38 - 2 * pad, height - 2 * pad)

    Rectangle { anchors.fill: parent; color: App.Style.backgroundColor }

    // ── The traces ──
    Column {
        id: plots
        objectName: "telemetryPlots"
        x: home.plotBox.x; y: home.plotBox.y
        width: home.plotBox.width
        spacing: home.pad
        readonly property real each: (home.plotBox.height - 3 * spacing) / 4
        Repeater {
            model: home.traces
            Rectangle {
                id: plot
                required property var modelData
                width: plots.width
                height: plots.each
                color: App.Style.obdBoxBackground
                border.width: 1
                border.color: home.rule
                // The trace's colour down the left edge
                Rectangle { width: Math.max(3, 0.6 * home.u); height: parent.height; color: plot.modelData.color }
                Gauges.SparklineGauge {
                    anchors.fill: parent
                    anchors.leftMargin: 2 * home.u
                    anchors.rightMargin: home.u
                    anchors.topMargin: 0.8 * home.u
                    anchors.bottomMargin: 0.8 * home.u
                    paramId: plot.modelData.id
                    lineColor: plot.modelData.color
                    backgroundColor: "transparent"
                    maxSamples: 120
                    sampleIntervalMs: 250
                }
            }
        }
    }

    // ── Beside the traces ──
    Item {
        id: side
        objectName: "telemetrySide"
        x: home.sideBox.x; y: home.sideBox.y
        width: home.sideBox.width; height: home.sideBox.height
        // Upright: the figures on the left, the G plot on the right
        readonly property real plotSize: home.portrait ? Math.min(height, width * 0.46)
                                                       : Math.min(width, height * 0.42)

        // Recording, and for how long
        Row {
            id: recRow
            spacing: 1.4 * home.u
            Rectangle {
                id: recDot
                anchors.verticalCenter: parent.verticalCenter
                width: 2.4 * home.u; height: width; radius: width / 2
                color: home.recording ? App.Style.statusDanger : home.rule
                SequentialAnimation on opacity {
                    running: home.recording && home.visible
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.25; duration: 700 }
                    NumberAnimation { to: 1.0; duration: 700 }
                    onRunningChanged: if (!running) recDot.opacity = 1
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: home.recording ? "REC  " + home.duration(home.rec.currentTripElapsedS * 1000)
                                       + "   " + home.rec.currentSampleCount + " samples"
                                     : "STANDBY"
                color: App.Style.obdLabelColor
                font.family: home.mono
                font.pixelSize: Math.max(9, 2.6 * home.u)
            }
        }

        // The speed, in figures
        Row {
            id: speedRow
            anchors.top: recRow.bottom
            anchors.topMargin: home.u
            spacing: 1.5 * home.u
            Text {
                id: speedFigure
                text: home.text("SPEED")
                color: App.Style.obdValueColor
                font.family: home.mono
                font.pixelSize: Math.min(side.width * (home.portrait ? 0.2 : 0.3), side.height * 0.24)
                font.bold: true
            }
            Text {
                anchors.baseline: speedFigure.baseline
                text: home.unitOf("SPEED")
                color: App.Style.obdLabelColor
                font.family: home.mono
                font.pixelSize: speedFigure.font.pixelSize * 0.3
            }
        }

        // A table of three more
        Column {
            id: table
            anchors.top: speedRow.bottom
            anchors.topMargin: home.u
            width: home.portrait ? side.width - side.plotSize - home.pad : side.width
            Repeater {
                model: ["COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE", "INTAKE_TEMP"]
                Item {
                    required property string modelData
                    width: table.width
                    height: Math.max(12, 5.4 * home.u)
                    Rectangle { width: parent.width; height: 1; color: home.rule }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: home.titleOf(modelData).toUpperCase()
                        color: App.Style.obdLabelColor
                        font.family: home.mono
                        font.pixelSize: Math.max(9, 2.6 * home.u)
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        text: home.text(modelData) + " " + home.unitOf(modelData)
                        color: App.Style.obdValueColor
                        font.family: home.mono
                        font.pixelSize: Math.max(10, 3.2 * home.u)
                        font.bold: true
                    }
                }
            }
        }

        App.OBDConnectButton {
            objectName: "telemetryConnectButton"
            anchors.top: table.bottom
            anchors.topMargin: home.u
            visible: !home.obdConnected
            mainWindow: home.mainWindow
            fontFamily: home.font
        }

        // The G-force plot
        Rectangle {
            objectName: "telemetryG"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: side.plotSize
            height: side.plotSize
            color: App.Style.obdBoxBackground
            border.width: 1
            border.color: home.rule
            Gauges.GForceGauge { anchors.fill: parent; anchors.margins: home.u }
        }
    }
}
