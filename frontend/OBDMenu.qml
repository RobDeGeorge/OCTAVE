import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Shapes 1.15
import QtQuick.Dialogs
import Qt5Compat.GraphicalEffects
import "." as App
import "dashboards" as Dashboards
import "dashboards/editor" as Editor
import "obd" as Obd
import OCTAVE.OBD 1.0

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: obdPage
    objectName: "obdMenu"

    // Full IMU rate while the OBD views are on screen (dashboards can carry
    // compass and G-force gauges)
    App.ImuDemand { consumer: "obdMenu"; wanted: obdPage.visible }
    required property StackView stackView
    required property ApplicationWindow mainWindow

    // Global font binding for all text in this component
    // fontFamily always returns a valid font (systemDefaultFont or custom font)
    property string globalFont: App.Style.fontFamily

    // Define background and accent colors based on screenshot
    property color backgroundColor: App.Style.obdBoxBackground
    property color accentColor: App.Style.obdBarColor
    property color textColor: App.Style.obdValueColor      // For values - calculated for OBD box background
    property color labelColor: App.Style.obdLabelColor     // For parameter labels - calculated for OBD box background
    
    // Card style setting - true for circular gauges, false for square cards
    readonly property bool useCircularCards: settingsManager ? settingsManager.obdCardStyleCircular : false

    // Active dashboard: "grid" = default parameter cards, or one of the registered dashboards
    property string activeDashboardId: settingsManager
        ? settingsManager.get_setting_with_default("activeDashboard", "grid")
        : "grid"

    // Dashboards only show against a live vehicle. Until obdManager reports
    // a connection the page shows the connect prompt (parameterCardsEmpty)
    // instead of whichever dashboard is active, swipe is off and the page
    // dots are hidden. The active id is kept, so the same dashboard comes
    // back the moment the adapter connects. The chooser stays reachable so
    // dashboards can still be created and edited while parked offline.
    readonly property bool obdConnected: typeof obdManager !== "undefined" && obdManager
                                         && obdManager.connected === true

    // Dashboard registry — built-in "grid" (parameter-cards view in this
    // file) plus every JSON-defined dashboard enumerated by DashboardManager
    // (presets + user-authored files). Ids like "sport"/"minimal"/"fullgrid"
    // still resolve to the same dashboards — they just render from JSON via
    // DashboardRenderer now instead of hand-written QML. See
    // TODO/dashboards-roadmap.md for the full Phase 2 design.
    // A dashboard being edited on another device (dashboardManager.preview):
    // shown here in place of the active one, with or without a car, until
    // that device ends it (or 60 s after its last change)
    readonly property var _preview: (typeof dashboardManager !== "undefined" && dashboardManager)
                                    ? dashboardManager.preview : ({})
    readonly property bool previewing: !!(_preview && _preview.spec)

    readonly property var _gridSentinel:
        ({ id: "grid", label: "Parameter Cards", builtIn: true })

    property var dashboardRegistry: _rebuildRegistry()

    // Loaded specs by dashboard id. loadDashboard() returns a new object on
    // every call, so a spec binding that re-evaluates during page activation
    // would hand DashboardRenderer a "new" dashboard and tear its async
    // widget Loaders down mid-incubation. Reading through this cache keeps
    // the object the same; filling it doesn't notify (only reassigning
    // _specCache does), so it can be read inside the binding.
    property var _specCache: ({})
    function _specFor(id) {
        if (!_specCache[id])
            _specCache[id] = dashboardManager.loadDashboard(id)
        return _specCache[id]
    }

    function _rebuildRegistry() {
        var list = [_gridSentinel]
        if (typeof dashboardManager !== "undefined" && dashboardManager) {
            list = list.concat(dashboardManager.dashboards)
        }
        return list
    }

    // Refresh the registry whenever DashboardManager's list changes (user
    // added/deleted a dashboard, or an external edit + refresh()).
    Connections {
        target: (typeof dashboardManager !== "undefined") ? dashboardManager : null
        ignoreUnknownSignals: true
        function onDashboardsChanged() {
            obdPage.dashboardRegistry = obdPage._rebuildRegistry()
            obdPage._specCache = ({})   // a dashboard was saved, edited or removed: reload
            // If the active dashboard vanished (deleted in-app, or its JSON
            // was removed externally and refresh() ran), fall back to the
            // built-in Parameter Cards rather than rendering nothing.
            var stillExists = false
            for (var i = 0; i < obdPage.dashboardRegistry.length; i++) {
                if (obdPage.dashboardRegistry[i].id === obdPage.activeDashboardId) {
                    stillExists = true
                    break
                }
            }
            if (!stillExists)
                obdPage.setActiveDashboard("grid")
        }
    }

    function setActiveDashboard(id) {
        activeDashboardId = id
        if (settingsManager) settingsManager.save_setting("activeDashboard", id)
    }

    // Scriptable (dev tooling drives these through the command server's `call`)
    // ── Swipe between dashboards (see swipeHandler) ──────────────────
    property real _swipeOffset: 0
    property int _swipeDir: 0

    function _registryIndex(id) {
        for (var i = 0; i < dashboardRegistry.length; i++)
            if (dashboardRegistry[i].id === id) return i
        return 0
    }

    // dir = +1 next, -1 previous; wraps around the registry.
    function swipeDashboard(dir) {
        var n = dashboardRegistry.length
        if (n < 2 || !obdConnected) return
        _swipeDir = dir
        swipeAnim.restart()
    }

    function _commitSwipe() {
        var n = dashboardRegistry.length
        var next = (_registryIndex(activeDashboardId) + _swipeDir + n) % n
        setActiveDashboard(dashboardRegistry[next].id)
        _swipeOffset = _swipeDir * Math.max(1, cardsScroll.width)   // enter from the far side
    }

    SequentialAnimation {
        id: swipeAnim
        NumberAnimation {
            target: obdPage; property: "_swipeOffset"
            to: -obdPage._swipeDir * Math.max(1, cardsScroll.width)
            duration: 160; easing.type: Easing.InCubic
        }
        ScriptAction { script: obdPage._commitSwipe() }
        NumberAnimation {
            target: obdPage; property: "_swipeOffset"
            to: 0; duration: 220; easing.type: Easing.OutCubic
        }
    }

    NumberAnimation {
        id: swipeBack
        target: obdPage; property: "_swipeOffset"
        to: 0; duration: 180; easing.type: Easing.OutCubic
    }

    function openDashboardChooser() { dashboardChooserPopup.open() }
    function closeDashboardChooser() { dashboardChooserPopup.close() }

    // Delete a dashboard from the chooser (built-ins are hidden, not removed). Lives on the page (not in the
    // card delegate) because dashboardsChanged rebuilds the registry
    // synchronously inside deleteDashboard(), which destroys the delegate that
    // tapped Delete — any code after the call in the delegate context throws
    // "obdPage is not defined". Active-dashboard fallback happens in the
    // onDashboardsChanged handler.
    // ── Sharing ──────────────────────────────────────────────────────
    property string shareStatus: ""
    property bool shareStatusIsError: false
    Timer {
        id: shareStatusTimer
        interval: 4500
        onTriggered: obdPage.shareStatus = ""
    }
    function showShareStatus(msg, isError) {
        shareStatus = msg
        shareStatusIsError = isError
        shareStatusTimer.restart()
    }
    function finishImport(newId) {
        if (newId && newId.length > 0) {
            var label = newId
            for (var i = 0; i < dashboardRegistry.length; i++)
                if (dashboardRegistry[i].id === newId) label = dashboardRegistry[i].label
            showShareStatus("Imported \"" + label + "\"", false)
            importReportPopup.openFor(dashboardManager.lastImportReport)
        } else {
            showShareStatus(dashboardManager.lastShareError || "Import failed", true)
        }
    }

    Dashboards.RemoteEditPopup { id: remoteEditPopup }

    // What the import brought in and what it left out
    Dashboards.ImportReportPopup {
        id: importReportPopup
        stageWidth: obdPage.width
        stageHeight: Math.max(1, obdPage.height - dp(80))
        onShowRequested: function(id) {
            dashboardChooserPopup.close()
            obdPage.setActiveDashboard(id)
        }
        onEditRequested: function(id) {
            dashboardChooserPopup.close()
            obdPage.stackView.push("dashboards/editor/DashboardEditor.qml", {
                stackView: obdPage.stackView,
                callerPage: obdPage,
                editingId: id
            })
        }
    }

    Menu {
        id: shareMenu
        objectName: "shareMenu"
        property string dashId: ""
        MenuItem {
            objectName: "shareExport"
            text: "Save to Downloads"
            onTriggered: {
                var path = dashboardManager.exportDashboard(shareMenu.dashId)
                if (path.length > 0) obdPage.showShareStatus("Saved to " + path, false)
                else obdPage.showShareStatus(dashboardManager.lastShareError || "Export failed", true)
            }
        }
        MenuItem {
            objectName: "shareClipboard"
            text: "Copy as text"
            onTriggered: {
                if (dashboardManager.copyDashboardToClipboard(shareMenu.dashId))
                    obdPage.showShareStatus("Copied. Paste it anywhere to share.", false)
                else
                    obdPage.showShareStatus(dashboardManager.lastShareError || "Copy failed", true)
            }
        }
    }

    FileDialog {
        id: importFileDialog
        title: "Import a dashboard"
        nameFilters: ["OCTAVE dashboards (*.json)", "All files (*)"]
        onAccepted: obdPage.finishImport(dashboardManager.importDashboard(selectedFile.toString()))
    }

    function restoreDefaultDashboards() {
        if (typeof dashboardManager === "undefined" || !dashboardManager) return
        if (dashboardManager.restoreBuiltInDashboards())
            showShareStatus("Default dashboards restored", false)
    }

    function deleteUserDashboard(id) {
        if (typeof dashboardManager === "undefined" || !dashboardManager) return false
        return dashboardManager.deleteDashboard(id)
    }

    // Unsaved editor work rescued by DashboardEditor's Component.onDestruction
    // (e.g. user switched menus mid-build). Refreshed each time the chooser opens;
    // non-empty shows the "Resume draft" button.
    property string _editorDraft: ""

    // Display label for the currently-active dashboard, derived from the
    // registry so custom user dashboards show their real name in the header.
    readonly property string activeDashboardLabel: {
        for (var i = 0; i < dashboardRegistry.length; i++) {
            if (dashboardRegistry[i].id === activeDashboardId)
                return dashboardRegistry[i].label
        }
        return "Dashboard"
    }

    // Fallback schematic preview for the "Parameter Cards" entry, which
    // has no standalone QML file (it's the built-in grid in this file).
    // All other dashboards render as live, scaled miniatures of the real
    // QML in the chooser popup below.
    Component {
        id: gridPreview
        GridLayout {
            anchors.fill: parent
            columns: 3
            rows: 3
            rowSpacing: dpMin(3, 1)
            columnSpacing: dpMin(3, 1)
            Repeater {
                model: 9
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: dpMin(3, 1)
                    color: Qt.darker(App.Style.obdBoxBackground, 1.15)
                    border.color: Qt.darker(App.Style.obdBarColor, 1.4)
                    border.width: 1
                    Rectangle {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: Math.max(2, parent.width * 0.08)
                        width: parent.width * 0.55
                        height: Math.max(2, parent.height * 0.1)
                        radius: height / 2
                        color: App.Style.obdBarColor
                    }
                }
            }
        }
    }


    // OBD parameters from centralized singleton
    property var allParameters: App.OBDParameterModel.allParameters

    // Cards read live values only while the parameter grid is actually on
    // screen. This page stays alive hidden (StackView cache, pre-built at
    // startup), and with the IMU streaming, cards that kept listening held
    // the render loop near 50 fps and cost ~10% of a Pi core while nothing of
    // it was visible. Hidden, the cards settle once to "--"; on return they
    // pick up the live values. On screen each card listens to its own value
    // (OBDParameterModel.liveValue -> paramStore[id]), not to every value.
    readonly property bool _gridLive: obdConnected && activeDashboardId === "grid"
        && (StackView.status === StackView.Active || StackView.status === StackView.Activating)
    
    // Parameter Cards = the connected vehicle's full PID list: every OBD
    // parameter its supported-PID scan reported this session. Empty until a
    // vehicle has been scanned (see the empty state over the grid).
    readonly property var vehicleParameters: App.OBDParameterModel.vehicleParameters

    // Column count from the card count; past ~16 cards the grid scrolls
    // (cards keep a minimum height instead of shrinking to slivers).
    function updateLayout() {
        const n = vehicleParameters.length;
        if (n <= 4) {
            parametersGrid.columns = 2;
        } else if (n <= 9) {
            parametersGrid.columns = 3;
        } else if (n <= 16 || cardsScroll.width < dp(900)) {
            parametersGrid.columns = 4;
        } else {
            parametersGrid.columns = 5;
        }
    }
    onVehicleParametersChanged: updateTimer.restart()

    // ── Polling demand ────────────────────────────────────────────────
    // obdManager polls only what is on screen. While this page is the
    // active StackView page: the active dashboard's PIDs, or — on Parameter
    // Cards — every PID the vehicle supports. Leaving the page withdraws
    // both demands.
    readonly property bool _pageActiveForDemand:
        StackView.status === StackView.Active || StackView.status === StackView.Activating
    readonly property var _dashboardDemand:
        (_pageActiveForDemand && obdConnected && (previewing || activeDashboardId !== "grid")) ? dashboardRenderer.paramIds : []
    readonly property var _cardsDemand: {
        if (!_pageActiveForDemand || !obdConnected || previewing || activeDashboardId !== "grid") return [];
        return vehicleParameters.map(function(p) { return p.id; });
    }
    function _pushDemand(consumer, ids) {
        if (typeof obdManager === "undefined" || !obdManager || !obdManager.setParameterDemand) return;
        obdManager.setParameterDemand(consumer, ids);
    }
    on_DashboardDemandChanged: _pushDemand("dashboard", _dashboardDemand)
    on_CardsDemandChanged: _pushDemand("cards", _cardsDemand)
    Component.onDestruction: {
        _pushDemand("dashboard", []);
        _pushDemand("cards", []);
        _pushDemand("autoShow", []);
    }

    // ── "Show this dashboard when ..." (a spec's showWhen) ───────────
    // Checked twice a second while this page is on screen with a car
    // connected (dashboards/AutoShowRules.qml has the rules). A dashboard
    // shown by a rule isn't saved as the active one.
    readonly property var _autoRules: dashboardRegistry.filter(function(d) { return !!d.showWhen })
    readonly property var _autoDemand: {
        if (!_pageActiveForDemand || !obdConnected) return []
        var seen = {}, out = []
        for (var i = 0; i < _autoRules.length; i++) {
            var id = _autoRules[i].showWhen.paramId
            if (!seen[id]) { seen[id] = true; out.push(id) }
        }
        return out
    }
    on_AutoDemandChanged: _pushDemand("autoShow", _autoDemand)
    Dashboards.AutoShowRules {
        id: autoShow
        rules: obdPage._autoRules
        activeId: obdPage.activeDashboardId
        onSwitchTo: function(id) { obdPage.activeDashboardId = id }
    }

    Timer {
        id: autoRuleTimer
        interval: 500
        repeat: true
        running: obdPage._autoRules.length > 0 && obdPage._pageActiveForDemand && obdPage.obdConnected
        onTriggered: autoShow.check()
    }
    
    Rectangle {
        anchors.fill: parent
        color: backgroundColor
        clip: true      // swipe slides content past the page edges

        // ── Header bar ─────────────────────────────────────────────────
        // Hosts the current dashboard's label and the chooser button.
        // Content sits below this.
        Rectangle {
            id: obdHeader
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            // Match MediaPlayer's titleBar height exactly so the header
            // lines up with the media pages.
            height: App.Spacing.bottomBarNavButtonHeight + App.Spacing.mediaRoomMargin * 2
            color: Qt.darker(obdPage.backgroundColor, 1.12)
            z: 2

            // Page dots: one per registry entry, active one filled — shows
            // where you are when swiping between dashboards.
            Row {
                objectName: "dashboardPageDots"
                visible: obdPage.obdConnected && obdPage.dashboardRegistry.length > 1
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: dp(5)
                spacing: dp(6)
                Repeater {
                    model: obdPage.dashboardRegistry.length
                    delegate: Rectangle {
                        readonly property bool _active: index === obdPage._registryIndex(obdPage.activeDashboardId)
                        width: dp(6); height: dp(6); radius: width / 2
                        color: _active ? App.Style.accent : App.Style.obdLabelColor
                        opacity: _active ? 1.0 : 0.35
                    }
                }
            }

            Text {
                objectName: "dashboardTitle"
                anchors.centerIn: parent
                text: obdPage.previewing
                      ? "Preview from another device · " + (obdPage._preview.spec.label || "New dashboard")
                      : (obdManager && obdManager.simulated ? "Simulated · " : "") + obdPage.activeDashboardLabel
                color: App.Style.obdValueColor
                font.pixelSize: App.Spacing.overallText * 1.15
                font.bold: true
                font.family: obdPage.globalFont
            }

            // Subtle separator line at the bottom of the header.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Qt.darker(App.Style.obdBarColor, 1.8)
                opacity: 0.6
            }
        }

        // Dashboards chooser button — anchored to the outer content
        // Rectangle (NOT to obdHeader) with mediaRoomMargin top/right
        // margins and the bottom-bar nav button size.
        Control {
            id: dashboardsButton
            objectName: "dashboardsButton"
            width: App.Spacing.bottomBarNavButtonWidth
            height: App.Spacing.bottomBarNavButtonHeight
            z: 3
            anchors {
                top: parent.top
                right: parent.right
                topMargin: App.Spacing.mediaRoomMargin
                rightMargin: App.Spacing.mediaRoomMargin
            }

            background: Rectangle {
                color: "transparent"
                radius: dpMin(8, 2)
                border.color: App.Style.accent
                border.width: 1
                scale: dashboardsButtonMouse.pressed ? 0.8 : 1.0
                opacity: dashboardsButtonMouse.pressed ? 0.7 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
                }
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }

            contentItem: Item {
                scale: dashboardsButtonMouse.pressed ? 0.8 : 1.0
                opacity: dashboardsButtonMouse.pressed ? 0.7 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
                }
                Behavior on opacity { NumberAnimation { duration: 150 } }
                Image {
                    id: dashboardsButtonImage
                    anchors.centerIn: parent
                    width: parent.width * 0.7
                    height: parent.height * 0.7
                    source: "./assets/dashboard_button.svg"
                    sourceSize: Qt.size(width * 2, height * 2)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    antialiasing: true
                    mipmap: true
                    visible: false
                }
                ColorOverlay {
                    anchors.fill: dashboardsButtonImage
                    source: dashboardsButtonImage
                    color: App.Style.accent
                }
            }

            MouseArea {
                id: dashboardsButtonMouse
                width: parent.width * 2
                height: parent.height * 2
                anchors.centerIn: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: dashboardChooserPopup.open()
            }
        }

        // ── Dashboard renderer (visible only when a dashboard is active) ──
        // Takes a JSON spec (fetched from DashboardManager) and instantiates
        // widgets into a grid. Replaces the old per-dashboard QML loader.
        // Hidden (and, through `spec`, unloaded) while no vehicle is
        // connected: the connect prompt takes the page instead.
        Dashboards.DashboardRenderer {
            id: dashboardRenderer
            objectName: "activeDashboardRenderer"
            transform: Translate { x: obdPage._swipeOffset }
            anchors {
                top: obdHeader.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                leftMargin: dp(10)
                rightMargin: dp(10)
                topMargin: dp(6)
                bottomMargin: dp(10)
            }
            visible: obdPage.previewing || (obdPage.obdConnected && obdPage.activeDashboardId !== "grid")
            // Load the dashboard only while this is the active StackView page,
            // not merely `visible`: page pre-building draws this page once
            // offscreen (briefly visible), and a visible-keyed spec then
            // started the widgets' async Loaders and tore them down mid-
            // incubation ("Object or context destroyed during incubation"),
            // leaving the render loop near full rate while hidden — ~20% of
            // a Pi core at idle.
            readonly property bool _pageActive: obdPage.StackView.status === StackView.Active
                                                || obdPage.StackView.status === StackView.Activating
            spec: (visible && _pageActive && typeof dashboardManager !== "undefined" && dashboardManager)
                  ? (obdPage.previewing ? obdPage._preview.spec : obdPage._specFor(obdPage.activeDashboardId))
                  : null
        }

        Flickable {
            id: cardsScroll
            objectName: "parameterCardsScroll"
            visible: !obdPage.previewing && obdPage.obdConnected && obdPage.activeDashboardId === "grid"
            anchors {
                top: obdHeader.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                leftMargin: dp(10)
                rightMargin: dp(10)
                topMargin: dp(10)
                bottomMargin: dp(10)
            }
            clip: true
            contentWidth: width
            contentHeight: parametersGrid.height
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height + 1
            transform: Translate { x: obdPage._swipeOffset }

            // Rows needed for the current card count, and the height a row
            // may not shrink below before the grid starts scrolling.
            readonly property int _rows: Math.ceil(obdPage.vehicleParameters.length
                                                   / Math.max(1, parametersGrid.columns))
            readonly property real _minRowH: dp(120)

        GridLayout {
            id: parametersGrid
            width: cardsScroll.width
            height: Math.max(cardsScroll.height,
                             cardsScroll._rows * cardsScroll._minRowH
                             + Math.max(0, cardsScroll._rows - 1) * rowSpacing)
            columns: 3
            rowSpacing: dp(10)
            columnSpacing: dp(10)
            
            // Use Repeater to create parameter cards
            Repeater {
                model: obdPage.vehicleParameters

                Item {
                    id: cardContainer
                    objectName: "paramCard_" + modelData.id
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1     // equal column widths
                    Layout.preferredHeight: 1

                    // This card's own value (OBDParameterModel.liveValue): it
                    // re-evaluates only when this PID changes, not when any
                    // other value does. Read only while the grid is live (hidden,
                    // the card settles once and stops listening, see _gridLive).
                    readonly property var liveValue: obdPage._gridLive
                        ? App.OBDParameterModel.liveValue(modelData.id) : undefined

                    // False until a reading arrives (adapter disconnected or PID
                    // not reported yet) so the card shows "--" instead of a fake 0.0.
                    readonly property bool hasValue: liveValue !== undefined

                    // Animated display value - fast rolling effect
                    property real targetValue: liveValue || 0
                    property real displayValue: targetValue
                    Behavior on displayValue {
                        NumberAnimation {
                            duration: 50
                            easing.type: Easing.Linear
                        }
                    }

                    // Square card style (default)
                    Rectangle {
                        id: squareCard
                        anchors.fill: parent
                        visible: !obdPage.useCircularCards
                        clip: true

                        color: squareCardMouseArea.containsMouse && modelData.id === "RPM" ?
                               Qt.lighter(Qt.darker(backgroundColor, 0.9), 1.1) : Qt.darker(backgroundColor, 0.9)
                        radius: dpMin(6, 2)

                        Behavior on color { ColorAnimation { duration: 150 } }

                        MouseArea {
                            id: squareCardMouseArea
                            anchors.fill: parent
                            hoverEnabled: modelData.id === "RPM"
                            cursorShape: modelData.id === "RPM" ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: {
                                if (modelData.id === "RPM") {
                                    rpmSettingsPopup.open()
                                }
                            }
                        }

                        // Title + value centred in the space above the bar. The
                        // bar is pinned to the card's bottom edge (not stacked in
                        // the column) so a short card can't push it out.
                        Column {
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: -bar.height / 2
                            width: parent.width - dp(16)
                            spacing: dp(2)

                            Text {
                                width: parent.width
                                text: modelData.title
                                color: labelColor
                                font.pixelSize: App.Spacing.overallText
                                font.family: obdPage.globalFont
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: cardContainer.hasValue
                                      ? cardContainer.displayValue.toFixed(App.OBDParameterModel.decimalsFor(modelData.id)) + " " + modelData.unit
                                      : "--"
                                color: cardContainer.hasValue ? textColor : labelColor
                                font.pixelSize: App.Spacing.overallText
                                font.bold: true
                                font.family: obdPage.globalFont
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                        }

                        Rectangle {
                            id: bar
                            anchors {
                                left: parent.left
                                right: parent.right
                                bottom: parent.bottom
                                margins: dp(8)
                            }
                            height: Math.max(3, App.Spacing.overallSliderHeight * .4)
                            color: Qt.darker(backgroundColor, 1.1)
                            radius: height / 2

                            // Signed ranges (fuel trims, timing, pitch/roll, G)
                            // fill outward from the zero point, so a reading of 0
                            // shows an empty bar instead of a half-full one.
                            readonly property real zeroFrac: modelData.min < 0 && modelData.max > 0
                                ? -modelData.min / (modelData.max - modelData.min) : 0
                            readonly property real valueFrac: {
                                const value = cardContainer.targetValue;
                                return Math.max(0, Math.min(1,
                                    (value - modelData.min) / (modelData.max - modelData.min)));
                            }

                            Rectangle {
                                id: progressBar
                                visible: cardContainer.hasValue
                                height: parent.height
                                radius: parent.radius
                                color: App.Style.obdBarColor
                                x: parent.width * Math.min(bar.zeroFrac, bar.valueFrac)
                                width: Math.max(parent.height,
                                                parent.width * Math.abs(bar.valueFrac - bar.zeroFrac))

                                Behavior on width {
                                    NumberAnimation {
                                        duration: 100
                                        easing.type: Easing.OutCubic
                                    }
                                }
                            }
                        }
                    }

                    // Circular gauge style
                    Rectangle {
                        id: circularCard
                        visible: obdPage.useCircularCards

                        // Make it larger - use 95% of the smaller dimension for better fill
                        property real cardSize: Math.min(parent.width, parent.height) * 0.95
                        width: cardSize
                        height: cardSize
                        anchors.centerIn: parent

                        color: circularCardMouseArea.containsMouse && modelData.id === "RPM" ?
                               Qt.lighter(Qt.darker(backgroundColor, 0.9), 1.1) : Qt.darker(backgroundColor, 0.9)
                        radius: width / 2  // Fully circular

                        Behavior on color { ColorAnimation { duration: 150 } }

                        MouseArea {
                            id: circularCardMouseArea
                            anchors.fill: parent
                            hoverEnabled: modelData.id === "RPM"
                            cursorShape: modelData.id === "RPM" ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: {
                                if (modelData.id === "RPM") {
                                    rpmSettingsPopup.open()
                                }
                            }
                        }

                        // Circular gauge arc - using GPU-accelerated Shape
                        property real gaugeValue: {
                            const value = cardContainer.targetValue;
                            return Math.min(1, Math.max(0, (value - modelData.min) / (modelData.max - modelData.min)));
                        }

                        // Animated gauge value for smooth transitions
                        property real animatedGaugeValue: gaugeValue
                        Behavior on animatedGaugeValue {
                            NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
                        }

                        // Gauge dimensions
                        property real arcRadius: (width / 2) - 8
                        property real arcWidth: Math.max(10, arcRadius * 0.14)
                        property real startAngle: 135  // degrees, bottom-left
                        property real sweepAngle: 270  // degrees, total arc span

                        // Background arc - GPU-accelerated Shape (no layer overhead)
                        Shape {
                            anchors.fill: parent

                            ShapePath {
                                fillColor: "transparent"
                                strokeColor: Qt.darker(obdPage.backgroundColor, 1.1)
                                strokeWidth: circularCard.arcWidth
                                capStyle: ShapePath.RoundCap

                                PathAngleArc {
                                    centerX: circularCard.width / 2
                                    centerY: circularCard.height / 2
                                    radiusX: circularCard.arcRadius
                                    radiusY: circularCard.arcRadius
                                    startAngle: circularCard.startAngle
                                    sweepAngle: circularCard.sweepAngle
                                }
                            }
                        }

                        // Value arc (on top) - GPU-accelerated Shape
                        Shape {
                            anchors.fill: parent
                            visible: cardContainer.hasValue && circularCard.animatedGaugeValue > 0.001

                            ShapePath {
                                fillColor: "transparent"
                                strokeColor: App.Style.obdBarColor
                                strokeWidth: circularCard.arcWidth
                                capStyle: ShapePath.RoundCap

                                PathAngleArc {
                                    centerX: circularCard.width / 2
                                    centerY: circularCard.height / 2
                                    radiusX: circularCard.arcRadius
                                    radiusY: circularCard.arcRadius
                                    startAngle: circularCard.startAngle
                                    sweepAngle: circularCard.sweepAngle * circularCard.animatedGaugeValue
                                }
                            }
                        }

                        // Center content
                        Column {
                            anchors.centerIn: parent
                            spacing: dp(2)

                            Text {
                                text: modelData.title
                                color: labelColor
                                font.pixelSize: App.Spacing.overallText * 0.9
                                font.family: obdPage.globalFont
                                anchors.horizontalCenter: parent.horizontalCenter
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                width: circularCard.width * 0.75
                            }

                            Text {
                                text: cardContainer.hasValue ? cardContainer.displayValue.toFixed(App.OBDParameterModel.decimalsFor(modelData.id)) : "--"
                                color: cardContainer.hasValue ? textColor : labelColor
                                font.pixelSize: App.Spacing.overallText * 1.4
                                font.bold: true
                                font.family: obdPage.globalFont
                                anchors.horizontalCenter: parent.horizontalCenter
                            }

                            Text {
                                text: modelData.unit
                                color: labelColor
                                font.pixelSize: App.Spacing.overallText * 0.75
                                font.family: obdPage.globalFont
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }
                    }
                }
            }
        }
        }   // cardsScroll

        // ── Empty states ────────────────────────────────────────────────
        // With no adapter connected this replaces every dashboard (the
        // connect prompt). Once connected it covers only Parameter Cards,
        // which lists the vehicle's PIDs and so stays empty until the
        // vehicle has answered the supported-PID scan.
        Column {
            objectName: "parameterCardsEmpty"
            visible: !obdPage.previewing && (!obdPage.obdConnected
                     || (obdPage.activeDashboardId === "grid" && obdPage.vehicleParameters.length === 0))
            anchors.centerIn: cardsScroll
            width: Math.min(cardsScroll.width - dp(40), dp(560))
            spacing: dp(10)
            transform: Translate { x: obdPage._swipeOffset }

            readonly property bool _connected: obdPage.obdConnected

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: !parent._connected ? "No vehicle connected"
                      : (App.OBDParameterModel.vehicleKnown ? "No supported PIDs reported"
                                                            : "Reading your vehicle's PIDs…")
                color: textColor
                font.pixelSize: App.Spacing.overallText * 1.2
                font.bold: true
                font.family: obdPage.globalFont
            }
            Text {
                visible: parent._connected
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: App.OBDParameterModel.vehicleKnown
                      ? "The adapter is connected, but the vehicle didn't report any PIDs. Check that the ignition is on."
                      : "The adapter is connected. Waiting for the vehicle to answer."
                color: labelColor
                font.pixelSize: App.Spacing.overallText * 0.9
                font.family: obdPage.globalFont
            }

            // Connect straight from the empty page (scan, pair, connect)
            App.OBDConnectButton {
                visible: !parent._connected
                anchors.horizontalCenter: parent.horizontalCenter
                mainWindow: obdPage.mainWindow
                fontFamily: obdPage.globalFont
            }
        }

        // ── Swipe between dashboards ────────────────────────────────────
        // Horizontal drag anywhere on the content cycles through the
        // registry (Parameter Cards + every dashboard, wrapping). Only the
        // active dashboard is ever instantiated: the content follows the
        // finger, slides out, the dashboard switches, and the new one slides
        // in from the other side. xAxis-only, so the cards' vertical scroll
        // and taps on widget buttons are unaffected.
        DragHandler {
            id: swipeHandler
            objectName: "dashboardSwipeHandler"
            target: null
            xAxis.enabled: true
            yAxis.enabled: false
            enabled: obdPage.obdConnected && obdPage.dashboardRegistry.length > 1 && !swipeAnim.running
            onTranslationChanged: if (active) obdPage._swipeOffset = translation.x
            onActiveChanged: {
                if (active) return
                // translation is already reset when this fires; the offset
                // tracked during the drag holds the distance travelled.
                var w = Math.max(1, cardsScroll.width)
                var dx = obdPage._swipeOffset
                var vx = centroid.velocity.x
                // A fast flick counts only past a minimum distance, so a tap
                // that skids a little on a bumpy road never flips the page.
                var flick = Math.abs(dx) > w * 0.08
                if (dx < -w * 0.2 || (flick && vx < -dp(800))) obdPage.swipeDashboard(1)
                else if (dx > w * 0.2 || (flick && vx > dp(800))) obdPage.swipeDashboard(-1)
                else swipeBack.restart()
            }
        }
    }
    
    // Use a timer to delay layout updates to prevent rapid successive updates
    Timer {
        id: updateTimer
        interval: 100
        repeat: false
        onTriggered: updateLayout()
    }
    
    // Listen for settings changes
    Connections {
        target: settingsManager
        function onObdParametersChanged() {
            // Use timer to debounce multiple rapid changes
            updateTimer.restart();
        }
    }
    
    // Listen for window size changes
    Connections {
        target: parent
        function onWidthChanged() { updateTimer.restart(); }
        function onHeightChanged() { updateTimer.restart(); }
    }
    
    // Lay the grid out immediately. The debounce timer above is only for the
    // later settings/resize churn — routing the first layout through it left
    // the page visible with the wrong column count for 100 ms on every open.
    Component.onCompleted: {
        updateLayout()
        // The saved active dashboard may have been deleted (or its JSON
        // removed) while the app was closed — fall back to Parameter Cards
        // instead of rendering an empty page. Runtime deletions are handled
        // in onDashboardsChanged.
        if (activeDashboardId !== "grid" && _registryIndex(activeDashboardId) === 0
                && dashboardRegistry[0].id !== activeDashboardId)
            setActiveDashboard("grid")
    }

    // ── Dashboard Chooser Popup ──────────────────────────────────────
    // "+ New": pick a starting point, then the editor opens on it, fitted
    // to this screen's grid density
    Editor.TemplateGallery {
        id: templateGallery
        stageWidth: obdPage.width
        stageHeight: Math.max(1, obdPage.height - dp(80))
        onTemplatePicked: function(spec) {
            dashboardChooserPopup.close()
            obdPage.stackView.push("dashboards/editor/DashboardEditor.qml", {
                stackView: obdPage.stackView,
                callerPage: obdPage,
                initialSpec: spec,
                fitToScreen: true
            })
        }
    }

    Popup {
        id: dashboardChooserPopup
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent.width * 0.92, dp(960))
        height: Math.min(
            parent.height * 0.9,
            chooserHeader.height + chooserGrid.implicitHeight + dp(62)
        )
        modal: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // True from aboutToShow until fully closed — gates the miniature
        // Loaders so instantiation begins *before* the popup animates in,
        // hiding the gauge value-binding animation that would otherwise
        // play out after the popup is already visible.
        property bool preloadMinis: false
        onAboutToShow: {
            preloadMinis = true
            // Re-check for an abandoned editor draft (saved when the editor
            // page is destroyed mid-edit, e.g. by switching menus).
            obdPage._editorDraft = settingsManager
                ? settingsManager.get_setting_with_default("dashboardEditorDraft", "")
                : ""
        }
        onClosed: preloadMinis = false

        background: Rectangle {
            color: Qt.rgba(App.Style.contentColor.r, App.Style.contentColor.g, App.Style.contentColor.b, 0.97)
            radius: App.Spacing.overallMargin
            border.color: App.Style.accent
            border.width: 2
        }

        contentItem: Item {
            id: chooserContent

            // Header strip — sized to the button + margins, like obdHeader:
            // the New / Import / Restore row on the left, the title centred.
            Rectangle {
                id: chooserHeader
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: App.Spacing.bottomBarNavButtonHeight + App.Spacing.mediaRoomMargin * 2
                color: "transparent"

                Text {
                    id: chooserTitle
                    anchors.centerIn: parent
                    text: "Dashboards"
                    // Gives way when the button row reaches it (an upright
                    // window: "In a browser" sat on top of it at 432 wide)
                    readonly property Item lastButton: restoreDefaultsButton.visible ? restoreDefaultsButton
                                                     : browserEditButton.visible ? browserEditButton : importDashboardButton
                    visible: x > lastButton.x + lastButton.width + dp(10)
                    color: App.Style.primaryTextColor
                    font.pixelSize: App.Spacing.overallText * 1.3
                    font.bold: true
                    font.family: obdPage.globalFont
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Qt.darker(App.Style.obdBarColor, 1.8)
                    opacity: 0.6
                }
            }

            // Share/import result line (auto-hides).
            Rectangle {
                id: shareStatusBanner
                objectName: "shareStatusBanner"
                visible: obdPage.shareStatus.length > 0
                z: 10
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: dp(14)
                width: Math.min(parent.width - dp(40), shareStatusText.implicitWidth + dp(32))
                height: shareStatusText.implicitHeight + dp(18)
                radius: dpMin(8, 2)
                color: obdPage.shareStatusIsError ? App.Style.statusDanger : App.Style.accent

                Text {
                    id: shareStatusText
                    anchors.centerIn: parent
                    width: parent.width - dp(24)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: obdPage.shareStatus
                    color: "white"
                    font.pixelSize: App.Spacing.overallText * 0.85
                    font.family: obdPage.globalFont
                }
            }

            // Launch the Phase 3 editor on a blank spec. Edit/Duplicate/Delete
            // live as per-card action buttons below.
            Rectangle {
                id: newDashboardButton
                objectName: "newDashboardButton"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.topMargin: App.Spacing.mediaRoomMargin
                anchors.leftMargin: App.Spacing.mediaRoomMargin
                height: App.Spacing.bottomBarNavButtonHeight
                width: newDashboardLabel.implicitWidth + dp(24)
                radius: dpMin(8, 2)
                color: "transparent"
                border.color: App.Style.accent
                border.width: 1
                scale: newDashboardMouse.pressed ? 0.9 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
                }

                Text {
                    id: newDashboardLabel
                    anchors.centerIn: parent
                    text: "+ New"
                    color: App.Style.accent
                    font.pixelSize: App.Spacing.overallText
                    font.bold: true
                    font.family: obdPage.globalFont
                }

                MouseArea {
                    id: newDashboardMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    // A template first (Blank among them); the editor
                    // opens on the one picked
                    onClicked: templateGallery.open()
                }
            }

            // Resume an editor draft rescued from a mid-build menu switch.
            Rectangle {
                id: resumeDraftButton
                objectName: "resumeDraftButton"
                visible: obdPage._editorDraft.length > 0
                anchors.top: parent.top
                anchors.left: newDashboardButton.right
                anchors.topMargin: App.Spacing.mediaRoomMargin
                anchors.leftMargin: dp(8)
                height: App.Spacing.bottomBarNavButtonHeight
                width: resumeDraftLabel.implicitWidth + dp(24)
                radius: dpMin(8, 2)
                color: "transparent"
                border.color: App.Style.accent
                border.width: 1
                scale: resumeDraftMouse.pressed ? 0.9 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
                }

                Text {
                    id: resumeDraftLabel
                    anchors.centerIn: parent
                    text: "▲ Draft"
                    color: App.Style.accent
                    font.pixelSize: App.Spacing.overallText
                    font.bold: true
                    font.family: obdPage.globalFont
                }

                MouseArea {
                    id: resumeDraftMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        dashboardChooserPopup.close()
                        obdPage.stackView.push("dashboards/editor/DashboardEditor.qml", {
                            stackView: obdPage.stackView,
                            callerPage: obdPage,
                            draftJson: obdPage._editorDraft
                        })
                    }
                }
            }

            // Import a shared dashboard (file or clipboard) — always becomes
            // a new user dashboard; the backend validates and re-ids it.
            Rectangle {
                id: importDashboardButton
                objectName: "importDashboardButton"
                anchors.top: parent.top
                anchors.left: resumeDraftButton.visible ? resumeDraftButton.right : newDashboardButton.right
                anchors.topMargin: App.Spacing.mediaRoomMargin
                anchors.leftMargin: dp(8)
                height: App.Spacing.bottomBarNavButtonHeight
                width: importLabel.implicitWidth + dp(24)
                radius: dpMin(8, 2)
                color: "transparent"
                border.color: App.Style.accent
                border.width: 1
                scale: importMouse.pressed ? 0.9 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
                }

                Text {
                    id: importLabel
                    anchors.centerIn: parent
                    text: "Import"
                    color: App.Style.accent
                    font.pixelSize: App.Spacing.overallText
                    font.bold: true
                    font.family: obdPage.globalFont
                }

                MouseArea {
                    id: importMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: importMenu.popup(importDashboardButton, 0, importDashboardButton.height)
                }

                Menu {
                    id: importMenu
                    objectName: "importMenu"
                    MenuItem {
                        objectName: "importFromFile"
                        text: "From file…"
                        onTriggered: importFileDialog.open()
                    }
                    MenuItem {
                        objectName: "importFromClipboard"
                        text: "Paste from clipboard"
                        onTriggered: obdPage.finishImport(dashboardManager.importDashboardFromClipboard())
                    }
                }
            }

            // The browser dashboard editor: where to open it, and a code
            Rectangle {
                id: browserEditButton
                objectName: "browserEditButton"
                visible: typeof servicePairing !== "undefined" && servicePairing !== null
                anchors.top: parent.top
                anchors.left: importDashboardButton.right
                anchors.topMargin: App.Spacing.mediaRoomMargin
                anchors.leftMargin: dp(8)
                height: App.Spacing.bottomBarNavButtonHeight
                width: visible ? browserEditLabel.implicitWidth + dp(24) : 0
                radius: dpMin(8, 2)
                color: "transparent"
                border.color: App.Style.accent
                border.width: 1
                scale: browserEditMouse.pressed ? 0.9 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
                }

                Text {
                    id: browserEditLabel
                    anchors.centerIn: parent
                    text: "In a browser"
                    color: App.Style.accent
                    font.pixelSize: App.Spacing.overallText
                    font.bold: true
                    font.family: obdPage.globalFont
                }

                MouseArea {
                    id: browserEditMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: remoteEditPopup.open()
                }
            }

            // Bring back built-in dashboards the user deleted.
            Rectangle {
                id: restoreDefaultsButton
                objectName: "restoreDefaultsButton"
                visible: typeof dashboardManager !== "undefined" && dashboardManager
                         && dashboardManager.hiddenBuiltInCount > 0
                anchors.top: parent.top
                anchors.left: browserEditButton.visible ? browserEditButton.right : importDashboardButton.right
                anchors.topMargin: App.Spacing.mediaRoomMargin
                anchors.leftMargin: dp(8)
                height: App.Spacing.bottomBarNavButtonHeight
                width: restoreDefaultsLabel.implicitWidth + dp(24)
                radius: dpMin(8, 2)
                color: "transparent"
                border.color: App.Style.accent
                border.width: 1
                scale: restoreDefaultsMouse.pressed ? 0.9 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
                }

                Text {
                    id: restoreDefaultsLabel
                    anchors.centerIn: parent
                    text: "Restore defaults"
                    color: App.Style.accent
                    font.pixelSize: App.Spacing.overallText
                    font.bold: true
                    font.family: obdPage.globalFont
                }

                MouseArea {
                    id: restoreDefaultsMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: obdPage.restoreDefaultDashboards()
                }
            }

            Flickable {
                id: chooserScroll
                objectName: "chooserScroll"
                anchors {
                    top: chooserHeader.bottom
                    bottom: parent.bottom
                    left: parent.left
                    right: parent.right
                    topMargin: dp(10)
                    leftMargin: dp(18)
                    rightMargin: dp(18)
                    bottomMargin: dp(18)
                }
                contentWidth: width
                contentHeight: chooserGrid.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                clip: true
                boundsBehavior: Flickable.DragAndOvershootBounds
                flickDeceleration: 1200
                maximumFlickVelocity: 4000
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

                GridLayout {
                    id: chooserGrid
                    width: chooserScroll.width
                    columns: 2
                    rowSpacing: dp(14)
                    columnSpacing: dp(14)

                    Repeater {
                        model: obdPage.dashboardRegistry
                        delegate: Rectangle {
                            id: card
                            objectName: "dashCard_" + modelData.id

                            // The dashboard entry — aliased so the nested
                            // action-button Repeater (whose own modelData
                            // shadows this one) can still reach it.
                            readonly property var dash: modelData

                            // Two-tap delete confirm ("Delete" → "Sure?").
                            property bool confirmingDelete: false
                            Timer {
                                id: deleteConfirmTimer
                                interval: 3000
                                onTriggered: card.confirmingDelete = false
                            }

                            function triggerAction(action) {
                                if (action === "customize") {
                                    // A built-in is read-only: the editor opens
                                    // on an unsaved copy, saved under its own id
                                    // on Done ("Sport (mine)")
                                    var spec = dashboardManager.loadDashboard(card.dash.id)
                                    if (!spec || !spec.cells) return
                                    spec.label = (spec.label || card.dash.label) + " (mine)"
                                    delete spec.id
                                    dashboardChooserPopup.close()
                                    obdPage.stackView.push("dashboards/editor/DashboardEditor.qml", {
                                        stackView: obdPage.stackView,
                                        callerPage: obdPage,
                                        initialSpec: spec
                                    })
                                } else if (action === "edit") {
                                    dashboardChooserPopup.close()
                                    obdPage.stackView.push("dashboards/editor/DashboardEditor.qml", {
                                        stackView: obdPage.stackView,
                                        callerPage: obdPage,
                                        editingId: card.dash.id
                                    })
                                } else if (action === "share") {
                                    shareMenu.dashId = card.dash.id
                                    shareMenu.popup()
                                } else if (action === "duplicate") {
                                    // Empty label → manager appends " (Copy)".
                                    dashboardManager.duplicateDashboard(card.dash.id, "")
                                } else if (action === "delete") {
                                    if (!card.confirmingDelete) {
                                        card.confirmingDelete = true
                                        deleteConfirmTimer.restart()
                                        return
                                    }
                                    card.confirmingDelete = false
                                    obdPage.deleteUserDashboard(card.dash.id)
                                }
                            }

                            Layout.fillWidth: true
                            Layout.preferredHeight: width * 0.72
                            radius: dpMin(12, 4)
                            color: obdPage.activeDashboardId === modelData.id
                                   ? Qt.lighter(App.Style.obdBoxBackground, 1.18)
                                   : (choiceMouse.containsMouse
                                      ? Qt.lighter(App.Style.obdBoxBackground, 1.1)
                                      : Qt.darker(App.Style.obdBoxBackground, 1.05))
                            border.color: obdPage.activeDashboardId === modelData.id
                                          ? App.Style.obdBarColor
                                          : Qt.darker(App.Style.obdBarColor, 1.5)
                            border.width: obdPage.activeDashboardId === modelData.id ? 3 : 1

                            Behavior on color { ColorAnimation { duration: 120 } }
                            Behavior on border.width { NumberAnimation { duration: 120 } }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: dp(10)
                                spacing: dp(8)

                                // Preview "screen" — shows a scaled miniature of the
                                // real dashboard QML. For the built-in "grid" entry
                                // (Parameter Cards, no standalone QML), falls back to
                                // the schematic gridPreview Component.
                                Rectangle {
                                    id: previewScreen
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: dpMin(8, 2)
                                    color: App.Style.backgroundColor
                                    clip: true

                                    // Schematic fallback for Parameter Cards
                                    Loader {
                                        anchors.fill: parent
                                        anchors.margins: dp(8)
                                        active: modelData.id === "grid"
                                        sourceComponent: active ? gridPreview : null
                                    }

                                    // Live miniature for everything else: render
                                    // the real dashboard at full natural size and
                                    // scale it down to fit the card. Loader is
                                    // active while the popup is open (starting at
                                    // aboutToShow so instantiation overlaps the
                                    // popup enter animation), and the contents
                                    // fade in only after gauge bindings have
                                    // settled — hides the 140ms 0→value animation
                                    // baked into the gauge primitives.
                                    Item {
                                        id: miniStage
                                        visible: modelData.id !== "grid"
                                        width: Math.max(1, obdPage.width)
                                        height: Math.max(1, obdPage.height - dp(80))
                                        anchors.centerIn: parent
                                        transformOrigin: Item.Center
                                        scale: Math.min(
                                            previewScreen.width / Math.max(1, width),
                                            previewScreen.height / Math.max(1, height)
                                        )

                                        property bool settled: false
                                        opacity: settled ? 1 : 0
                                        Behavior on opacity {
                                            NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
                                        }

                                        // Miniature is a DashboardRenderer bound to the
                                        // same JSON spec the main view uses — guaranteed
                                        // 1:1 with what the user will see full-size.
                                        Dashboards.DashboardRenderer {
                                            id: miniRenderer
                                            anchors.fill: parent
                                            spec: (miniStage.visible
                                                   && dashboardChooserPopup.preloadMinis
                                                   && typeof dashboardManager !== "undefined"
                                                   && dashboardManager)
                                                  ? dashboardManager.loadDashboard(modelData.id)
                                                  : null

                                            onSpecChanged: {
                                                if (spec && Object.keys(spec).length > 0) {
                                                    settleTimer.restart()
                                                } else {
                                                    miniStage.settled = false
                                                }
                                            }
                                        }

                                        Timer {
                                            id: settleTimer
                                            interval: 180
                                            onTriggered: miniStage.settled = true
                                        }

                                        // Reset on close so the fade replays next open
                                        Connections {
                                            target: dashboardChooserPopup
                                            function onClosed() { miniStage.settled = false }
                                        }
                                    }
                                }

                                // Label row with active indicator dot
                                Row {
                                    Layout.alignment: Qt.AlignHCenter
                                    spacing: dp(6)

                                    Rectangle {
                                        visible: obdPage.activeDashboardId === modelData.id
                                        width: dp(8)
                                        height: width
                                        radius: width / 2
                                        color: App.Style.obdBarColor
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text: modelData.label
                                        color: obdPage.activeDashboardId === modelData.id
                                               ? App.Style.obdBarColor
                                               : App.Style.obdValueColor
                                        font.pixelSize: App.Spacing.overallText
                                        font.bold: true
                                        font.family: obdPage.globalFont
                                    }
                                }
                            }

                            MouseArea {
                                id: choiceMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    obdPage.setActiveDashboard(modelData.id)
                                    dashboardChooserPopup.close()
                                }
                            }

                            // Card actions (Phase 3 Milestone C): always-visible
                            // buttons, no long-press (discoverability-hostile in
                            // a glance-and-go in-car UI — see roadmap). Duplicate
                            // and Delete on every JSON dashboard (deleting a
                            // built-in hides it until "Restore defaults"); Edit
                            // only on user-authored ones. The "grid" sentinel
                            // (Parameter Cards) has no JSON spec and can't be
                            // deleted, so no actions at all.
                            // One line at the card's right while it fits
                            // (oneLine); in a narrow card (an upright window)
                            // the buttons wrap inside it. As a plain Row they
                            // ran over the neighbouring card, cut at the left.
                            Flow {
                                id: cardActions
                                visible: card.dash.id !== "grid"
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.topMargin: dp(8)
                                anchors.rightMargin: dp(8)
                                spacing: dp(6)
                                z: 2
                                // Summed over children, which notifies as
                                // each button arrives. Repeater.itemAt() does
                                // not: on a page pre-built in the background
                                // the buttons are created after the Repeater
                                // reports its count, so the sum stayed 0 and
                                // they stacked outside the card (0.9.6-rc3).
                                readonly property real oneLine: {
                                    var w = 0, n = 0
                                    for (var i = 0; i < children.length; i++) {
                                        var b = children[i]
                                        if (b.shown === true) { w += b.width; n++ }
                                    }
                                    return w + Math.max(0, n - 1) * spacing
                                }
                                width: Math.min(Math.ceil(oneLine), card.width - dp(16))

                                Repeater {
                                    id: cardActionButtons
                                    model: [
                                        { "action": "edit",      "label": "Edit",   "danger": false, "show": card.dash.builtIn === false },
                                        { "action": "customize", "label": "Customize", "danger": false, "show": card.dash.builtIn === true },
                                        { "action": "duplicate", "label": "Copy",   "danger": false, "show": true },
                                        { "action": "share",     "label": "Share",  "danger": false, "show": true },
                                        { "action": "delete",    "label": "Delete", "danger": true,  "show": true }
                                    ]
                                    delegate: Rectangle {
                                        objectName: "dashCard_" + card.dash.id + "_" + modelData.action
                                        readonly property bool shown: modelData.show
                                        visible: shown
                                        width: actionLabel.implicitWidth + dp(16)
                                        height: actionLabel.implicitHeight + dp(10)
                                        radius: dpMin(6, 2)
                                        color: Qt.rgba(0, 0, 0, actionMouse.pressed ? 0.7 : 0.45)
                                        border.color: modelData.danger ? App.Style.statusDanger : App.Style.accent
                                        border.width: 1

                                        Text {
                                            id: actionLabel
                                            anchors.centerIn: parent
                                            text: (modelData.action === "delete" && card.confirmingDelete)
                                                  ? "Sure?" : modelData.label
                                            color: modelData.danger ? App.Style.statusDanger : App.Style.accent
                                            font.pixelSize: App.Spacing.overallText * 0.75
                                            font.bold: true
                                            font.family: obdPage.globalFont
                                        }

                                        MouseArea {
                                            id: actionMouse
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: card.triggerAction(modelData.action)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // RPM shift light editor (the RPM card opens it)
    Obd.ShiftLightEditor {
        id: rpmSettingsPopup
        fontFamily: obdPage.globalFont
    }
}
