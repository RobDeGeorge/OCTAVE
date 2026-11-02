import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App

// Trip History -- the black box viewer, two views:
//   Drives: left, every recorded drive, newest first (the one being recorded
//     on top, live); right, the selected drive's summary and a chart of any
//     recorded parameter.
//   Trends: left, the car's health numbers across drives (warm-up time,
//     running coolant, voltages, idle, fuel trims), each with a sparkline and
//     a verdict; right, the selected number (or any recorded parameter) per
//     drive over 7 / 30 / 90 days or everything, a tap opening that drive.
// Data comes from tripRecorder (TripRecorder, C++); the files it lists are
// the same CSV + JSON a person can pull off the box.
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

    // "drives" or "trends"
    property string view: "drives"

    // Trends: the window, the answer to requestHealthTrends, and what the
    // big chart shows (a health metric or any recorded parameter)
    property int trendDays: 30
    property var trends: ({})
    property string trendKind: "metric"      // "metric" | "param"
    property string trendId: ""
    property var paramRows: []
    readonly property var trendMetrics: trends && trends.metrics ? trends.metrics : []
    readonly property var trendParams: trends && trends.params ? trends.params : []
    readonly property var trendMetric: {
        if (trendKind !== "metric") return null
        for (var i = 0; i < trendMetrics.length; i++)
            if (trendMetrics[i].id === trendId) return trendMetrics[i]
        return null
    }
    readonly property var trendRows: trendKind === "metric" ? (trendMetric ? trendMetric.rows : []) : paramRows

    function setView(v) {
        view = v
        if (v === "trends") fetchTrends()
    }
    function fetchTrends() {
        if (!available) return
        tripRecorder.requestHealthTrends(trendDays)
        if (trendKind === "param" && trendId !== "") tripRecorder.requestParamTrend(trendId, trendDays)
    }
    function setTrendDays(d) {
        if (d === trendDays) return
        trendDays = d
        fetchTrends()
    }
    function selectTrend(kind, id) {
        trendKind = kind
        trendId = id
        paramRows = []
        if (kind === "param" && available) tripRecorder.requestParamTrend(id, trendDays)
    }
    // A point on the trend chart: open that drive
    function openTripFromTrend(id) {
        for (var i = 0; i < trips.length; i++) {
            if (trips[i].id === id) { view = "drives"; selectTrip(trips[i]); tripList.positionViewAtIndex(i, ListView.Contain); return }
        }
    }
    function trendValue(v, decimals, unit) {
        if (v === undefined || v === null || isNaN(v)) return "—"
        return Number(v).toFixed(decimals) + (unit === "%" ? "%" : unit ? " " + unit : "")
    }
    function statusColor(status) {
        return status === "watch" ? App.Style.statusWarning : status === "ok" ? App.Style.statusSuccess : labelColor
    }
    // Trips change when a drive ends (or one is deleted): the trends follow, once things settle
    Timer {
        id: trendRefetch
        interval: 800
        onTriggered: tripHistoryPage.fetchTrends()
    }

    // Selection
    property var selectedTrip: null          // list item (summary) or null
    property var detail: ({})                // full summary with params + events
    property string selectedParam: ""
    property var seriesPoints: []

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
    // The recorded unit, not the gauges' display unit: the store, the CSV
    // and this page's chart and stats hold the parameter's own values
    function paramUnit(id) { return App.OBDParameterModel.nativeUnit(id) }
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
        if (view === "trends") trendRefetch.restart()
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
        function onHealthTrendsReady(days, result) {
            if (days !== tripHistoryPage.trendDays) return
            tripHistoryPage.trends = result
            // First look: the first number that needs a look, else the first with data
            if (tripHistoryPage.trendId === "") {
                var ms = result.metrics || [], pick = ""
                for (var i = 0; i < ms.length && pick === ""; i++)
                    if (ms[i].status === "watch") pick = ms[i].id
                for (var j = 0; j < ms.length && pick === ""; j++)
                    if (ms[j].rows.length > 0) pick = ms[j].id
                if (pick !== "") tripHistoryPage.selectTrend("metric", pick)
            }
        }
        function onParamTrendReady(paramId, days, rows) {
            if (days !== tripHistoryPage.trendDays || tripHistoryPage.trendKind !== "param"
                    || paramId !== tripHistoryPage.trendId) return
            tripHistoryPage.paramRows = rows
        }
        function onTripEnded(id, summary) {
            if (tripHistoryPage.selectedTrip && tripHistoryPage.selectedTrip.id === id)
                tripHistoryPage.selectTrip(tripHistoryPage.selectedTrip)
        }
        // The live trip grew (at most every 5 s): refresh its detail and chart while shown
        function onLiveTripChanged(id) {
            if (tripHistoryPage.visible && tripHistoryPage.selectedTrip && tripHistoryPage.selectedTrip.id === id)
                tripHistoryPage.selectTrip(tripHistoryPage.selectedTrip)
        }
    }
    // Elapsed clock for the recording pill
    readonly property int liveElapsedS: available ? tripRecorder.currentTripElapsedS : 0
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

                    // Drives | Trends
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: dpMin(34, 28)
                        radius: height / 2
                        color: Qt.darker(backgroundColor, 1.1)
                        border.color: Qt.rgba(labelColor.r, labelColor.g, labelColor.b, 0.5)
                        border.width: 1
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 3
                            spacing: 3
                            Repeater {
                                model: [{ id: "drives", label: "Drives" }, { id: "trends", label: "Trends" }]
                                delegate: Rectangle {
                                    required property var modelData
                                    readonly property bool active: tripHistoryPage.view === modelData.id
                                    objectName: "tripView_" + modelData.id
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: height / 2
                                    color: active ? accentColor : "transparent"
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: active ? backgroundColor : textColor
                                        font.pixelSize: App.Spacing.overallText * 0.75
                                        font.bold: active
                                        font.family: globalFont
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: tripHistoryPage.setView(modelData.id)
                                    }
                                }
                            }
                        }
                    }

                    ListView {
                        id: tripList
                        visible: tripHistoryPage.view === "drives"
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
                                          ? "Recording · " + tripRecorder.currentSampleCount + " samples"   // live: the recorder's count, not the list's snapshot
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

                    // Health numbers across drives
                    ListView {
                        id: metricList
                        objectName: "tripTrendMetrics"
                        visible: tripHistoryPage.view === "trends"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: dp(6)
                        // All "none" with no drives: the empty note says so instead
                        model: tripHistoryPage.trends.tripCount > 0 ? tripHistoryPage.trendMetrics : []
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { }

                        delegate: Rectangle {
                            id: metricCard
                            required property var modelData
                            readonly property bool selected: tripHistoryPage.trendKind === "metric"
                                                             && tripHistoryPage.trendId === modelData.id
                            objectName: "tripTrendMetric_" + modelData.id
                            readonly property bool empty: modelData.rows.length === 0
                            width: metricList.width - dp(8)
                            height: metricCol.implicitHeight + dp(14)
                            radius: dpMin(8, 3)
                            opacity: empty ? 0.55 : 1.0
                            color: selected ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.22)
                                 : metricMouse.containsMouse ? Qt.lighter(cardColor, 1.15) : Qt.darker(backgroundColor, 1.05)
                            border.color: selected ? accentColor : "transparent"
                            border.width: selected ? 2 : 0

                            MouseArea {
                                id: metricMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: tripHistoryPage.selectTrend("metric", modelData.id)
                            }

                            ColumnLayout {
                                id: metricCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: dp(10)
                                spacing: dp(3)

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: dp(6)
                                    Rectangle {
                                        width: dp(9); height: dp(9); radius: width / 2
                                        color: tripHistoryPage.statusColor(modelData.status)
                                    }
                                    Text {
                                        text: modelData.title
                                        color: textColor
                                        font.pixelSize: App.Spacing.overallText * 0.8
                                        font.bold: true
                                        font.family: globalFont
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: metricCard.empty ? "No data"
                                              : tripHistoryPage.trendValue(modelData.recent, modelData.decimals, modelData.unit)
                                        color: modelData.status === "watch" ? App.Style.statusWarning : textColor
                                        font.pixelSize: App.Spacing.overallText * 0.8
                                        font.bold: !metricCard.empty
                                        font.family: globalFont
                                    }
                                }
                                // One dot per drive, the usual range shaded
                                Canvas {
                                    id: spark
                                    visible: modelData.rows.length > 1
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: dp(26)
                                    property var rows: modelData.rows
                                    property color lineColor: modelData.status === "watch" ? App.Style.statusWarning : accentColor
                                    onRowsChanged: requestPaint()
                                    onWidthChanged: requestPaint()
                                    onLineColorChanged: requestPaint()
                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.reset()
                                        var r = rows || []
                                        if (r.length < 2 || width < 4) return
                                        var lo = Infinity, hi = -Infinity
                                        for (var i = 0; i < r.length; i++) { lo = Math.min(lo, r[i].v); hi = Math.max(hi, r[i].v) }
                                        // The data sets the scale, but never finer than a quarter of the
                                        // usual range (10 % of the value without one): noise stays flat.
                                        // The usual range is shaded where it falls.
                                        var bLo = modelData.bandLo, bHi = modelData.bandHi
                                        var minSpan = bLo !== undefined && bHi !== undefined ? (bHi - bLo) * 0.25
                                                                                         : Math.abs(hi + lo) / 2 * 0.1
                                        var span = Math.max(1e-6, hi - lo)
                                        if (span < minSpan) { var mid = (hi + lo) / 2; lo = mid - minSpan / 2; hi = mid + minSpan / 2; span = minSpan }
                                        lo -= span * 0.1; hi += span * 0.1; span = hi - lo
                                        var t0 = r[0].startMs, t1 = Math.max(t0 + 1, r[r.length - 1].startMs)
                                        var X = function(t) { return 3 + (t - t0) / (t1 - t0) * (width - 6) }
                                        var Y = function(v) { return height - 3 - (v - lo) / span * (height - 6) }
                                        if (bLo !== undefined || bHi !== undefined) {
                                            ctx.fillStyle = Qt.rgba(App.Style.statusSuccess.r, App.Style.statusSuccess.g, App.Style.statusSuccess.b, 0.12)
                                            var top = bHi === undefined ? 0 : Math.max(0, Y(bHi))
                                            var bottom = bLo === undefined ? height : Math.min(height, Y(bLo))
                                            if (bottom > top) ctx.fillRect(0, top, width, bottom - top)
                                        }
                                        ctx.strokeStyle = lineColor
                                        ctx.globalAlpha = 0.6
                                        ctx.lineWidth = 1.5
                                        ctx.beginPath()
                                        ctx.moveTo(X(r[0].startMs), Y(r[0].v))
                                        for (var j = 1; j < r.length; j++) ctx.lineTo(X(r[j].startMs), Y(r[j].v))
                                        ctx.stroke()
                                        ctx.globalAlpha = 1
                                        ctx.fillStyle = lineColor
                                        var last = r[r.length - 1]
                                        ctx.beginPath()
                                        ctx.arc(X(last.startMs), Y(last.v), 2.5, 0, Math.PI * 2)
                                        ctx.fill()
                                    }
                                }
                                Text {
                                    text: modelData.note
                                    color: modelData.status === "watch" ? App.Style.statusWarning : labelColor
                                    font.pixelSize: App.Spacing.overallText * 0.65
                                    font.family: globalFont
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: metricCard.selected ? 4 : 1
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: metricList.count === 0
                            width: parent.width - dp(32)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: tripHistoryPage.trends.tripCount === 0
                                  ? "No finished drives in this period.\nThe health numbers come from every drive the black box records."
                                  : "Loading…"
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
                    visible: tripHistoryPage.view === "drives" && tripHistoryPage.selectedTrip === null
                    text: "Select a drive"
                    color: labelColor
                    font.pixelSize: App.Spacing.overallText
                    font.family: globalFont
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: dp(12)
                    spacing: dp(8)
                    visible: tripHistoryPage.view === "drives" && tripHistoryPage.selectedTrip !== null

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

                }

                // ── Trends: one number (or parameter) across drives ──────
                ColumnLayout {
                    id: trendPanel
                    objectName: "tripTrendPanel"
                    anchors.fill: parent
                    anchors.margins: dp(12)
                    spacing: dp(8)
                    visible: tripHistoryPage.view === "trends"

                    readonly property string title: tripHistoryPage.trendKind === "metric"
                                                    ? (tripHistoryPage.trendMetric ? tripHistoryPage.trendMetric.title : "")
                                                    : paramTitle(tripHistoryPage.trendId)
                    readonly property string unit: tripHistoryPage.trendKind === "metric"
                                                   ? (tripHistoryPage.trendMetric ? tripHistoryPage.trendMetric.unit : "")
                                                   : paramUnit(tripHistoryPage.trendId)
                    readonly property int decimals: tripHistoryPage.trendKind === "metric"
                                                    ? (tripHistoryPage.trendMetric ? tripHistoryPage.trendMetric.decimals : 1)
                                                    : paramDecimals(tripHistoryPage.trendId)

                    // Title + window
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: dp(6)
                        Text {
                            text: trendPanel.title + (trendPanel.unit ? "  (" + trendPanel.unit + ")" : "")
                            color: textColor
                            font.pixelSize: App.Spacing.overallText * 0.95
                            font.bold: true
                            font.family: globalFont
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Repeater {
                            model: [{ d: 7, label: "7 d" }, { d: 30, label: "30 d" }, { d: 90, label: "90 d" }, { d: 0, label: "All" }]
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool active: tripHistoryPage.trendDays === modelData.d
                                objectName: "tripTrendDays_" + modelData.d
                                implicitWidth: dayText.implicitWidth + dp(16)
                                implicitHeight: dayText.implicitHeight + dp(8)
                                radius: height / 2
                                color: active ? accentColor : Qt.darker(backgroundColor, 1.1)
                                border.color: active ? accentColor : labelColor
                                border.width: 1
                                Text {
                                    id: dayText
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: active ? backgroundColor : textColor
                                    font.pixelSize: App.Spacing.overallText * 0.7
                                    font.bold: active
                                    font.family: globalFont
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: tripHistoryPage.setTrendDays(modelData.d)
                                }
                            }
                        }
                    }

                    // Verdict (a health number) or what the chart shows (a parameter)
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        text: {
                            var n = tripHistoryPage.trends.tripCount || 0
                            var drives = n + (n === 1 ? " drive" : " drives")
                                       + (tripHistoryPage.trendDays > 0 ? " in the last " + tripHistoryPage.trendDays + " days" : " in all")
                            if (tripHistoryPage.trendKind === "metric") {
                                var m = tripHistoryPage.trendMetric
                                return m ? m.note + "  ·  " + drives : drives
                            }
                            return "Lowest, average and highest on each drive  ·  " + drives
                        }
                        color: tripHistoryPage.trendKind === "metric" && tripHistoryPage.trendMetric
                               && tripHistoryPage.trendMetric.status === "watch" ? App.Style.statusWarning : labelColor
                        font.pixelSize: App.Spacing.overallText * 0.72
                        font.family: globalFont
                    }

                    // Any recorded parameter
                    ListView {
                        id: trendParamChips
                        Layout.fillWidth: true
                        Layout.preferredHeight: dp(30)
                        orientation: ListView.Horizontal
                        spacing: dp(6)
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: tripHistoryPage.trendParams
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool active: tripHistoryPage.trendKind === "param" && modelData.id === tripHistoryPage.trendId
                            objectName: "tripTrendParam_" + modelData.id
                            height: trendParamChips.height
                            width: trendChipText.implicitWidth + dp(18)
                            radius: height / 2
                            color: active ? accentColor : Qt.darker(backgroundColor, 1.1)
                            border.color: active ? accentColor : Qt.rgba(labelColor.r, labelColor.g, labelColor.b, 0.6)
                            border.width: 1
                            Text {
                                id: trendChipText
                                anchors.centerIn: parent
                                text: paramTitle(modelData.id)
                                color: active ? backgroundColor : textColor
                                font.pixelSize: App.Spacing.overallText * 0.68
                                font.bold: active
                                font.family: globalFont
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: tripHistoryPage.selectTrend("param", modelData.id)
                            }
                        }
                    }

                    // Chart: one point per drive over the window
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: dpMin(8, 3)
                        color: Qt.darker(backgroundColor, 1.15)

                        Canvas {
                            id: trendChart
                            objectName: "tripTrendChart"
                            anchors.fill: parent
                            anchors.margins: dp(8)

                            property var rows: tripHistoryPage.trendRows
                            readonly property bool isParam: tripHistoryPage.trendKind === "param"
                            readonly property var metric: tripHistoryPage.trendMetric
                            property color lineColor: accentColor
                            property color axisColor: labelColor
                            property color warnColor: App.Style.statusWarning
                            property color bandColor: App.Style.statusSuccess
                            property string font: globalFont
                            property int fontPx: Math.max(8, Math.round(App.Spacing.overallText * 0.6))
                            readonly property real plotLeft: fontPx * 3.6
                            readonly property real plotRight: 8
                            readonly property real plotTop: fontPx * 0.8
                            readonly property real plotBottom: fontPx * 2.0
                            // Time window: the chosen days up to now, or from the first drive
                            readonly property real t1: tripHistoryPage.trends.toMs || Date.now()
                            readonly property real t0: {
                                if (tripHistoryPage.trendDays > 0) return t1 - tripHistoryPage.trendDays * 86400000
                                var r = rows || []
                                return r.length > 0 ? Math.min(r[0].startMs - 43200000, t1 - 86400000) : t1 - 86400000
                            }
                            function xOf(t) { return plotLeft + (t - t0) / Math.max(1, t1 - t0) * (width - plotLeft - plotRight) }

                            onRowsChanged: requestPaint()
                            onMetricChanged: requestPaint()
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            onLineColorChanged: requestPaint()
                            onT0Changed: requestPaint()

                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                var r = rows || []
                                var left = plotLeft, top = plotTop, w = width - plotLeft - plotRight, h = height - plotTop - plotBottom
                                if (w <= 10 || h <= 10) return
                                ctx.font = fontPx + "px " + font
                                ctx.fillStyle = axisColor
                                ctx.strokeStyle = axisColor
                                ctx.lineWidth = 1
                                if (r.length === 0) {
                                    ctx.textAlign = "center"
                                    ctx.textBaseline = "middle"
                                    ctx.fillText(isParam ? "No drive in this period recorded it"
                                                         : "No drive in this period gave this number", left + w / 2, top + h / 2)
                                    return
                                }
                                var m = isParam ? null : metric
                                var bLo = m && m.bandLo !== undefined ? m.bandLo : NaN
                                var bHi = m && m.bandHi !== undefined ? m.bandHi : NaN
                                var base = m && m.baseline !== undefined ? m.baseline : NaN
                                var lo = Infinity, hi = -Infinity
                                for (var i = 0; i < r.length; i++) {
                                    lo = Math.min(lo, isParam ? r[i].lo : r[i].v)
                                    hi = Math.max(hi, isParam ? r[i].hi : r[i].v)
                                }
                                // Show the usual range when it is near the data
                                var dataSpan = Math.max(1e-6, hi - lo)
                                if (!isNaN(bLo) && bLo < lo && lo - bLo < dataSpan * 3 + 1) lo = bLo
                                if (!isNaN(bHi) && bHi > hi && bHi - hi < dataSpan * 3 + 1) hi = bHi
                                if (!isNaN(base)) { lo = Math.min(lo, base); hi = Math.max(hi, base) }
                                var span = hi - lo
                                if (span < 1e-6) { span = Math.max(1, Math.abs(hi) * 0.1); lo -= span / 2; hi += span / 2 }
                                else { lo -= span * 0.08; hi += span * 0.08 }
                                span = hi - lo
                                var Y = function(v) { return top + h - (v - lo) / span * h }
                                var X = xOf

                                // Grid + value labels
                                ctx.globalAlpha = 0.3
                                for (var g = 0; g <= 4; g++) {
                                    var gy = top + h * g / 4
                                    ctx.beginPath(); ctx.moveTo(left, gy); ctx.lineTo(left + w, gy); ctx.stroke()
                                }
                                ctx.globalAlpha = 1
                                ctx.textAlign = "right"
                                ctx.textBaseline = "middle"
                                for (var k = 0; k <= 4; k++)
                                    ctx.fillText(Number(hi - span * k / 4).toFixed(span < 5 ? 1 : 0), left - 4, top + h * k / 4)
                                // Date labels, the outer two kept inside the plot
                                ctx.textBaseline = "top"
                                for (var d = 0; d <= 4; d++) {
                                    var t = t0 + (t1 - t0) * d / 4
                                    ctx.textAlign = d === 0 ? "left" : d === 4 ? "right" : "center"
                                    ctx.fillText(Qt.formatDate(new Date(t), "d MMM"), X(t), top + h + 5)
                                }

                                ctx.save()
                                ctx.beginPath(); ctx.rect(left, top, w, h); ctx.clip()
                                // Usual range
                                if (!isNaN(bLo) || !isNaN(bHi)) {
                                    var yTop = isNaN(bHi) ? top : Y(bHi), yBot = isNaN(bLo) ? top + h : Y(bLo)
                                    ctx.fillStyle = Qt.rgba(bandColor.r, bandColor.g, bandColor.b, 0.12)
                                    ctx.fillRect(left, yTop, w, yBot - yTop)
                                }
                                // The drives before the last three, as a dashed line
                                if (!isNaN(base)) {
                                    ctx.strokeStyle = axisColor
                                    ctx.globalAlpha = 0.7
                                    ctx.setLineDash([5, 4])
                                    ctx.beginPath(); ctx.moveTo(left, Y(base)); ctx.lineTo(left + w, Y(base)); ctx.stroke()
                                    ctx.setLineDash([])
                                    ctx.globalAlpha = 1
                                }
                                // Lowest-highest whiskers (parameters)
                                if (isParam) {
                                    ctx.strokeStyle = Qt.rgba(lineColor.r, lineColor.g, lineColor.b, 0.45)
                                    ctx.lineWidth = Math.max(2, fontPx * 0.3)
                                    for (var q = 0; q < r.length; q++) {
                                        ctx.beginPath(); ctx.moveTo(X(r[q].startMs), Y(r[q].lo)); ctx.lineTo(X(r[q].startMs), Y(r[q].hi)); ctx.stroke()
                                    }
                                }
                                // Line through the drives
                                ctx.strokeStyle = lineColor
                                ctx.globalAlpha = 0.5
                                ctx.lineWidth = 1.5
                                ctx.beginPath()
                                ctx.moveTo(X(r[0].startMs), Y(r[0].v))
                                for (var a = 1; a < r.length; a++) ctx.lineTo(X(r[a].startMs), Y(r[a].v))
                                ctx.stroke()
                                ctx.globalAlpha = 1
                                // A dot per drive; outside the usual range in the warning colour
                                var dotR = Math.max(3, fontPx * 0.4)
                                for (var b = 0; b < r.length; b++) {
                                    var v = r[b].v
                                    var out = (!isNaN(bLo) && v < bLo) || (!isNaN(bHi) && v > bHi)
                                    ctx.fillStyle = out ? warnColor : lineColor
                                    ctx.beginPath(); ctx.arc(X(r[b].startMs), Y(v), dotR, 0, Math.PI * 2); ctx.fill()
                                }
                                ctx.restore()
                                if (!isNaN(base)) {
                                    ctx.fillStyle = axisColor
                                    ctx.textAlign = "left"
                                    ctx.textBaseline = "bottom"
                                    ctx.fillText("before", left + 4, Y(base) - 2)
                                }
                            }

                            // Tap a drive's point to open it
                            TapHandler {
                                onTapped: function(eventPoint) {
                                    var r = trendChart.rows || []
                                    var best = -1, bestDx = dp(24)
                                    for (var i = 0; i < r.length; i++) {
                                        var dx = Math.abs(trendChart.xOf(r[i].startMs) - eventPoint.position.x)
                                        if (dx < bestDx) { bestDx = dx; best = i }
                                    }
                                    if (best >= 0) tripHistoryPage.openTripFromTrend(r[best].id)
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: text !== ""
                        wrapMode: Text.WordWrap
                        text: (tripHistoryPage.trendKind === "metric" && tripHistoryPage.trendMetric
                               ? tripHistoryPage.trendMetric.description + "  " : "")
                              + (tripHistoryPage.trendRows.length > 0 ? "Tap a point to open that drive." : "")
                        color: labelColor
                        font.pixelSize: App.Spacing.overallText * 0.65
                        font.family: globalFont
                    }
                }
            }
        }
    }

    // Small accent-outline button used by the page
    component TripActionButton: Rectangle {
        id: btn
        property string text: ""
        signal clicked()
        implicitWidth: btnText.implicitWidth + dp(20)
        implicitHeight: btnText.implicitHeight + dp(10)
        radius: dpMin(6, 2)
        color: btnMouse.pressed ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.3) : "transparent"
        border.color: accentColor
        border.width: 1
        Text {
            id: btnText
            anchors.centerIn: parent
            text: btn.text
            color: accentColor
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

}
