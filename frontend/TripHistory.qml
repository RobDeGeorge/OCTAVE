import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App

// Trip History -- the black box viewer. Left: every recorded drive, newest
// first (the one being recorded on top, live). Right: the selected drive's
// summary, a chart of any recorded parameter, its events, and export /
// delete. Data comes from tripRecorder (TripRecorder, C++); the files it
// lists are the same CSV + JSON a person can pull off the box.
Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: tripHistoryPage
    // "obd" prefix: BottomBar.openOBDSection() steps OBD subpages back to OBD Home
    objectName: "obdTripHistory"
    required property StackView stackView
    required property ApplicationWindow mainWindow

    property string globalFont: App.Style.fontFamily
    property color backgroundColor: App.Style.obdBoxBackground
    property color accentColor: App.Style.obdBarColor
    property color textColor: App.Style.obdValueColor
    property color labelColor: App.Style.obdLabelColor
    property color cardColor: Qt.darker(backgroundColor, 0.85)

    readonly property bool available: typeof tripRecorder !== "undefined" && tripRecorder !== null
    readonly property var trips: available ? tripRecorder.trips : []
    readonly property bool recording: available && tripRecorder.recording

    // Selection
    property var selectedTrip: null          // list item (summary) or null
    property var detail: ({})                // full summary with params + events
    property string selectedParam: ""
    property var seriesPoints: []
    property string notice: ""

    // Chart time window in seconds since trip start; -1 / -1 = the whole trip.
    // Pinch, drag and wheel change it; every change re-queries the store for
    // just that window (360 buckets), so zooming in reveals detail instead of
    // stretching the overview.
    property real viewFrom: -1
    property real viewTo: -1
    readonly property bool zoomed: viewFrom >= 0 || viewTo >= 0
    readonly property real tripDurationS: {
        if (detail && detail.durationS !== undefined && detail.durationS > 0) return detail.durationS
        if (selectedTrip && selectedTrip.open) return Math.max(1, liveElapsedS)
        return selectedTrip ? Math.max(1, selectedTrip.durationS || 1) : 1
    }
    onViewFromChanged: { seriesFetch.restart(); chart.requestPaint() }
    onViewToChanged: { seriesFetch.restart(); chart.requestPaint() }
    Timer {
        id: seriesFetch
        interval: 120
        onTriggered: tripHistoryPage.fetchSeries()
    }
    function fetchSeries() {
        if (!selectedTrip || selectedParam === "" || !available) return
        tripRecorder.requestSeries(selectedTrip.id, selectedParam, 360, viewFrom, viewTo)
    }
    function resetWindow() { viewFrom = -1; viewTo = -1 }
    // Set the visible window, clamped to the trip and never narrower than 5 s
    function setWindow(from, to) {
        var dur = tripDurationS
        var span = Math.max(5, Math.min(dur, to - from))
        from = Math.max(0, Math.min(from, dur - span))
        to = from + span
        if (from <= 0.01 && to >= dur - 0.01) { resetWindow(); return }
        viewFrom = from
        viewTo = to
    }
    // Zoom by `factor` (>1 in) keeping the time under `anchorFrac` (0..1 across the plot) fixed
    function zoomAt(factor, anchorFrac) {
        var from = zoomed ? viewFrom : 0, to = zoomed ? viewTo : tripDurationS
        var anchorT = from + (to - from) * anchorFrac
        var span = (to - from) / factor
        setWindow(anchorT - span * anchorFrac, anchorT + span * (1 - anchorFrac))
    }

    // ── Helpers ──────────────────────────────────────────────────────
    function fmtDuration(s) {
        s = Math.max(0, Math.round(s || 0))
        var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60
        if (h > 0) return h + "h " + (m < 10 ? "0" : "") + m + "m"
        return m + "m " + (sec < 10 ? "0" : "") + sec + "s"
    }
    function fmtClock(s) {
        s = Math.max(0, Math.round(s || 0))
        var m = Math.floor(s / 60), sec = s % 60
        return m + ":" + (sec < 10 ? "0" : "") + sec
    }
    function fmtDate(ms) { return ms ? Qt.formatDateTime(new Date(ms), "ddd d MMM yyyy, HH:mm") : "" }
    function fmtShortDate(ms) { return ms ? Qt.formatDateTime(new Date(ms), "ddd d MMM HH:mm") : "" }
    function fmtTime(ms) { return ms ? Qt.formatDateTime(new Date(ms), "HH:mm") : "" }
    function fmtBytes(b) {
        b = b || 0
        if (b >= 1024 * 1024 * 1024) return (b / (1024 * 1024 * 1024)).toFixed(2) + " GB"
        if (b >= 1024 * 1024) return (b / (1024 * 1024)).toFixed(1) + " MB"
        if (b >= 1024) return Math.round(b / 1024) + " KB"
        return b + " B"
    }
    function fmtNum(v, decimals) {
        if (v === undefined || v === null || v === "" || isNaN(v)) return "—"
        return Number(v).toFixed(decimals === undefined ? 0 : decimals)
    }
    readonly property var _fullTitles: {
        var t = {}
        var all = App.OBDParameterModel.allParameters || []
        for (var i = 0; i < all.length; i++) t[all[i].id] = all[i].title
        return t
    }
    function paramTitle(id) { return _fullTitles[id] || App.OBDParameterModel.getParamInfo(id).title || id }
    function paramUnit(id) { return App.OBDParameterModel.getParamInfo(id).unit || "" }
    function paramDecimals(id) { return App.OBDParameterModel.decimalsFor ? App.OBDParameterModel.decimalsFor(id) : 1 }

    function selectTrip(item) {
        if (!item) { selectedTrip = null; detail = {}; seriesPoints = []; resetWindow(); return }
        var changed = !selectedTrip || selectedTrip.id !== item.id
        selectedTrip = item
        if (changed) { detail = {}; seriesPoints = []; resetWindow() }
        tripRecorder.requestDetail(item.id)
        fetchSeries()
    }
    function selectParam(id) {
        selectedParam = id
        seriesPoints = []
        fetchSeries()
    }
    // Keep the selection pointing at fresh list data (the live trip's sample
    // count moves; a deleted trip drops out)
    onTripsChanged: {
        if (!selectedTrip) return
        for (var i = 0; i < trips.length; i++) {
            if (trips[i].id === selectedTrip.id) { selectedTrip = trips[i]; return }
        }
        selectTrip(null)
    }

    Connections {
        target: tripHistoryPage.available ? tripRecorder : null
        function onDetailReady(id, d) {
            if (!tripHistoryPage.selectedTrip || id !== tripHistoryPage.selectedTrip.id) return
            tripHistoryPage.detail = d
            var params = d.params || []
            // Default to speed, else RPM, else the first recorded parameter
            if (tripHistoryPage.selectedParam === "" || !params.some(function(p) { return p.id === tripHistoryPage.selectedParam })) {
                var pick = ""
                for (var i = 0; i < params.length && pick === ""; i++)
                    if (params[i].id === "SPEED") pick = "SPEED"
                for (var j = 0; j < params.length && pick === ""; j++)
                    if (params[j].id === "RPM") pick = "RPM"
                if (pick === "" && params.length > 0) pick = params[0].id
                if (pick !== "") tripHistoryPage.selectParam(pick)
            }
        }
        function onSeriesReady(id, paramId, points) {
            if (!tripHistoryPage.selectedTrip || id !== tripHistoryPage.selectedTrip.id) return
            if (paramId !== tripHistoryPage.selectedParam) return
            tripHistoryPage.seriesPoints = points
        }
        function onExportFinished(id, path) {
            tripHistoryPage.notice = path ? "Exported to " + path : "Export failed"
            noticeTimer.restart()
        }
        function onTripEnded(id, summary) {
            if (tripHistoryPage.selectedTrip && tripHistoryPage.selectedTrip.id === id)
                tripHistoryPage.selectTrip(tripHistoryPage.selectedTrip)
        }
    }
    Timer { id: noticeTimer; interval: 6000; onTriggered: tripHistoryPage.notice = "" }
    // The live trip keeps growing: refresh its detail and chart while shown
    Timer {
        interval: 5000; repeat: true
        running: tripHistoryPage.visible && tripHistoryPage.selectedTrip !== null && tripHistoryPage.selectedTrip.open === true
        onTriggered: tripHistoryPage.selectTrip(tripHistoryPage.selectedTrip)
    }
    // Elapsed clock for the recording pill
    property int liveElapsedS: 0
    Timer {
        interval: 1000; repeat: true; running: tripHistoryPage.recording && tripHistoryPage.visible
        triggeredOnStart: true
        onTriggered: tripHistoryPage.liveElapsedS = Math.round((Date.now() - tripRecorder.currentTripStartMs) / 1000)
    }
    Component.onCompleted: {
        if (available) {
            tripRecorder.refresh()
            if (trips.length > 0) selectTrip(trips[0])
        }
    }
    StackView.onActivated: if (available) tripRecorder.refresh()

    Rectangle {
        anchors.fill: parent
        color: backgroundColor

        RowLayout {
            anchors.fill: parent
            anchors.margins: dp(16)
            spacing: dp(16)

            // ── Left: drive list ─────────────────────────────────────
            Rectangle {
                Layout.preferredWidth: Math.round(parent.width * 0.36)
                Layout.fillHeight: true
                radius: dpMin(12, 4)
                color: cardColor

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: dp(12)
                    spacing: dp(8)

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Trip History"
                            color: textColor
                            font.pixelSize: App.Spacing.overallText * 1.1
                            font.bold: true
                            font.family: globalFont
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        // Recording pill
                        Rectangle {
                            radius: height / 2
                            color: recording ? Qt.rgba(1, 0.27, 0.27, 0.18) : Qt.darker(backgroundColor, 1.2)
                            border.color: recording ? "#FF4444" : labelColor
                            border.width: 1
                            implicitWidth: pillRow.implicitWidth + dp(16)
                            implicitHeight: pillRow.implicitHeight + dp(6)
                            RowLayout {
                                id: pillRow
                                anchors.centerIn: parent
                                spacing: dp(6)
                                Rectangle {
                                    width: dp(8); height: dp(8); radius: dp(4)
                                    color: recording ? "#FF4444" : labelColor
                                    SequentialAnimation on opacity {
                                        running: recording && tripHistoryPage.visible
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.3; duration: 700 }
                                        NumberAnimation { to: 1.0; duration: 700 }
                                    }
                                }
                                Text {
                                    text: !available || !tripRecorder.storeReady ? "Unavailable"
                                        : !tripRecorder.enabled ? "Off"
                                        : recording ? "REC " + fmtClock(liveElapsedS) + " · " + tripRecorder.currentSampleCount
                                        : "Waiting for engine"
                                    color: textColor
                                    font.pixelSize: App.Spacing.overallText * 0.75
                                    font.family: globalFont
                                    font.bold: recording
                                }
                            }
                        }
                    }

                    ListView {
                        id: tripList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: dp(6)
                        model: tripHistoryPage.trips
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { }

                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool selected: tripHistoryPage.selectedTrip !== null
                                                             && tripHistoryPage.selectedTrip.id === modelData.id
                            width: tripList.width - dp(8)
                            height: rowCol.implicitHeight + dp(16)
                            radius: dpMin(8, 3)
                            color: selected ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.22)
                                 : rowMouse.containsMouse ? Qt.lighter(cardColor, 1.15) : Qt.darker(backgroundColor, 1.05)
                            border.color: selected ? accentColor : "transparent"
                            border.width: selected ? 2 : 0

                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: tripHistoryPage.selectTrip(modelData)
                            }

                            ColumnLayout {
                                id: rowCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: dp(10)
                                spacing: dp(2)

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: fmtShortDate(modelData.startMs)
                                        color: textColor
                                        font.pixelSize: App.Spacing.overallText * 0.85
                                        font.bold: true
                                        font.family: globalFont
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Rectangle {
                                        visible: modelData.open === true || modelData.clean === false
                                        radius: dpMin(4, 2)
                                        color: modelData.open === true ? "#FF4444" : Qt.rgba(1, 0.67, 0, 0.8)
                                        implicitWidth: badge.implicitWidth + dp(10)
                                        implicitHeight: badge.implicitHeight + dp(4)
                                        Text {
                                            id: badge
                                            anchors.centerIn: parent
                                            text: modelData.open === true ? "LIVE" : "RECOVERED"
                                            color: "white"
                                            font.pixelSize: App.Spacing.overallText * 0.6
                                            font.bold: true
                                            font.family: globalFont
                                        }
                                    }
                                }
                                Text {
                                    text: modelData.open === true
                                          ? "Recording · " + modelData.sampleCount + " samples"
                                          : fmtDuration(modelData.durationS) + " · "
                                            + fmtNum(modelData.distanceMiles, 1) + " mi · "
                                            + fmtNum(modelData.maxSpeed, 0) + " mph max"
                                    color: labelColor
                                    font.pixelSize: App.Spacing.overallText * 0.75
                                    font.family: globalFont
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: tripList.count === 0
                            width: parent.width - dp(32)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: !available ? "The trip recorder is not available."
                                : !tripRecorder.storeReady ? "The black box could not open its database, so nothing is recorded.\n" + tripRecorder.lastError
                                : !tripRecorder.enabled ? "Black box recording is off.\nTurn it on in Settings → OBD → Black Box."
                                : "No drives recorded yet.\nA trip starts on the first RPM reading after the adapter connects."
                            color: labelColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: globalFont
                        }
                    }

                    // Storage footer
                    Text {
                        Layout.fillWidth: true
                        text: available ? trips.length + (trips.length === 1 ? " drive · " : " drives · ") + fmtBytes(tripRecorder.storageBytes)
                                        + " of " + (settingsManager ? settingsManager.blackBoxMaxMB : 500) + " MB"
                                        : ""
                        color: labelColor
                        font.pixelSize: App.Spacing.overallText * 0.7
                        font.family: globalFont
                        elide: Text.ElideRight
                    }
                }
            }

            // ── Right: selected drive ────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: dpMin(12, 4)
                color: cardColor

                Text {
                    anchors.centerIn: parent
                    visible: tripHistoryPage.selectedTrip === null
                    text: "Select a drive"
                    color: labelColor
                    font.pixelSize: App.Spacing.overallText
                    font.family: globalFont
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: dp(12)
                    spacing: dp(8)
                    visible: tripHistoryPage.selectedTrip !== null

                    // Title row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: dp(8)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                text: selectedTrip ? fmtShortDate(selectedTrip.startMs)
                                                     + (selectedTrip.open ? " → now" : " → " + fmtTime(selectedTrip.endMs)) : ""
                                color: textColor
                                font.pixelSize: App.Spacing.overallText * 0.95
                                font.bold: true
                                font.family: globalFont
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: {
                                    if (!selectedTrip) return ""
                                    var parts = []
                                    if (selectedTrip.modelYear > 0 || selectedTrip.make)
                                        parts.push([selectedTrip.modelYear > 0 ? selectedTrip.modelYear : "", selectedTrip.make].join(" ").trim())
                                    if (selectedTrip.vin) parts.push("VIN " + selectedTrip.vin)
                                    if (selectedTrip.clean === false) parts.push("recovered after a power cut")
                                    return parts.join("  ·  ")
                                }
                                visible: text !== ""
                                color: labelColor
                                font.pixelSize: App.Spacing.overallText * 0.7
                                font.family: globalFont
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        TripActionButton {
                            text: "End trip"
                            visible: selectedTrip !== null && selectedTrip.open === true
                            onClicked: tripRecorder.endTripNow()
                        }
                        TripActionButton {
                            text: "Export"
                            visible: selectedTrip !== null && selectedTrip.open !== true
                            onClicked: tripRecorder.exportTrip(selectedTrip.id)
                        }
                        TripActionButton {
                            text: "Delete"
                            danger: true
                            visible: selectedTrip !== null
                            onClicked: deleteConfirm.open()
                        }
                    }

                    // Stat tiles
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        rowSpacing: dp(6)
                        columnSpacing: dp(6)
                        Repeater {
                            model: {
                                var d = tripHistoryPage.detail || {}
                                var t = tripHistoryPage.selectedTrip || {}
                                var dur = d.durationS !== undefined ? d.durationS
                                        : (t.open ? tripHistoryPage.liveElapsedS : t.durationS)
                                var fuel = d.fuelUsedPct !== undefined ? d.fuelUsedPct : t.fuelUsedPct
                                return [
                                    { label: "Duration", value: fmtDuration(dur) },
                                    { label: "Distance", value: fmtNum(d.distanceMiles !== undefined ? d.distanceMiles : t.distanceMiles, 1) + " mi" },
                                    { label: "Max speed", value: fmtNum(d.maxSpeed !== undefined ? d.maxSpeed : t.maxSpeed, 0) + " mph" },
                                    { label: "Max RPM", value: fmtNum(d.maxRpm !== undefined ? d.maxRpm : t.maxRpm, 0) },
                                    { label: "Peak coolant", value: fmtNum(d.maxCoolant !== undefined ? d.maxCoolant : t.maxCoolant, 0) + " °C" },
                                    { label: "Voltage", value: fmtNum(d.minVoltage !== undefined ? d.minVoltage : t.minVoltage, 1) + " – "
                                                               + fmtNum(d.maxVoltage !== undefined ? d.maxVoltage : t.maxVoltage, 1) + " V" },
                                    { label: "Fuel used", value: fuel === undefined || fuel === null ? "—" : fmtNum(fuel, 1) + " %" },
                                    { label: "Samples", value: (d.sampleCount !== undefined ? d.sampleCount : t.sampleCount) + " · "
                                                               + (d.paramCount !== undefined ? d.paramCount : "?") + " PIDs" }
                                ]
                            }
                            delegate: Rectangle {
                                id: statTile
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                implicitHeight: statCol.implicitHeight + dp(12)
                                radius: dpMin(6, 2)
                                color: Qt.darker(backgroundColor, 1.08)
                                ColumnLayout {
                                    id: statCol
                                    anchors.centerIn: parent
                                    spacing: 0
                                    Text {
                                        text: modelData.label
                                        color: labelColor
                                        font.pixelSize: App.Spacing.overallText * 0.65
                                        font.family: globalFont
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Text {
                                        text: modelData.value
                                        color: textColor
                                        font.pixelSize: App.Spacing.overallText * 0.85
                                        minimumPixelSize: 8
                                        fontSizeMode: Text.HorizontalFit
                                        font.bold: true
                                        font.family: globalFont
                                        horizontalAlignment: Text.AlignHCenter
                                        Layout.alignment: Qt.AlignHCenter
                                        Layout.preferredWidth: statTile.width - dp(10)
                                    }
                                }
                            }
                        }
                    }

                    // Parameter chips
                    ListView {
                        id: paramChips
                        Layout.fillWidth: true
                        Layout.preferredHeight: dp(32)
                        orientation: ListView.Horizontal
                        spacing: dp(6)
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: (tripHistoryPage.detail && tripHistoryPage.detail.params) ? tripHistoryPage.detail.params : []
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool active: modelData.id === tripHistoryPage.selectedParam
                            height: paramChips.height
                            width: chipText.implicitWidth + dp(20)
                            radius: height / 2
                            color: active ? accentColor : Qt.darker(backgroundColor, 1.1)
                            border.color: active ? accentColor : labelColor
                            border.width: 1
                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                text: paramTitle(modelData.id)
                                color: active ? backgroundColor : textColor
                                font.pixelSize: App.Spacing.overallText * 0.72
                                font.bold: active
                                font.family: globalFont
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: tripHistoryPage.selectParam(modelData.id)
                            }
                        }
                    }

                    // Chart
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: dpMin(8, 3)
                        color: Qt.darker(backgroundColor, 1.15)

                        readonly property var stat: {
                            var params = (tripHistoryPage.detail && tripHistoryPage.detail.params) ? tripHistoryPage.detail.params : []
                            for (var i = 0; i < params.length; i++)
                                if (params[i].id === tripHistoryPage.selectedParam) return params[i]
                            return null
                        }

                        RowLayout {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: dp(8)
                            Text {
                                text: selectedParam ? paramTitle(selectedParam) + (paramUnit(selectedParam) ? "  (" + paramUnit(selectedParam) + ")" : "") : ""
                                color: textColor
                                font.pixelSize: App.Spacing.overallText * 0.8
                                font.bold: true
                                font.family: globalFont
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.maximumWidth: parent.width * 0.6
                                elide: Text.ElideRight
                                text: parent.parent.stat ? "min " + fmtNum(parent.parent.stat.min, paramDecimals(selectedParam))
                                                           + "   avg " + fmtNum(parent.parent.stat.avg, paramDecimals(selectedParam))
                                                           + "   max " + fmtNum(parent.parent.stat.max, paramDecimals(selectedParam))
                                                           + "   " + parent.parent.stat.count + " samples" : ""
                                color: labelColor
                                font.pixelSize: App.Spacing.overallText * 0.65
                                font.family: globalFont
                            }
                        }

                        Canvas {
                            id: chart
                            anchors.fill: parent
                            anchors.topMargin: dp(30)
                            anchors.margins: dp(8)

                            property var points: tripHistoryPage.seriesPoints
                            property color lineColor: accentColor
                            property color bandColor: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.25)
                            property color axisColor: labelColor
                            property string font: globalFont
                            property int fontPx: Math.max(8, Math.round(App.Spacing.overallText * 0.6))
                            // Plot rectangle, shared with the gesture handlers
                            readonly property real plotLeft: fontPx * 3.4
                            readonly property real plotRight: 6
                            readonly property real plotWidth: Math.max(1, width - plotLeft - plotRight)
                            readonly property real winFrom: tripHistoryPage.zoomed ? tripHistoryPage.viewFrom : 0
                            readonly property real winTo: tripHistoryPage.zoomed ? tripHistoryPage.viewTo : tripHistoryPage.tripDurationS

                            onPointsChanged: requestPaint()
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            onLineColorChanged: requestPaint()
                            onWinFromChanged: requestPaint()
                            onWinToChanged: requestPaint()

                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                var pts = points || []
                                var left = plotLeft, bottom = fontPx * 1.8, top = fontPx * 0.7, right = plotRight
                                var w = width - left - right, h = height - top - bottom
                                if (w <= 10 || h <= 10) return
                                ctx.font = fontPx + "px " + font
                                ctx.fillStyle = axisColor
                                ctx.strokeStyle = axisColor
                                ctx.lineWidth = 1
                                var t0 = winFrom, t1 = Math.max(winFrom + 1, winTo)
                                // Only the buckets inside the window set the value range
                                var lo = Infinity, hi = -Infinity, shown = 0
                                for (var i = 0; i < pts.length; i++) {
                                    if (pts[i].t < t0 || pts[i].t > t1) continue
                                    lo = Math.min(lo, pts[i].lo); hi = Math.max(hi, pts[i].hi); shown++
                                }
                                if (shown < 2) {
                                    ctx.textAlign = "center"
                                    ctx.textBaseline = "middle"
                                    ctx.fillText(pts.length === 0 ? "No samples" : "Not enough samples here", left + w / 2, top + h / 2)
                                    return
                                }
                                var span = hi - lo
                                if (span < 1e-6) { span = Math.max(1, Math.abs(hi) * 0.1); lo -= span / 2; hi += span / 2 }
                                else { lo -= span * 0.05; hi += span * 0.05 }
                                span = hi - lo
                                var X = function(t) { return left + ((t - t0) / (t1 - t0)) * w }
                                var Y = function(v) { return top + h - ((v - lo) / span) * h }

                                // Axes + grid
                                ctx.globalAlpha = 0.35
                                for (var g = 0; g <= 4; g++) {
                                    var gy = top + (h * g) / 4
                                    ctx.beginPath(); ctx.moveTo(left, gy); ctx.lineTo(left + w, gy); ctx.stroke()
                                }
                                ctx.globalAlpha = 1
                                ctx.textAlign = "right"
                                ctx.textBaseline = "middle"
                                for (var k = 0; k <= 4; k++) {
                                    var v = hi - (span * k) / 4
                                    ctx.fillText(Number(v).toFixed(span < 5 ? 1 : 0), left - 4, top + (h * k) / 4)
                                }
                                ctx.textAlign = "center"
                                ctx.textBaseline = "top"
                                for (var x = 0; x <= 4; x++) {
                                    var t = t0 + ((t1 - t0) * x) / 4
                                    ctx.fillText(tripHistoryPage.fmtClock(t), X(t), top + h + 4)
                                }

                                // Clip the traces to the plot so a window mid-fetch never spills over the axes
                                ctx.save()
                                ctx.beginPath()
                                ctx.rect(left, top, w, h)
                                ctx.clip()

                                // Min–max band
                                ctx.beginPath()
                                ctx.moveTo(X(pts[0].t), Y(pts[0].hi))
                                for (var a = 1; a < pts.length; a++) ctx.lineTo(X(pts[a].t), Y(pts[a].hi))
                                for (var b = pts.length - 1; b >= 0; b--) ctx.lineTo(X(pts[b].t), Y(pts[b].lo))
                                ctx.closePath()
                                ctx.fillStyle = bandColor
                                ctx.fill()

                                // Average line
                                ctx.beginPath()
                                ctx.moveTo(X(pts[0].t), Y(pts[0].v))
                                for (var c = 1; c < pts.length; c++) ctx.lineTo(X(pts[c].t), Y(pts[c].v))
                                ctx.strokeStyle = lineColor
                                ctx.lineWidth = 2
                                ctx.lineJoin = "round"
                                ctx.stroke()
                                ctx.restore()
                            }

                            // ── Gestures: pinch to zoom, drag to pan, wheel to zoom, double-tap to reset ──
                            property real _gestureFrom: 0
                            property real _gestureTo: 0
                            function _beginGesture() {
                                _gestureFrom = winFrom
                                _gestureTo = winTo
                            }

                            PinchHandler {
                                id: pinch
                                target: null
                                minimumRotation: 0
                                maximumRotation: 0
                                onActiveChanged: if (active) chart._beginGesture()
                                onActiveScaleChanged: {
                                    if (!active) return
                                    var span0 = chart._gestureTo - chart._gestureFrom
                                    var frac = Math.max(0, Math.min(1, (centroid.pressPosition.x - chart.plotLeft) / chart.plotWidth))
                                    var anchorT = chart._gestureFrom + span0 * frac
                                    var span = span0 / Math.max(0.05, activeScale)
                                    // Pan with the centroid too
                                    var shift = -(centroid.position.x - centroid.pressPosition.x) / chart.plotWidth * span
                                    tripHistoryPage.setWindow(anchorT - span * frac + shift, anchorT + span * (1 - frac) + shift)
                                }
                            }
                            DragHandler {
                                id: pan
                                target: null
                                xAxis.enabled: true
                                yAxis.enabled: false
                                enabled: tripHistoryPage.zoomed
                                onActiveChanged: if (active) chart._beginGesture()
                                onTranslationChanged: {
                                    if (!active) return
                                    var span = chart._gestureTo - chart._gestureFrom
                                    var shift = -translation.x / chart.plotWidth * span
                                    tripHistoryPage.setWindow(chart._gestureFrom + shift, chart._gestureTo + shift)
                                }
                            }
                            WheelHandler {
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                onWheel: function(event) {
                                    var frac = Math.max(0, Math.min(1, (point.position.x - chart.plotLeft) / chart.plotWidth))
                                    tripHistoryPage.zoomAt(event.angleDelta.y > 0 ? 1.25 : 1 / 1.25, frac)
                                }
                            }
                            TapHandler {
                                onDoubleTapped: tripHistoryPage.resetWindow()
                            }
                        }

                        // Window readout + reset, shown while zoomed
                        Rectangle {
                            visible: tripHistoryPage.zoomed
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: dp(10)
                            anchors.bottomMargin: dp(10) + chart.fontPx * 2   // clear of the time labels
                            radius: dpMin(6, 2)
                            color: Qt.rgba(0, 0, 0, 0.45)
                            border.color: accentColor
                            border.width: 1
                            implicitWidth: zoomText.implicitWidth + dp(16)
                            implicitHeight: zoomText.implicitHeight + dp(8)
                            Text {
                                id: zoomText
                                anchors.centerIn: parent
                                text: fmtClock(tripHistoryPage.viewFrom) + " – " + fmtClock(tripHistoryPage.viewTo) + "   ✕"
                                color: textColor
                                font.pixelSize: App.Spacing.overallText * 0.65
                                font.family: globalFont
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: tripHistoryPage.resetWindow()
                            }
                        }
                    }

                    // Events
                    Text {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: {
                            var ev = (tripHistoryPage.detail && tripHistoryPage.detail.events) ? tripHistoryPage.detail.events : []
                            var out = []
                            for (var i = 0; i < ev.length && out.length < 6; i++) {
                                if (ev[i].kind === "trip_end") continue
                                out.push(fmtClock(ev[i].t) + " " + ev[i].kind + (ev[i].detail ? " " + ev[i].detail : ""))
                            }
                            return out.length ? "Events: " + out.join("  ·  ") : ""
                        }
                        color: labelColor
                        font.pixelSize: App.Spacing.overallText * 0.65
                        font.family: globalFont
                        elide: Text.ElideRight
                    }

                    // File path / notice
                    Text {
                        Layout.fillWidth: true
                        text: tripHistoryPage.notice !== "" ? tripHistoryPage.notice
                            : (selectedTrip && selectedTrip.file) ? selectedTrip.file : ""
                        visible: text !== ""
                        color: tripHistoryPage.notice !== "" ? accentColor : labelColor
                        font.pixelSize: App.Spacing.overallText * 0.62
                        font.family: globalFont
                        elide: Text.ElideMiddle
                    }
                }
            }
        }
    }

    // Small accent-outline button used by the page
    component TripActionButton: Rectangle {
        id: btn
        property string text: ""
        property bool danger: false
        signal clicked()
        implicitWidth: btnText.implicitWidth + dp(20)
        implicitHeight: btnText.implicitHeight + dp(10)
        radius: dpMin(6, 2)
        color: btnMouse.pressed ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.3) : "transparent"
        border.color: danger ? "#FF6666" : accentColor
        border.width: 1
        Text {
            id: btnText
            anchors.centerIn: parent
            text: btn.text
            color: btn.danger ? "#FF6666" : accentColor
            font.pixelSize: App.Spacing.overallText * 0.75
            font.bold: true
            font.family: tripHistoryPage.globalFont
        }
        MouseArea {
            id: btnMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.clicked()
        }
    }

    Dialog {
        id: deleteConfirm
        title: "Delete this drive?"
        anchors.centerIn: parent
        width: dp(400)
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: App.Style.backgroundColor
            radius: dpMin(8, 2)
            border.color: accentColor
            border.width: 2
        }
        contentItem: Text {
            text: selectedTrip ? fmtDate(selectedTrip.startMs) + "\n\nIts samples, summary and files are removed for good." : ""
            font.pixelSize: dp(15)
            font.family: globalFont
            color: textColor
            wrapMode: Text.WordWrap
        }
        onAccepted: if (selectedTrip) tripRecorder.deleteTrip(selectedTrip.id)
    }
}
