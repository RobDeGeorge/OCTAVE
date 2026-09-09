import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Shapes 1.15
import Qt5Compat.GraphicalEffects
import "." as App
import "gauges" as Gauges
import "dashboards" as Dashboards

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: obdPage
    objectName: "obdMenu"
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
    property bool useCircularCards: settingsManager ? settingsManager.get_setting_with_default("obdCardStyleCircular", false) : false

    // Active dashboard: "grid" = default parameter cards, or one of the registered dashboards
    property string activeDashboardId: settingsManager
        ? settingsManager.get_setting_with_default("activeDashboard", "grid")
        : "grid"

    // Dashboard registry — built-in "grid" (parameter-cards view in this
    // file) plus every JSON-defined dashboard enumerated by DashboardManager
    // (presets + user-authored files). Ids like "sport"/"minimal"/"fullgrid"
    // still resolve to the same dashboards — they just render from JSON via
    // DashboardRenderer now instead of hand-written QML. See
    // TODO/dashboards-roadmap.md for the full Phase 2 design.
    readonly property var _gridSentinel:
        ({ id: "grid", label: "Parameter Cards", builtIn: true })

    property var dashboardRegistry: _rebuildRegistry()

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

    // Delete a user dashboard from the chooser. Lives on the page (not in the
    // card delegate) because dashboardsChanged rebuilds the registry
    // synchronously inside deleteDashboard(), which destroys the delegate that
    // tapped Delete — any code after the call in the delegate context throws
    // "obdPage is not defined". Active-dashboard fallback happens in the
    // onDashboardsChanged handler.
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

    // Live OBD values from singleton (updated via signal connections there)
    property var paramValues: App.OBDParameterModel.paramValues
    
    // Just update column count when parameters change
    function updateLayout() {
        // Count visible parameters
        let visibleCount = 0;
        for (let i = 0; i < allParameters.length; i++) {
            const param = allParameters[i];
            if (settingsManager && settingsManager.get_obd_parameter_enabled(param.id, true)) {
                visibleCount++;
            }
        }
        
        // Determine column count based on visible parameters
        if (visibleCount <= 4) {
            parametersGrid.columns = 2;
        } else if (visibleCount <= 9) {
            parametersGrid.columns = 3;
        } else {
            parametersGrid.columns = 4;
        }
    }
    
    Rectangle {
        anchors.fill: parent
        color: backgroundColor

        // ── Header bar ─────────────────────────────────────────────────
        // Hosts the current dashboard's label and the chooser button.
        // Content sits below this.
        Rectangle {
            id: obdHeader
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            // Match MediaPlayer's titleBar height exactly so the chooser
            // button lines up pixel-for-pixel with the YouTube Music
            // download button: see MediaPlayer.qml:277.
            height: App.Spacing.bottomBarNavButtonHeight + App.Spacing.mediaRoomMargin * 2
            color: Qt.darker(obdPage.backgroundColor, 1.12)
            z: 2

            Text {
                anchors.centerIn: parent
                text: obdPage.activeDashboardLabel
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
        // Rectangle (NOT to obdHeader) with the identical top/right
        // margins the YouTube Music download button uses in
        // MediaPlayer.qml:650-655. Same parent-relative anchors + same
        // margins + same button size = exact X/Y match across pages.
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
        Dashboards.DashboardRenderer {
            id: dashboardRenderer
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
            visible: obdPage.activeDashboardId !== "grid"
            spec: (visible && typeof dashboardManager !== "undefined" && dashboardManager)
                  ? dashboardManager.loadDashboard(obdPage.activeDashboardId)
                  : null
        }

        GridLayout {
            id: parametersGrid
            visible: obdPage.activeDashboardId === "grid"
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
            columns: 3
            rowSpacing: dp(10)
            columnSpacing: dp(10)
            
            // Use Repeater to create parameter cards
            Repeater {
                model: allParameters
                
                Item {
                    id: cardContainer
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: settingsManager ? settingsManager.get_obd_parameter_enabled(modelData.id, true) : true

                    // Update layout when visibility changes
                    onVisibleChanged: {
                        if (updateTimer.running) {
                            updateTimer.restart();
                        } else {
                            updateTimer.start();
                        }
                    }

                    // Only take up space when visible
                    Layout.preferredWidth: visible ? implicitWidth : 0
                    Layout.preferredHeight: visible ? implicitHeight : 0

                    // Animated display value - fast rolling effect
                    property real targetValue: paramValues[modelData.id] || 0
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

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: dp(8)
                            spacing: dp(4)

                            Text {
                                text: modelData.title
                                color: labelColor
                                font.pixelSize: App.Spacing.overallText
                                font.family: obdPage.globalFont
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Text {
                                text: cardContainer.displayValue.toFixed(1) + " " + modelData.unit
                                color: textColor
                                font.pixelSize: App.Spacing.overallText
                                font.bold: true
                                font.family: obdPage.globalFont
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: App.Spacing.overallSliderHeight * .5
                                color: Qt.darker(backgroundColor, 1.1)
                                radius: 3
                                Layout.topMargin: dp(4)

                                Rectangle {
                                    id: progressBar
                                    height: parent.height
                                    radius: 3
                                    color: App.Style.obdBarColor
                                    width: {
                                        const value = paramValues[modelData.id] || 0;
                                        return Math.max(6, parent.width * Math.min(1,
                                            (value - modelData.min) / (modelData.max - modelData.min)));
                                    }

                                    Behavior on width {
                                        NumberAnimation {
                                            duration: 100
                                            easing.type: Easing.OutCubic
                                        }
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
                            const value = paramValues[modelData.id] || 0;
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
                            visible: circularCard.animatedGaugeValue > 0.001

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
                                text: cardContainer.displayValue.toFixed(1)
                                color: textColor
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
        function onGenericSettingChanged(key) {
            // Update card style when setting changes
            if (key === "obdCardStyleCircular") {
                obdPage.useCircularCards = settingsManager.get_setting_with_default("obdCardStyleCircular", false)
            }
        }
    }
    
    // Listen for window size changes
    Connections {
        target: parent
        function onWidthChanged() { updateTimer.restart(); }
        function onHeightChanged() { updateTimer.restart(); }
    }
    
    // Initialize layout
    Component.onCompleted: {
        updateTimer.start();
    }

    // ── Dashboard Chooser Popup ──────────────────────────────────────
    Popup {
        id: dashboardChooserPopup
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        // Sized to match primitivesGalleryPopup so both popups feel the
        // same — consistent visual weight when switching between them.
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

            // Header strip — sized to the button + margins so the title row
            // and the top-right primitives button share the same frame and
            // can't overlap when the popup narrows. Mirrors obdHeader.
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
                    onClicked: {
                        dashboardChooserPopup.close()
                        obdPage.stackView.push("dashboards/editor/DashboardEditor.qml", {
                            stackView: obdPage.stackView,
                            callerPage: obdPage
                        })
                    }
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

            // Primitives gallery button — temporary dev/showcase screen.
            // Anchored with the same top/right margins as the OBDMenu
            // dashboards button so it sits centered within the header strip.
            // Remove this once the Phase 3 in-app dashboard editor lands.
            Control {
                id: primitivesButton
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: App.Spacing.mediaRoomMargin
                anchors.rightMargin: App.Spacing.mediaRoomMargin
                width: App.Spacing.bottomBarNavButtonWidth
                height: App.Spacing.bottomBarNavButtonHeight

                background: Rectangle {
                    color: "transparent"
                    radius: dpMin(8, 2)
                    border.color: App.Style.accent
                    border.width: 1
                    scale: primitivesButtonMouse.pressed ? 0.8 : 1.0
                    opacity: primitivesButtonMouse.pressed ? 0.7 : 1.0
                    Behavior on scale {
                        NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
                    }
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                }

                contentItem: Item {
                    scale: primitivesButtonMouse.pressed ? 0.8 : 1.0
                    opacity: primitivesButtonMouse.pressed ? 0.7 : 1.0
                    Behavior on scale {
                        NumberAnimation { duration: 200; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
                    }
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                    Image {
                        id: primitivesButtonImage
                        anchors.centerIn: parent
                        width: parent.width * 0.7
                        height: parent.height * 0.7
                        source: "./assets/primitives_button.svg"
                        sourceSize: Qt.size(width * 2, height * 2)
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        antialiasing: true
                        mipmap: true
                        visible: false
                    }
                    ColorOverlay {
                        anchors.fill: primitivesButtonImage
                        source: primitivesButtonImage
                        color: App.Style.accent
                    }
                }

                MouseArea {
                    id: primitivesButtonMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: primitivesGalleryPopup.open()
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
                                if (action === "edit") {
                                    dashboardChooserPopup.close()
                                    obdPage.stackView.push("dashboards/editor/DashboardEditor.qml", {
                                        stackView: obdPage.stackView,
                                        callerPage: obdPage,
                                        editingId: card.dash.id
                                    })
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
                            // on every JSON dashboard; Edit/Delete only on
                            // user-authored ones. The "grid" sentinel (Parameter
                            // Cards) has no JSON spec, so no actions at all.
                            Row {
                                visible: card.dash.id !== "grid"
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.topMargin: dp(8)
                                anchors.rightMargin: dp(8)
                                spacing: dp(6)
                                z: 2

                                Repeater {
                                    model: [
                                        { "action": "edit",      "label": "Edit",   "danger": false, "show": card.dash.builtIn === false },
                                        { "action": "duplicate", "label": "Copy",   "danger": false, "show": true },
                                        { "action": "delete",    "label": "Delete", "danger": true,  "show": card.dash.builtIn === false }
                                    ]
                                    delegate: Rectangle {
                                        objectName: "dashCard_" + card.dash.id + "_" + modelData.action
                                        visible: modelData.show
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

    // ── Primitives Gallery Popup ─────────────────────────────────────
    // Temporary dev/showcase screen — live preview of every gauge
    // primitive with hardcoded demo values, so you can see what each
    // one looks like regardless of OBD connection state. Tear this out
    // once the Phase 3 in-app editor ships (see TODO/dashboards-roadmap.md).
    Popup {
        id: primitivesGalleryPopup
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent.width * 0.92, dp(960))
        height: Math.min(
            parent.height * 0.9,
            galleryTitle.height + gallerySubtitle.height + galleryGrid.implicitHeight + dp(80)
        )
        modal: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Qt.rgba(App.Style.contentColor.r, App.Style.contentColor.g, App.Style.contentColor.b, 0.97)
            radius: App.Spacing.overallMargin
            border.color: App.Style.accent
            border.width: 2
        }

        contentItem: Item {
            Text {
                id: galleryTitle
                anchors.top: parent.top
                anchors.topMargin: dp(18)
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Primitives Gallery"
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText * 1.3
                font.bold: true
                font.family: obdPage.globalFont
            }

            Text {
                id: gallerySubtitle
                anchors.top: galleryTitle.bottom
                anchors.topMargin: dp(4)
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Live preview of every gauge primitive"
                color: App.Style.obdLabelColor
                font.pixelSize: App.Spacing.overallText * 0.85
                font.family: obdPage.globalFont
            }

            Flickable {
                id: galleryScroll
                anchors {
                    top: gallerySubtitle.bottom
                    bottom: parent.bottom
                    left: parent.left
                    right: parent.right
                    topMargin: dp(16)
                    leftMargin: dp(18)
                    rightMargin: dp(18)
                    bottomMargin: dp(18)
                }
                contentWidth: width
                contentHeight: galleryGrid.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                clip: true
                boundsBehavior: Flickable.DragAndOvershootBounds
                flickDeceleration: 1200
                maximumFlickVelocity: 4000
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

                GridLayout {
                    id: galleryGrid
                    width: galleryScroll.width
                    columns: 2
                    rowSpacing: dp(14)
                    columnSpacing: dp(14)

                    // ── CircularGauge ───────────────────────────────
                    // Demo value pushed into the redline zone so the
                    // needle points hard-right instead of straight up —
                    // otherwise it lands at the same angle as the
                    // ArcGauge tile's needle and the two look identical.
                    GalleryTile {
                        title: "CircularGauge"
                        props: "paramId: RPM · showNeedle · redlineStart: 6500 (demo in redline)"
                        Gauges.CircularGauge {
                            anchors.centerIn: parent
                            width: Math.min(parent.width, parent.height) * 0.92
                            height: width
                            paramId: "RPM"
                            value: 7200
                            showNeedle: true
                            redlineStart: 6500
                        }
                    }

                    // ── ArcGauge ────────────────────────────────────
                    GalleryTile {
                        title: "ArcGauge"
                        props: "paramId: SPEED · showNeedle · 180° top arc"
                        Gauges.ArcGauge {
                            anchors.fill: parent
                            anchors.margins: dp(6)
                            paramId: "SPEED"
                            value: 65
                            showNeedle: true
                        }
                    }

                    // ── BarGauge (horizontal, with warn) ────────────
                    GalleryTile {
                        title: "BarGauge — horizontal"
                        props: "paramId: COOLANT_TEMP · warnAbove: 105"
                        Item {
                            anchors.fill: parent
                            anchors.margins: dp(14)
                            Gauges.BarGauge {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                height: dp(56)
                                paramId: "COOLANT_TEMP"
                                value: 92
                                warnAbove: 105
                                orientation: "horizontal"
                            }
                        }
                    }

                    // ── BarGauge (vertical) ─────────────────────────
                    GalleryTile {
                        title: "BarGauge — vertical"
                        props: "paramId: FUEL_LEVEL · orientation: vertical"
                        Item {
                            anchors.fill: parent
                            anchors.margins: dp(14)
                            Gauges.BarGauge {
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: dp(90)
                                paramId: "FUEL_LEVEL"
                                value: 65
                                orientation: "vertical"
                            }
                        }
                    }

                    // ── LinearGauge (bidirectional) ─────────────────
                    GalleryTile {
                        title: "LinearGauge"
                        props: "paramId: SHORT_FUEL_TRIM_1 · bidirectional (min<0<max)"
                        Gauges.LinearGauge {
                            anchors.fill: parent
                            anchors.margins: dp(14)
                            paramId: "SHORT_FUEL_TRIM_1"
                            value: 6
                        }
                    }

                    // ── DigitalReadout ──────────────────────────────
                    GalleryTile {
                        title: "DigitalReadout"
                        props: "paramId: SPEED · padDigits: 3 · valueScale: 5"
                        Gauges.DigitalReadout {
                            anchors.fill: parent
                            paramId: "SPEED"
                            value: 65
                            padDigits: 3
                            valueScale: 5
                        }
                    }

                    // ── SparklineGauge (live) ───────────────────────
                    GalleryTile {
                        title: "SparklineGauge"
                        props: "paramId: ENGINE_LOAD · live 500ms sampling · fillBelow"
                        Gauges.SparklineGauge {
                            anchors.fill: parent
                            anchors.margins: dp(12)
                            paramId: "ENGINE_LOAD"
                            // Left live — in dev mode you see real motion;
                            // on hardware without OBD, flat line at 0.
                        }
                    }

                    // ── WarningLight (off + on + pulse) ─────────────
                    GalleryTile {
                        title: "WarningLight"
                        props: "triggerAbove/Below · pulse (right is lit)"
                        Row {
                            anchors.centerIn: parent
                            spacing: dp(20)
                            Item {
                                width: dp(72); height: width
                                Gauges.WarningLight {
                                    anchors.fill: parent
                                    paramId: "COOLANT_TEMP"
                                    value: 85            // below trigger — off
                                    triggerAbove: 110
                                    label: "TEMP"
                                }
                            }
                            Item {
                                width: dp(72); height: width
                                Gauges.WarningLight {
                                    anchors.fill: parent
                                    paramId: "COOLANT_TEMP"
                                    value: 115           // above trigger — lit
                                    triggerAbove: 110
                                    label: "TEMP"
                                    pulse: true
                                }
                            }
                            Item {
                                width: dp(72); height: width
                                Gauges.WarningLight {
                                    anchors.fill: parent
                                    paramId: "FUEL_LEVEL"
                                    value: 8
                                    triggerBelow: 12
                                    label: "FUEL"
                                    activeColor: "#F1C40F"   // amber
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // RPM Settings Popup
    Popup {
        id: rpmSettingsPopup
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: parent.width * 0.92
        height: parent.height * 0.95
        modal: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // RPM Settings state
        property bool shiftLightEnabled: true
        property bool showOnHomeCard: true       // Show indicator on home OBD card
        property int maxRpm: 8000
        property int selectedFlagIndex: -1  // Which flag is selected for editing (-1 = none)
        property real fullScreenFlashOpacity: 0.5

        // Shift light appearance settings
        property real shiftLightSize: 0.5        // Size as percentage of card height (0.25 - 1.0)
        property real glowSize: 0.6              // Inner glow size percentage (0.0 - 1.0)
        property real glowIntensity: 0.6         // Inner glow opacity (0.0 - 1.0)
        property int colorTransitionSpeed: 100   // Color animation duration in ms (0 - 500)
        property bool pulseEnabled: false        // Enable pulse animation when active

        // Flags list - each flag has: rpmLow, rpmHigh, color, flash, flashSpeed, fullScreenFlash, fullScreenFlashOpacity, auraFlash, auraFlashOpacity, auraSpread
        property var flags: []

        // Default colors for new flags
        property var defaultColors: ["#00FF00", "#FFFF00", "#FF8800", "#FF0000", "#FF00FF", "#00FFFF"]

        // Load settings when popup opens
        onOpened: {
            if (settingsManager) {
                shiftLightEnabled = settingsManager.get_setting_with_default("rpm_shift_light_enabled", true)
                showOnHomeCard = settingsManager.get_setting_with_default("rpm_show_on_home_card", true)
                maxRpm = settingsManager.get_setting_with_default("rpm_max_rpm", 8000)
                fullScreenFlashOpacity = settingsManager.get_setting_with_default("rpm_fullscreen_flash_opacity", 0.5)
                // Load appearance settings
                shiftLightSize = settingsManager.get_setting_with_default("rpm_shift_light_size", 0.5)
                glowSize = settingsManager.get_setting_with_default("rpm_glow_size", 0.6)
                glowIntensity = settingsManager.get_setting_with_default("rpm_glow_intensity", 0.6)
                colorTransitionSpeed = settingsManager.get_setting_with_default("rpm_color_transition_speed", 100)
                pulseEnabled = settingsManager.get_setting_with_default("rpm_pulse_enabled", false)
                var savedFlags = settingsManager.get_setting_with_default("rpm_flags", "[]")
                try {
                    flags = JSON.parse(savedFlags)
                    // Migrate old format (single rpm) to new format (rpmLow/rpmHigh)
                    for (var i = 0; i < flags.length; i++) {
                        if (flags[i].rpm !== undefined && flags[i].rpmLow === undefined) {
                            flags[i].rpmLow = flags[i].rpm
                            flags[i].rpmHigh = maxRpm  // Default high to max
                            delete flags[i].rpm
                        }
                    }
                } catch(e) {
                    flags = []
                }
                selectedFlagIndex = -1
            }
        }

        // Save flags to settings
        function saveFlags() {
            if (settingsManager) {
                settingsManager.save_setting("rpm_flags", JSON.stringify(flags))
            }
        }

        // Add a new flag at a default position
        function addFlag() {
            var newRpmLow = Math.round(maxRpm * 0.6 / 100) * 100  // Default to 60% of max
            var newRpmHigh = Math.round(maxRpm * 0.85 / 100) * 100  // Default to 85% of max
            var colorIndex = flags.length % defaultColors.length
            var newFlag = {
                rpmLow: newRpmLow,
                rpmHigh: newRpmHigh,
                color: defaultColors[colorIndex],
                flash: false,
                flashSpeed: 100,
                fullScreenFlash: false,
                fullScreenFlashOpacity: 0.5,
                auraFlash: false,
                auraFlashOpacity: 0.6,
                auraSpread: 0.18
            }
            var newFlags = flags.slice()  // Create a copy
            newFlags.push(newFlag)
            // Sort by low RPM
            newFlags.sort(function(a, b) { return a.rpmLow - b.rpmLow })
            flags = newFlags
            selectedFlagIndex = flags.indexOf(newFlag)
            saveFlags()
        }

        // Remove a flag
        function removeFlag(index) {
            if (index >= 0 && index < flags.length) {
                var newFlags = flags.slice()
                newFlags.splice(index, 1)
                flags = newFlags
                if (selectedFlagIndex >= flags.length) {
                    selectedFlagIndex = flags.length - 1
                }
                if (selectedFlagIndex < 0 && flags.length > 0) {
                    selectedFlagIndex = 0
                }
                saveFlags()
            }
        }

        // Update a flag property
        function updateFlag(index, property, value) {
            if (index >= 0 && index < flags.length) {
                var newFlags = flags.slice()
                newFlags[index] = Object.assign({}, newFlags[index])
                newFlags[index][property] = value
                // Ensure rpmLow <= rpmHigh
                if (property === "rpmLow" && newFlags[index].rpmLow > newFlags[index].rpmHigh) {
                    newFlags[index].rpmHigh = newFlags[index].rpmLow
                }
                if (property === "rpmHigh" && newFlags[index].rpmHigh < newFlags[index].rpmLow) {
                    newFlags[index].rpmLow = newFlags[index].rpmHigh
                }
                // Re-sort if low RPM changed
                if (property === "rpmLow") {
                    var updatedFlag = newFlags[index]
                    newFlags.sort(function(a, b) { return a.rpmLow - b.rpmLow })
                    selectedFlagIndex = newFlags.indexOf(updatedFlag)
                }
                flags = newFlags
                saveFlags()
            }
        }

        background: Rectangle {
            color: Qt.rgba(App.Style.contentColor.r, App.Style.contentColor.g, App.Style.contentColor.b, 0.95)
            radius: App.Spacing.overallMargin
            border.color: App.Style.accent
            border.width: 2

            layer.enabled: true
            layer.effect: DropShadow {
                horizontalOffset: 0
                verticalOffset: 6
                radius: 30
                samples: 31
                color: "#90000000"
            }
        }

        contentItem: Item {
            // Header at top
            Text {
                id: rpmPopupHeader
                text: "RPM Shift Light Settings"
                font.pixelSize: App.Spacing.overallText * 1.6
                font.bold: true
                font.family: obdPage.globalFont
                color: App.Style.primaryTextColor
                anchors.top: parent.top
                anchors.topMargin: App.Spacing.overallMargin
                anchors.horizontalCenter: parent.horizontalCenter
            }

            // Close button at bottom
            Rectangle {
                id: rpmCloseBtn
                width: App.Spacing.overallText * 10
                height: App.Spacing.overallText * 3
                radius: App.Spacing.overallMargin * 0.5
                color: rpmCloseButtonMouse.pressed ? Qt.darker(App.Style.accent, 1.2) :
                       rpmCloseButtonMouse.containsMouse ? Qt.lighter(App.Style.accent, 1.1) : App.Style.accent
                anchors.bottom: parent.bottom
                anchors.bottomMargin: App.Spacing.overallMargin
                anchors.horizontalCenter: parent.horizontalCenter

                Behavior on color { ColorAnimation { duration: 150 } }

                Text {
                    anchors.centerIn: parent
                    text: "Close"
                    font.pixelSize: App.Spacing.overallText * 1.4
                    font.bold: true
                    font.family: obdPage.globalFont
                    color: "white"
                }

                MouseArea {
                    id: rpmCloseButtonMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: rpmSettingsPopup.close()
                }
            }

            // Settings content in scrollable area
            ScrollView {
                id: rpmSettingsScrollView
                anchors.top: rpmPopupHeader.bottom
                anchors.topMargin: App.Spacing.overallSpacing
                anchors.bottom: rpmCloseBtn.top
                anchors.bottomMargin: App.Spacing.overallSpacing
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: App.Spacing.overallMargin
                anchors.rightMargin: App.Spacing.overallMargin
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AlwaysOff

                Column {
                    width: rpmSettingsScrollView.width
                    spacing: App.Spacing.overallSpacing

                    // === SHIFT LIGHT ENABLE/DISABLE ===
                    Rectangle {
                        width: parent.width
                        height: dp(60)
                        radius: App.Spacing.overallMargin * 0.5
                        color: shiftLightRowMouse.containsMouse ? Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3) : "transparent"

                        MouseArea {
                            id: shiftLightRowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                rpmSettingsPopup.shiftLightEnabled = !rpmSettingsPopup.shiftLightEnabled
                                if (settingsManager) {
                                    settingsManager.save_setting("rpm_shift_light_enabled", rpmSettingsPopup.shiftLightEnabled)
                                }
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: App.Spacing.overallMargin
                            anchors.rightMargin: App.Spacing.overallMargin
                            spacing: App.Spacing.overallSpacing * 2

                            Text {
                                text: "Enable Shift Light"
                                font.pixelSize: App.Spacing.overallText * 1.1
                                font.family: obdPage.globalFont
                                color: App.Style.primaryTextColor
                                Layout.fillWidth: true

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        rpmSettingsPopup.shiftLightEnabled = !rpmSettingsPopup.shiftLightEnabled
                                        if (settingsManager) {
                                            settingsManager.save_setting("rpm_shift_light_enabled", rpmSettingsPopup.shiftLightEnabled)
                                        }
                                    }
                                }
                            }

                            // Toggle switch - matches SettingsMenu styling exactly
                            Item {
                                Layout.preferredWidth: dp(80)
                                Layout.preferredHeight: dp(40)

                                // Main track
                                Rectangle {
                                    id: shiftLightTrack
                                    anchors.fill: parent
                                    radius: height / 2
                                    color: rpmSettingsPopup.shiftLightEnabled ?
                                        Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3) :
                                        Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3)

                                    // Subtle gradient overlay
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: parent.radius
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: "transparent" }
                                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.05) }
                                        }
                                    }

                                    // Animated highlight
                                    Rectangle {
                                        id: shiftLightHighlight
                                        width: rpmSettingsPopup.shiftLightEnabled ? parent.width : 0
                                        height: parent.height
                                        radius: parent.radius
                                        anchors.right: rpmSettingsPopup.shiftLightEnabled ? parent.right : undefined
                                        anchors.left: !rpmSettingsPopup.shiftLightEnabled ? parent.left : undefined
                                        color: App.Style.accent
                                        opacity: rpmSettingsPopup.shiftLightEnabled ? 0.5 : 0

                                        Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutQuad } }
                                        Behavior on opacity { NumberAnimation { duration: 300 } }
                                    }

                                    // ON/OFF text
                                    Text {
                                        anchors {
                                            left: rpmSettingsPopup.shiftLightEnabled ? undefined : parent.left
                                            right: rpmSettingsPopup.shiftLightEnabled ? parent.right : undefined
                                            margins: dp(10)
                                            verticalCenter: parent.verticalCenter
                                        }
                                        text: rpmSettingsPopup.shiftLightEnabled ? "ON" : "OFF"
                                        font.pixelSize: App.Spacing.overallText * 0.8
                                        font.bold: true
                                        font.family: obdPage.globalFont
                                        color: rpmSettingsPopup.shiftLightEnabled ? App.Style.accent :
                                            Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.7)
                                        visible: width < (parent.width - shiftLightHandle.width - dp(10))

                                        Behavior on color { ColorAnimation { duration: 200 } }
                                    }
                                }

                                // Handle
                                Rectangle {
                                    id: shiftLightHandle
                                    width: dp(40)
                                    height: dp(40)
                                    radius: width / 2
                                    x: rpmSettingsPopup.shiftLightEnabled ? parent.width - width : 0
                                    y: 0
                                    color: "white"

                                    // Inner indicator
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: parent.width * 0.4
                                        height: width
                                        radius: width / 2
                                        color: App.Style.accent
                                        opacity: rpmSettingsPopup.shiftLightEnabled ? 1 : 0
                                        scale: rpmSettingsPopup.shiftLightEnabled ? 1 : 0.5

                                        Behavior on opacity { NumberAnimation { duration: 200 } }
                                        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
                                    }

                                    layer.enabled: true
                                    layer.effect: DropShadow {
                                        verticalOffset: 2
                                        radius: 6.0
                                        samples: 17
                                        color: Qt.rgba(0, 0, 0, 0.2)
                                    }

                                    scale: shiftLightToggleMouse.pressed ? 0.95 : 1.0

                                    Behavior on x {
                                        NumberAnimation {
                                            duration: 300
                                            easing.type: Easing.OutBack
                                            easing.overshoot: 0.6
                                        }
                                    }
                                    Behavior on scale { NumberAnimation { duration: 100 } }
                                }

                                // Interactive area with pulse animation
                                MouseArea {
                                    id: shiftLightToggleMouse
                                    anchors.fill: parent
                                    onClicked: {
                                        rpmSettingsPopup.shiftLightEnabled = !rpmSettingsPopup.shiftLightEnabled
                                        if (settingsManager) {
                                            settingsManager.save_setting("rpm_shift_light_enabled", rpmSettingsPopup.shiftLightEnabled)
                                        }
                                    }
                                    onPressed: {
                                        shiftLightPulseAnimation.start()
                                    }

                                    SequentialAnimation {
                                        id: shiftLightPulseAnimation
                                        PropertyAnimation {
                                            target: shiftLightHandle
                                            property: "scale"
                                            to: 0.9
                                            duration: 100
                                        }
                                        PropertyAnimation {
                                            target: shiftLightHandle
                                            property: "scale"
                                            to: 1.0
                                            duration: 100
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // === SHOW ON HOME CARD TOGGLE ===
                    Rectangle {
                        width: parent.width
                        height: dp(60)
                        radius: App.Spacing.overallMargin * 0.5
                        color: showOnHomeCardRowMouse.containsMouse ? Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3) : "transparent"

                        MouseArea {
                            id: showOnHomeCardRowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                rpmSettingsPopup.showOnHomeCard = !rpmSettingsPopup.showOnHomeCard
                                if (settingsManager) {
                                    settingsManager.save_setting("rpm_show_on_home_card", rpmSettingsPopup.showOnHomeCard)
                                }
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: App.Spacing.overallMargin
                            anchors.rightMargin: App.Spacing.overallMargin
                            spacing: App.Spacing.overallSpacing * 2

                            Text {
                                text: "Show Indicator on Home"
                                font.pixelSize: App.Spacing.overallText * 1.1
                                font.family: obdPage.globalFont
                                color: App.Style.primaryTextColor
                                Layout.fillWidth: true

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        rpmSettingsPopup.showOnHomeCard = !rpmSettingsPopup.showOnHomeCard
                                        if (settingsManager) {
                                            settingsManager.save_setting("rpm_show_on_home_card", rpmSettingsPopup.showOnHomeCard)
                                        }
                                    }
                                }
                            }

                            // Toggle switch
                            Item {
                                Layout.preferredWidth: dp(80)
                                Layout.preferredHeight: dp(40)

                                Rectangle {
                                    id: showOnHomeCardTrack
                                    anchors.fill: parent
                                    radius: height / 2
                                    color: rpmSettingsPopup.showOnHomeCard ?
                                        Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3) :
                                        Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3)

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: parent.radius
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: "transparent" }
                                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.05) }
                                        }
                                    }

                                    Rectangle {
                                        width: rpmSettingsPopup.showOnHomeCard ? parent.width : 0
                                        height: parent.height
                                        radius: parent.radius
                                        anchors.right: rpmSettingsPopup.showOnHomeCard ? parent.right : undefined
                                        anchors.left: !rpmSettingsPopup.showOnHomeCard ? parent.left : undefined
                                        color: App.Style.accent
                                        opacity: rpmSettingsPopup.showOnHomeCard ? 0.5 : 0

                                        Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutQuad } }
                                        Behavior on opacity { NumberAnimation { duration: 300 } }
                                    }

                                    Text {
                                        anchors {
                                            left: rpmSettingsPopup.showOnHomeCard ? undefined : parent.left
                                            right: rpmSettingsPopup.showOnHomeCard ? parent.right : undefined
                                            margins: dp(10)
                                            verticalCenter: parent.verticalCenter
                                        }
                                        text: rpmSettingsPopup.showOnHomeCard ? "ON" : "OFF"
                                        font.pixelSize: App.Spacing.overallText * 0.8
                                        font.bold: true
                                        font.family: obdPage.globalFont
                                        color: rpmSettingsPopup.showOnHomeCard ? App.Style.accent :
                                            Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.7)
                                        visible: width < (parent.width - showOnHomeCardHandle.width - dp(10))

                                        Behavior on color { ColorAnimation { duration: 200 } }
                                    }
                                }

                                Rectangle {
                                    id: showOnHomeCardHandle
                                    width: dp(40)
                                    height: dp(40)
                                    radius: width / 2
                                    x: rpmSettingsPopup.showOnHomeCard ? parent.width - width : 0
                                    y: 0
                                    color: "white"

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: parent.width * 0.4
                                        height: width
                                        radius: width / 2
                                        color: App.Style.accent
                                        opacity: rpmSettingsPopup.showOnHomeCard ? 1 : 0
                                        scale: rpmSettingsPopup.showOnHomeCard ? 1 : 0.5

                                        Behavior on opacity { NumberAnimation { duration: 200 } }
                                        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
                                    }

                                    layer.enabled: true
                                    layer.effect: DropShadow {
                                        verticalOffset: 2
                                        radius: 6.0
                                        samples: 17
                                        color: Qt.rgba(0, 0, 0, 0.2)
                                    }

                                    scale: showOnHomeCardToggleMouse.pressed ? 0.95 : 1.0

                                    Behavior on x {
                                        NumberAnimation {
                                            duration: 300
                                            easing.type: Easing.OutBack
                                            easing.overshoot: 0.6
                                        }
                                    }
                                    Behavior on scale { NumberAnimation { duration: 100 } }
                                }

                                MouseArea {
                                    id: showOnHomeCardToggleMouse
                                    anchors.fill: parent
                                    onClicked: {
                                        rpmSettingsPopup.showOnHomeCard = !rpmSettingsPopup.showOnHomeCard
                                        if (settingsManager) {
                                            settingsManager.save_setting("rpm_show_on_home_card", rpmSettingsPopup.showOnHomeCard)
                                        }
                                    }
                                    onPressed: {
                                        showOnHomeCardPulseAnimation.start()
                                    }

                                    SequentialAnimation {
                                        id: showOnHomeCardPulseAnimation
                                        PropertyAnimation {
                                            target: showOnHomeCardHandle
                                            property: "scale"
                                            to: 0.9
                                            duration: 100
                                        }
                                        PropertyAnimation {
                                            target: showOnHomeCardHandle
                                            property: "scale"
                                            to: 1.0
                                            duration: 100
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // === MAX RPM SETTING ===
                    RowLayout {
                        width: parent.width
                        Layout.leftMargin: App.Spacing.overallMargin
                        Layout.rightMargin: App.Spacing.overallMargin

                        Text {
                            text: "Redline"
                            font.pixelSize: App.Spacing.overallText * 1.1
                            font.family: obdPage.globalFont
                            color: App.Style.primaryTextColor
                        }

                        Slider {
                            id: maxRpmSlider
                            Layout.fillWidth: true
                            implicitHeight: App.Spacing.overallSliderHeight * 2.5
                            from: 4000
                            to: 12000
                            stepSize: 500
                            value: rpmSettingsPopup.maxRpm
                            onMoved: {
                                rpmSettingsPopup.maxRpm = value
                                if (settingsManager) {
                                    settingsManager.save_setting("rpm_max_rpm", value)
                                }
                                // Clamp flag rpmHigh values that exceed new max
                                var needsSave = false
                                var newFlags = rpmSettingsPopup.flags.slice()
                                for (var i = 0; i < newFlags.length; i++) {
                                    if (newFlags[i].rpmHigh > value) {
                                        newFlags[i] = Object.assign({}, newFlags[i])
                                        newFlags[i].rpmHigh = value
                                        // Also clamp rpmLow if it exceeds new rpmHigh
                                        if (newFlags[i].rpmLow > value) {
                                            newFlags[i].rpmLow = value
                                        }
                                        needsSave = true
                                    }
                                }
                                if (needsSave) {
                                    rpmSettingsPopup.flags = newFlags
                                    rpmSettingsPopup.saveFlags()
                                }
                            }

                            background: Rectangle {
                                x: maxRpmSlider.leftPadding
                                y: maxRpmSlider.topPadding + maxRpmSlider.availableHeight / 2 - height / 2
                                width: maxRpmSlider.availableWidth
                                height: App.Spacing.overallSliderHeight * 0.5
                                radius: height / 2
                                color: Qt.darker(App.Style.contentColor, 1.2)

                                Rectangle {
                                    width: maxRpmSlider.visualPosition * parent.width
                                    height: parent.height
                                    color: "#FF0000"
                                    radius: height / 2
                                }
                            }

                            handle: Rectangle {
                                x: maxRpmSlider.leftPadding + maxRpmSlider.visualPosition * (maxRpmSlider.availableWidth - width)
                                y: maxRpmSlider.topPadding + maxRpmSlider.availableHeight / 2 - height / 2
                                width: App.Spacing.overallSliderHeight * 1.5
                                height: width
                                radius: width / 2
                                color: maxRpmSlider.pressed ? Qt.darker("white", 1.1) : "white"
                                border.color: "#FF0000"
                                border.width: 2
                            }
                        }

                        Text {
                            text: rpmSettingsPopup.maxRpm
                            font.pixelSize: App.Spacing.overallText * 1.1
                            font.bold: true
                            font.family: obdPage.globalFont
                            color: "#FF0000"
                            Layout.preferredWidth: App.Spacing.overallText * 4
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    // RPM Bar visualization with flags
                    Item {
                        width: parent.width
                        height: App.Spacing.overallText * 8

                        // The RPM bar background
                        Rectangle {
                            id: rpmBar
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: App.Spacing.overallMargin
                            anchors.rightMargin: App.Spacing.overallMargin
                            anchors.verticalCenter: parent.verticalCenter
                            height: App.Spacing.overallSliderHeight * 1.2
                            radius: height / 2
                            color: Qt.darker(App.Style.contentColor, 1.2)

                            // Gradient fill showing base RPM range
                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: Qt.rgba(0.2, 0.2, 0.2, 1) }
                                    GradientStop { position: 1.0; color: Qt.rgba(0.5, 0.1, 0.1, 1) }
                                }
                            }

                            // Range segments for each flag
                            Repeater {
                                model: rpmSettingsPopup.flags

                                Rectangle {
                                    id: rangeSegment
                                    property real lowPos: modelData.rpmLow / rpmSettingsPopup.maxRpm
                                    property real highPos: modelData.rpmHigh / rpmSettingsPopup.maxRpm
                                    x: lowPos * rpmBar.width
                                    width: (highPos - lowPos) * rpmBar.width
                                    height: rpmBar.height
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: Qt.rgba(Qt.lighter(modelData.color, 1.2).r, Qt.lighter(modelData.color, 1.2).g, Qt.lighter(modelData.color, 1.2).b, 0.5)
                                    border.color: index === rpmSettingsPopup.selectedFlagIndex ? "white" : modelData.color
                                    border.width: index === rpmSettingsPopup.selectedFlagIndex ? 3 : 2
                                    radius: rpmBar.radius

                                    // Flag number label on segment
                                    Text {
                                        anchors.centerIn: parent
                                        text: (index + 1).toString()
                                        font.pixelSize: App.Spacing.overallText * 1.1
                                        font.bold: true
                                        font.family: obdPage.globalFont
                                        color: "white"
                                        visible: parent.width > App.Spacing.overallText * 2
                                    }

                                    // Click to select flag
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            rpmSettingsPopup.selectedFlagIndex = index
                                        }
                                    }
                                }
                            }

                            // RPM scale labels
                            Text {
                                anchors.left: parent.left
                                anchors.top: parent.bottom
                                anchors.topMargin: App.Spacing.overallSpacing * 0.5
                                text: "0"
                                font.pixelSize: App.Spacing.overallText * 0.9
                                font.family: obdPage.globalFont
                                color: Qt.darker(App.Style.primaryTextColor, 1.3)
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.bottom
                                anchors.topMargin: App.Spacing.overallSpacing * 0.5
                                text: Math.round(rpmSettingsPopup.maxRpm / 2).toString()
                                font.pixelSize: App.Spacing.overallText * 0.9
                                font.family: obdPage.globalFont
                                color: Qt.darker(App.Style.primaryTextColor, 1.3)
                            }

                            Text {
                                anchors.right: parent.right
                                anchors.top: parent.bottom
                                anchors.topMargin: App.Spacing.overallSpacing * 0.5
                                text: rpmSettingsPopup.maxRpm.toString()
                                font.pixelSize: App.Spacing.overallText * 0.9
                                font.family: obdPage.globalFont
                                color: Qt.darker(App.Style.primaryTextColor, 1.3)
                            }
                        }
                    }

                    // Add Flag Button
                    Rectangle {
                        width: App.Spacing.overallText * 12
                        height: App.Spacing.overallText * 3
                        radius: App.Spacing.overallMargin * 0.5
                        color: addFlagMouse.pressed ? Qt.darker(App.Style.accent, 1.2) :
                               addFlagMouse.containsMouse ? Qt.lighter(App.Style.accent, 1.1) : App.Style.accent
                        anchors.horizontalCenter: parent.horizontalCenter

                        Behavior on color { ColorAnimation { duration: 150 } }

                        Row {
                            anchors.centerIn: parent
                            spacing: App.Spacing.overallSpacing * 0.5

                            Text {
                                text: "+"
                                font.pixelSize: App.Spacing.overallText * 1.6
                                font.bold: true
                                font.family: obdPage.globalFont
                                color: "white"
                            }

                            Text {
                                text: "Add Flag"
                                font.pixelSize: App.Spacing.overallText * 1.4
                                font.bold: true
                                font.family: obdPage.globalFont
                                color: "white"
                            }
                        }

                        MouseArea {
                            id: addFlagMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: rpmSettingsPopup.addFlag()
                        }
                    }

                    // === SECTION DIVIDER ===
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.2)
                        visible: rpmSettingsPopup.flags.length > 0
                    }

                    // === SELECTED FLAG SETTINGS ===
                    Column {
                        id: flagSettingsColumn
                        width: parent.width
                        spacing: App.Spacing.overallSpacing
                        visible: rpmSettingsPopup.selectedFlagIndex >= 0 && rpmSettingsPopup.selectedFlagIndex < rpmSettingsPopup.flags.length

                        property var currentFlag: rpmSettingsPopup.selectedFlagIndex >= 0 && rpmSettingsPopup.selectedFlagIndex < rpmSettingsPopup.flags.length ?
                                                  rpmSettingsPopup.flags[rpmSettingsPopup.selectedFlagIndex] : null

                        RowLayout {
                            width: parent.width

                            Text {
                                text: "Flag " + (rpmSettingsPopup.selectedFlagIndex + 1) + " Settings"
                                font.pixelSize: App.Spacing.overallText * 1.6
                                font.bold: true
                                font.family: obdPage.globalFont
                                color: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.accent
                                leftPadding: App.Spacing.overallMargin
                                Layout.fillWidth: true
                            }

                            // Delete flag button
                            Rectangle {
                                Layout.preferredWidth: App.Spacing.overallText * 8
                                Layout.preferredHeight: App.Spacing.overallText * 2.5
                                Layout.rightMargin: App.Spacing.overallMargin
                                radius: App.Spacing.overallMargin * 0.5
                                color: deleteFlagMouse.pressed ? Qt.darker("#FF4444", 1.2) :
                                       deleteFlagMouse.containsMouse ? Qt.lighter("#FF4444", 1.1) : "#FF4444"

                                Behavior on color { ColorAnimation { duration: 150 } }

                                Text {
                                    anchors.centerIn: parent
                                    text: "Delete"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.bold: true
                                    font.family: obdPage.globalFont
                                    color: "white"
                                }

                                MouseArea {
                                    id: deleteFlagMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: rpmSettingsPopup.removeFlag(rpmSettingsPopup.selectedFlagIndex)
                                }
                            }
                        }

                        // RPM Low Value Slider for selected flag
                        Column {
                            width: parent.width
                            spacing: App.Spacing.overallSpacing * 0.5

                            RowLayout {
                                width: parent.width

                                Text {
                                    text: "RPM Low (Start)"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    Layout.fillWidth: true
                                    leftPadding: App.Spacing.overallMargin
                                }

                                Text {
                                    text: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.rpmLow + " RPM" : ""
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.bold: true
                                    font.family: obdPage.globalFont
                                    color: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.primaryTextColor
                                    rightPadding: App.Spacing.overallMargin
                                }
                            }

                            Slider {
                                id: flagRpmLowSlider
                                width: parent.width - App.Spacing.overallMargin * 2
                                anchors.horizontalCenter: parent.horizontalCenter
                                implicitHeight: App.Spacing.overallSliderHeight * 2.5
                                from: 0
                                to: rpmSettingsPopup.maxRpm
                                stepSize: 100
                                value: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.rpmLow : 0
                                onMoved: {
                                    rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "rpmLow", value)
                                }

                                background: Rectangle {
                                    x: flagRpmLowSlider.leftPadding
                                    y: flagRpmLowSlider.topPadding + flagRpmLowSlider.availableHeight / 2 - height / 2
                                    width: flagRpmLowSlider.availableWidth
                                    height: App.Spacing.overallSliderHeight * 0.5
                                    radius: height / 2
                                    color: Qt.darker(App.Style.contentColor, 1.2)

                                    Rectangle {
                                        width: flagRpmLowSlider.visualPosition * parent.width
                                        height: parent.height
                                        color: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.accent
                                        radius: height / 2
                                    }
                                }

                                handle: Rectangle {
                                    x: flagRpmLowSlider.leftPadding + flagRpmLowSlider.visualPosition * (flagRpmLowSlider.availableWidth - width)
                                    y: flagRpmLowSlider.topPadding + flagRpmLowSlider.availableHeight / 2 - height / 2
                                    width: App.Spacing.overallSliderHeight * 1.5
                                    height: width
                                    radius: width / 2
                                    color: flagRpmLowSlider.pressed ? Qt.darker("white", 1.1) : "white"
                                    border.color: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.accent
                                    border.width: 2
                                }
                            }
                        }

                        // RPM High Value Slider for selected flag
                        Column {
                            width: parent.width
                            spacing: App.Spacing.overallSpacing * 0.5

                            RowLayout {
                                width: parent.width

                                Text {
                                    text: "RPM High (End)"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    Layout.fillWidth: true
                                    leftPadding: App.Spacing.overallMargin
                                }

                                Text {
                                    text: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.rpmHigh + " RPM" : ""
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.bold: true
                                    font.family: obdPage.globalFont
                                    color: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.primaryTextColor
                                    rightPadding: App.Spacing.overallMargin
                                }
                            }

                            Slider {
                                id: flagRpmHighSlider
                                width: parent.width - App.Spacing.overallMargin * 2
                                anchors.horizontalCenter: parent.horizontalCenter
                                implicitHeight: App.Spacing.overallSliderHeight * 2.5
                                from: 0
                                to: rpmSettingsPopup.maxRpm
                                stepSize: 100
                                value: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.rpmHigh : rpmSettingsPopup.maxRpm
                                onMoved: {
                                    rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "rpmHigh", value)
                                }

                                background: Rectangle {
                                    x: flagRpmHighSlider.leftPadding
                                    y: flagRpmHighSlider.topPadding + flagRpmHighSlider.availableHeight / 2 - height / 2
                                    width: flagRpmHighSlider.availableWidth
                                    height: App.Spacing.overallSliderHeight * 0.5
                                    radius: height / 2
                                    color: Qt.darker(App.Style.contentColor, 1.2)

                                    Rectangle {
                                        width: flagRpmHighSlider.visualPosition * parent.width
                                        height: parent.height
                                        color: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.accent
                                        radius: height / 2
                                    }
                                }

                                handle: Rectangle {
                                    x: flagRpmHighSlider.leftPadding + flagRpmHighSlider.visualPosition * (flagRpmHighSlider.availableWidth - width)
                                    y: flagRpmHighSlider.topPadding + flagRpmHighSlider.availableHeight / 2 - height / 2
                                    width: App.Spacing.overallSliderHeight * 1.5
                                    height: width
                                    radius: width / 2
                                    color: flagRpmHighSlider.pressed ? Qt.darker("white", 1.1) : "white"
                                    border.color: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.accent
                                    border.width: 2
                                }
                            }
                        }

                        // Color picker for selected flag
                        Column {
                            width: parent.width
                            spacing: App.Spacing.overallSpacing * 0.5

                            Text {
                                text: "Color"
                                font.pixelSize: App.Spacing.overallText * 1.1
                                font.family: obdPage.globalFont
                                color: App.Style.primaryTextColor
                                leftPadding: App.Spacing.overallMargin
                            }

                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter
                                spacing: App.Spacing.overallSpacing

                                Repeater {
                                    model: ["#00FF00", "#FFFF00", "#FF8800", "#FF0000", "#FF00FF", "#00FFFF", "#FFFFFF", "#0088FF"]

                                    Rectangle {
                                        width: App.Spacing.overallText * 3
                                        height: width
                                        radius: width / 2
                                        color: modelData
                                        border.color: flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.color === modelData ? "white" : Qt.darker(modelData, 1.3)
                                        border.width: flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.color === modelData ? 3 : 2

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: {
                                                rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "color", modelData)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Flash toggle for selected flag
                        Rectangle {
                            width: parent.width
                            height: dp(60)
                            radius: App.Spacing.overallMargin * 0.5
                            color: flagFlashRowMouse.containsMouse ? Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3) : "transparent"

                            MouseArea {
                                id: flagFlashRowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    var currentFlash = flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.flash : false
                                    rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "flash", !currentFlash)
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: App.Spacing.overallMargin
                                anchors.rightMargin: App.Spacing.overallMargin
                                spacing: App.Spacing.overallSpacing * 2

                                Text {
                                    text: "Flash when triggered"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    Layout.fillWidth: true

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            var currentFlash = flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.flash
                                            rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "flash", !currentFlash)
                                        }
                                    }
                                }

                                // Toggle switch - matches SettingsMenu styling exactly
                                Item {
                                    id: flagFlashToggleItem
                                    Layout.preferredWidth: dp(80)
                                    Layout.preferredHeight: dp(40)

                                    property bool isChecked: flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.flash

                                    Rectangle {
                                        id: flagFlashTrack
                                        anchors.fill: parent
                                        radius: height / 2
                                        color: flagFlashToggleItem.isChecked ?
                                            Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3) :
                                            Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3)

                                        // Subtle gradient overlay
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: parent.radius
                                            gradient: Gradient {
                                                GradientStop { position: 0.0; color: "transparent" }
                                                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.05) }
                                            }
                                        }

                                        // Animated highlight
                                        Rectangle {
                                            width: flagFlashToggleItem.isChecked ? parent.width : 0
                                            height: parent.height
                                            radius: parent.radius
                                            anchors.right: flagFlashToggleItem.isChecked ? parent.right : undefined
                                            anchors.left: !flagFlashToggleItem.isChecked ? parent.left : undefined
                                            color: App.Style.accent
                                            opacity: flagFlashToggleItem.isChecked ? 0.5 : 0

                                            Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutQuad } }
                                            Behavior on opacity { NumberAnimation { duration: 300 } }
                                        }

                                        // ON/OFF text
                                        Text {
                                            anchors {
                                                left: flagFlashToggleItem.isChecked ? undefined : parent.left
                                                right: flagFlashToggleItem.isChecked ? parent.right : undefined
                                                margins: dp(10)
                                                verticalCenter: parent.verticalCenter
                                            }
                                            text: flagFlashToggleItem.isChecked ? "ON" : "OFF"
                                            font.pixelSize: App.Spacing.overallText * 0.8
                                            font.bold: true
                                            font.family: obdPage.globalFont
                                            color: flagFlashToggleItem.isChecked ? App.Style.accent :
                                                Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.7)
                                            visible: width < (parent.width - flagFlashHandle.width - dp(10))

                                            Behavior on color { ColorAnimation { duration: 200 } }
                                        }
                                    }

                                    // Handle
                                    Rectangle {
                                        id: flagFlashHandle
                                        width: dp(40)
                                        height: dp(40)
                                        radius: width / 2
                                        x: flagFlashToggleItem.isChecked ? parent.width - width : 0
                                        y: 0
                                        color: "white"

                                        // Inner indicator
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: parent.width * 0.4
                                            height: width
                                            radius: width / 2
                                            color: App.Style.accent
                                            opacity: flagFlashToggleItem.isChecked ? 1 : 0
                                            scale: flagFlashToggleItem.isChecked ? 1 : 0.5

                                            Behavior on opacity { NumberAnimation { duration: 200 } }
                                            Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
                                        }

                                        layer.enabled: true
                                        layer.effect: DropShadow {
                                            verticalOffset: 2
                                            radius: 6.0
                                            samples: 17
                                            color: Qt.rgba(0, 0, 0, 0.2)
                                        }

                                        scale: flagFlashToggleMouse.pressed ? 0.95 : 1.0

                                        Behavior on x {
                                            NumberAnimation {
                                                duration: 300
                                                easing.type: Easing.OutBack
                                                easing.overshoot: 0.6
                                            }
                                        }
                                        Behavior on scale { NumberAnimation { duration: 100 } }
                                    }

                                    // Interactive area with pulse animation
                                    MouseArea {
                                        id: flagFlashToggleMouse
                                        anchors.fill: parent
                                        onClicked: {
                                            rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "flash", !flagFlashToggleItem.isChecked)
                                        }
                                        onPressed: {
                                            flagFlashPulseAnimation.start()
                                        }

                                        SequentialAnimation {
                                            id: flagFlashPulseAnimation
                                            PropertyAnimation {
                                                target: flagFlashHandle
                                                property: "scale"
                                                to: 0.9
                                                duration: 100
                                            }
                                            PropertyAnimation {
                                                target: flagFlashHandle
                                                property: "scale"
                                                to: 1.0
                                                duration: 100
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Flash speed slider (only visible when flash is enabled)
                        Column {
                            width: parent.width
                            spacing: App.Spacing.overallSpacing * 0.5
                            visible: flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.flash

                            RowLayout {
                                width: parent.width

                                Text {
                                    text: "Flash Speed"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    Layout.fillWidth: true
                                    leftPadding: App.Spacing.overallMargin
                                }

                                Text {
                                    text: (flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.flashSpeed : 100) + " ms"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.bold: true
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    rightPadding: App.Spacing.overallMargin
                                }
                            }

                            Slider {
                                id: flagFlashSpeedSlider
                                width: parent.width - App.Spacing.overallMargin * 2
                                anchors.horizontalCenter: parent.horizontalCenter
                                implicitHeight: App.Spacing.overallSliderHeight * 2.5
                                from: 50
                                to: 500
                                stepSize: 25
                                value: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.flashSpeed : 100
                                onMoved: {
                                    rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "flashSpeed", value)
                                }

                                background: Rectangle {
                                    x: flagFlashSpeedSlider.leftPadding
                                    y: flagFlashSpeedSlider.topPadding + flagFlashSpeedSlider.availableHeight / 2 - height / 2
                                    width: flagFlashSpeedSlider.availableWidth
                                    height: App.Spacing.overallSliderHeight * 0.5
                                    radius: height / 2
                                    color: Qt.darker(App.Style.contentColor, 1.2)

                                    Rectangle {
                                        width: flagFlashSpeedSlider.visualPosition * parent.width
                                        height: parent.height
                                        color: App.Style.accent
                                        radius: height / 2
                                    }
                                }

                                handle: Rectangle {
                                    x: flagFlashSpeedSlider.leftPadding + flagFlashSpeedSlider.visualPosition * (flagFlashSpeedSlider.availableWidth - width)
                                    y: flagFlashSpeedSlider.topPadding + flagFlashSpeedSlider.availableHeight / 2 - height / 2
                                    width: App.Spacing.overallSliderHeight * 1.5
                                    height: width
                                    radius: width / 2
                                    color: flagFlashSpeedSlider.pressed ? Qt.darker("white", 1.1) : "white"
                                    border.color: App.Style.accent
                                    border.width: 2
                                }
                            }

                            RowLayout {
                                width: parent.width - App.Spacing.overallMargin * 2
                                anchors.horizontalCenter: parent.horizontalCenter

                                Text {
                                    text: "Fast"
                                    font.pixelSize: App.Spacing.overallText * 0.9
                                    font.family: obdPage.globalFont
                                    color: Qt.darker(App.Style.primaryTextColor, 1.3)
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: "Slow"
                                    font.pixelSize: App.Spacing.overallText * 0.9
                                    font.family: obdPage.globalFont
                                    color: Qt.darker(App.Style.primaryTextColor, 1.3)
                                }
                            }
                        }

                        // Full screen overlay toggle for this flag
                        Rectangle {
                            width: parent.width
                            height: dp(60)
                            radius: App.Spacing.overallMargin * 0.5
                            color: fullScreenRowMouse.containsMouse ? Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3) : "transparent"

                            MouseArea {
                                id: fullScreenRowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    var currentFullScreen = flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.fullScreenFlash : false
                                    rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "fullScreenFlash", !currentFullScreen)
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: App.Spacing.overallMargin
                                anchors.rightMargin: App.Spacing.overallMargin
                                spacing: App.Spacing.overallSpacing * 2

                                Text {
                                    text: "Full screen overlay"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    Layout.fillWidth: true

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            var currentFullScreen = flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.fullScreenFlash : false
                                            rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "fullScreenFlash", !currentFullScreen)
                                        }
                                    }
                                }

                                // Toggle switch - matches SettingsMenu styling exactly
                                Item {
                                    id: fullScreenToggleItem
                                    Layout.preferredWidth: dp(80)
                                    Layout.preferredHeight: dp(40)

                                    property bool isChecked: flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.fullScreenFlash

                                    Rectangle {
                                        id: fullScreenTrack
                                        anchors.fill: parent
                                        radius: height / 2
                                        color: fullScreenToggleItem.isChecked ?
                                            Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3) :
                                            Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3)

                                        // Subtle gradient overlay
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: parent.radius
                                            gradient: Gradient {
                                                GradientStop { position: 0.0; color: "transparent" }
                                                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.05) }
                                            }
                                        }

                                        // Animated highlight
                                        Rectangle {
                                            width: fullScreenToggleItem.isChecked ? parent.width : 0
                                            height: parent.height
                                            radius: parent.radius
                                            anchors.right: fullScreenToggleItem.isChecked ? parent.right : undefined
                                            anchors.left: !fullScreenToggleItem.isChecked ? parent.left : undefined
                                            color: App.Style.accent
                                            opacity: fullScreenToggleItem.isChecked ? 0.5 : 0

                                            Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutQuad } }
                                            Behavior on opacity { NumberAnimation { duration: 300 } }
                                        }

                                        // ON/OFF text
                                        Text {
                                            anchors {
                                                left: fullScreenToggleItem.isChecked ? undefined : parent.left
                                                right: fullScreenToggleItem.isChecked ? parent.right : undefined
                                                margins: dp(10)
                                                verticalCenter: parent.verticalCenter
                                            }
                                            text: fullScreenToggleItem.isChecked ? "ON" : "OFF"
                                            font.pixelSize: App.Spacing.overallText * 0.8
                                            font.bold: true
                                            font.family: obdPage.globalFont
                                            color: fullScreenToggleItem.isChecked ? App.Style.accent :
                                                Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.7)
                                            visible: width < (parent.width - fullScreenHandle.width - dp(10))

                                            Behavior on color { ColorAnimation { duration: 200 } }
                                        }
                                    }

                                    // Handle
                                    Rectangle {
                                        id: fullScreenHandle
                                        width: dp(40)
                                        height: dp(40)
                                        radius: width / 2
                                        x: fullScreenToggleItem.isChecked ? parent.width - width : 0
                                        y: 0
                                        color: "white"

                                        // Inner indicator
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: parent.width * 0.4
                                            height: width
                                            radius: width / 2
                                            color: App.Style.accent
                                            opacity: fullScreenToggleItem.isChecked ? 1 : 0
                                            scale: fullScreenToggleItem.isChecked ? 1 : 0.5

                                            Behavior on opacity { NumberAnimation { duration: 200 } }
                                            Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
                                        }

                                        layer.enabled: true
                                        layer.effect: DropShadow {
                                            verticalOffset: 2
                                            radius: 6.0
                                            samples: 17
                                            color: Qt.rgba(0, 0, 0, 0.2)
                                        }

                                        scale: fullScreenFlashFlagToggleMouse.pressed ? 0.95 : 1.0

                                        Behavior on x {
                                            NumberAnimation {
                                                duration: 300
                                                easing.type: Easing.OutBack
                                                easing.overshoot: 0.6
                                            }
                                        }
                                        Behavior on scale { NumberAnimation { duration: 100 } }
                                    }

                                    // Interactive area with pulse animation
                                    MouseArea {
                                        id: fullScreenFlashFlagToggleMouse
                                        anchors.fill: parent
                                        onClicked: {
                                            rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "fullScreenFlash", !fullScreenToggleItem.isChecked)
                                        }
                                        onPressed: {
                                            fullScreenPulseAnimation.start()
                                        }

                                        SequentialAnimation {
                                            id: fullScreenPulseAnimation
                                            PropertyAnimation {
                                                target: fullScreenHandle
                                                property: "scale"
                                                to: 0.9
                                                duration: 100
                                            }
                                            PropertyAnimation {
                                                target: fullScreenHandle
                                                property: "scale"
                                                to: 1.0
                                                duration: 100
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Overlay opacity slider (only visible when full screen overlay is enabled for this flag)
                        Column {
                            width: parent.width
                            spacing: App.Spacing.overallSpacing * 0.5
                            visible: flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.fullScreenFlash

                            RowLayout {
                                width: parent.width

                                Text {
                                    text: "Overlay Opacity"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    Layout.fillWidth: true
                                    leftPadding: App.Spacing.overallMargin
                                }

                                Text {
                                    text: Math.round((flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.fullScreenFlashOpacity !== undefined ? flagSettingsColumn.currentFlag.fullScreenFlashOpacity : 0.5) * 100) + "%"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.bold: true
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    rightPadding: App.Spacing.overallMargin
                                }
                            }

                            Slider {
                                id: flagOpacitySlider
                                width: parent.width - App.Spacing.overallMargin * 2
                                anchors.horizontalCenter: parent.horizontalCenter
                                implicitHeight: App.Spacing.overallSliderHeight * 2.5
                                from: 0.1
                                to: 1.0
                                stepSize: 0.05
                                value: flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.fullScreenFlashOpacity !== undefined ? flagSettingsColumn.currentFlag.fullScreenFlashOpacity : 0.5
                                onMoved: {
                                    rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "fullScreenFlashOpacity", value)
                                }

                                background: Rectangle {
                                    x: flagOpacitySlider.leftPadding
                                    y: flagOpacitySlider.topPadding + flagOpacitySlider.availableHeight / 2 - height / 2
                                    width: flagOpacitySlider.availableWidth
                                    height: App.Spacing.overallSliderHeight * 0.5
                                    radius: height / 2
                                    color: Qt.darker(App.Style.contentColor, 1.2)

                                    // Filled portion shows actual opacity preview
                                    Rectangle {
                                        width: flagOpacitySlider.visualPosition * parent.width
                                        height: parent.height
                                        radius: height / 2
                                        color: {
                                            var flagColor = flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.accent
                                            var opacityVal = flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.fullScreenFlashOpacity !== undefined ? flagSettingsColumn.currentFlag.fullScreenFlashOpacity : 0.5
                                            return Qt.rgba(Qt.lighter(flagColor, 1).r, Qt.lighter(flagColor, 1).g, Qt.lighter(flagColor, 1).b, opacityVal)
                                        }
                                    }
                                }

                                handle: Rectangle {
                                    x: flagOpacitySlider.leftPadding + flagOpacitySlider.visualPosition * (flagOpacitySlider.availableWidth - width)
                                    y: flagOpacitySlider.topPadding + flagOpacitySlider.availableHeight / 2 - height / 2
                                    width: App.Spacing.overallSliderHeight * 1.5
                                    height: width
                                    radius: width / 2
                                    color: flagOpacitySlider.pressed ? Qt.darker("white", 1.1) : "white"
                                    border.color: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.accent
                                    border.width: 2
                                }
                            }
                        }

                        // Aura overlay toggle for this flag (perimeter-only fade)
                        Rectangle {
                            width: parent.width
                            height: dp(60)
                            radius: App.Spacing.overallMargin * 0.5
                            color: auraRowMouse.containsMouse ? Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3) : "transparent"

                            MouseArea {
                                id: auraRowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    var currentAura = flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.auraFlash : false
                                    rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "auraFlash", !currentAura)
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: App.Spacing.overallMargin
                                anchors.rightMargin: App.Spacing.overallMargin
                                spacing: App.Spacing.overallSpacing * 2

                                Text {
                                    text: "Aura overlay"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    Layout.fillWidth: true

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            var currentAura = flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.auraFlash : false
                                            rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "auraFlash", !currentAura)
                                        }
                                    }
                                }

                                Item {
                                    id: auraToggleItem
                                    Layout.preferredWidth: dp(80)
                                    Layout.preferredHeight: dp(40)

                                    property bool isChecked: flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.auraFlash

                                    Rectangle {
                                        id: auraTrack
                                        anchors.fill: parent
                                        radius: height / 2
                                        color: auraToggleItem.isChecked ?
                                            Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3) :
                                            Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3)

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: parent.radius
                                            gradient: Gradient {
                                                GradientStop { position: 0.0; color: "transparent" }
                                                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.05) }
                                            }
                                        }

                                        Rectangle {
                                            width: auraToggleItem.isChecked ? parent.width : 0
                                            height: parent.height
                                            radius: parent.radius
                                            anchors.right: auraToggleItem.isChecked ? parent.right : undefined
                                            anchors.left: !auraToggleItem.isChecked ? parent.left : undefined
                                            color: App.Style.accent
                                            opacity: auraToggleItem.isChecked ? 0.5 : 0

                                            Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutQuad } }
                                            Behavior on opacity { NumberAnimation { duration: 300 } }
                                        }

                                        Text {
                                            anchors {
                                                left: auraToggleItem.isChecked ? undefined : parent.left
                                                right: auraToggleItem.isChecked ? parent.right : undefined
                                                margins: dp(10)
                                                verticalCenter: parent.verticalCenter
                                            }
                                            text: auraToggleItem.isChecked ? "ON" : "OFF"
                                            font.pixelSize: App.Spacing.overallText * 0.8
                                            font.bold: true
                                            font.family: obdPage.globalFont
                                            color: auraToggleItem.isChecked ? App.Style.accent :
                                                Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.7)
                                            visible: width < (parent.width - auraHandle.width - dp(10))

                                            Behavior on color { ColorAnimation { duration: 200 } }
                                        }
                                    }

                                    Rectangle {
                                        id: auraHandle
                                        width: dp(40)
                                        height: dp(40)
                                        radius: width / 2
                                        x: auraToggleItem.isChecked ? parent.width - width : 0
                                        y: 0
                                        color: "white"

                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: parent.width * 0.4
                                            height: width
                                            radius: width / 2
                                            color: App.Style.accent
                                            opacity: auraToggleItem.isChecked ? 1 : 0
                                            scale: auraToggleItem.isChecked ? 1 : 0.5

                                            Behavior on opacity { NumberAnimation { duration: 200 } }
                                            Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
                                        }

                                        layer.enabled: true
                                        layer.effect: DropShadow {
                                            verticalOffset: 2
                                            radius: 6.0
                                            samples: 17
                                            color: Qt.rgba(0, 0, 0, 0.2)
                                        }

                                        scale: auraFlagToggleMouse.pressed ? 0.95 : 1.0

                                        Behavior on x {
                                            NumberAnimation {
                                                duration: 300
                                                easing.type: Easing.OutBack
                                                easing.overshoot: 0.6
                                            }
                                        }
                                        Behavior on scale { NumberAnimation { duration: 100 } }
                                    }

                                    MouseArea {
                                        id: auraFlagToggleMouse
                                        anchors.fill: parent
                                        onClicked: {
                                            rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "auraFlash", !auraToggleItem.isChecked)
                                        }
                                        onPressed: {
                                            auraPulseAnimation.start()
                                        }

                                        SequentialAnimation {
                                            id: auraPulseAnimation
                                            PropertyAnimation {
                                                target: auraHandle
                                                property: "scale"
                                                to: 0.9
                                                duration: 100
                                            }
                                            PropertyAnimation {
                                                target: auraHandle
                                                property: "scale"
                                                to: 1.0
                                                duration: 100
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Aura opacity slider (only visible when aura overlay is enabled for this flag)
                        Column {
                            width: parent.width
                            spacing: App.Spacing.overallSpacing * 0.5
                            visible: flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.auraFlash

                            RowLayout {
                                width: parent.width

                                Text {
                                    text: "Aura Opacity"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    Layout.fillWidth: true
                                    leftPadding: App.Spacing.overallMargin
                                }

                                Text {
                                    text: Math.round((flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.auraFlashOpacity !== undefined ? flagSettingsColumn.currentFlag.auraFlashOpacity : 0.6) * 100) + "%"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.bold: true
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    rightPadding: App.Spacing.overallMargin
                                }
                            }

                            Slider {
                                id: flagAuraOpacitySlider
                                width: parent.width - App.Spacing.overallMargin * 2
                                anchors.horizontalCenter: parent.horizontalCenter
                                implicitHeight: App.Spacing.overallSliderHeight * 2.5
                                from: 0.1
                                to: 1.0
                                stepSize: 0.05
                                value: flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.auraFlashOpacity !== undefined ? flagSettingsColumn.currentFlag.auraFlashOpacity : 0.6
                                onMoved: {
                                    rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "auraFlashOpacity", value)
                                }

                                background: Rectangle {
                                    x: flagAuraOpacitySlider.leftPadding
                                    y: flagAuraOpacitySlider.topPadding + flagAuraOpacitySlider.availableHeight / 2 - height / 2
                                    width: flagAuraOpacitySlider.availableWidth
                                    height: App.Spacing.overallSliderHeight * 0.5
                                    radius: height / 2
                                    color: Qt.darker(App.Style.contentColor, 1.2)

                                    Rectangle {
                                        width: flagAuraOpacitySlider.visualPosition * parent.width
                                        height: parent.height
                                        radius: height / 2
                                        color: {
                                            var flagColor = flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.accent
                                            var opacityVal = flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.auraFlashOpacity !== undefined ? flagSettingsColumn.currentFlag.auraFlashOpacity : 0.6
                                            return Qt.rgba(Qt.lighter(flagColor, 1).r, Qt.lighter(flagColor, 1).g, Qt.lighter(flagColor, 1).b, opacityVal)
                                        }
                                    }
                                }

                                handle: Rectangle {
                                    x: flagAuraOpacitySlider.leftPadding + flagAuraOpacitySlider.visualPosition * (flagAuraOpacitySlider.availableWidth - width)
                                    y: flagAuraOpacitySlider.topPadding + flagAuraOpacitySlider.availableHeight / 2 - height / 2
                                    width: App.Spacing.overallSliderHeight * 1.5
                                    height: width
                                    radius: width / 2
                                    color: flagAuraOpacitySlider.pressed ? Qt.darker("white", 1.1) : "white"
                                    border.color: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.accent
                                    border.width: 2
                                }
                            }
                        }

                        // Aura spread slider (only visible when aura overlay is enabled for this flag)
                        Column {
                            width: parent.width
                            spacing: App.Spacing.overallSpacing * 0.5
                            visible: flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.auraFlash

                            RowLayout {
                                width: parent.width

                                Text {
                                    text: "Aura Spread"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    Layout.fillWidth: true
                                    leftPadding: App.Spacing.overallMargin
                                }

                                Text {
                                    text: Math.round((flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.auraSpread !== undefined ? flagSettingsColumn.currentFlag.auraSpread : 0.18) * 100) + "%"
                                    font.pixelSize: App.Spacing.overallText * 1.1
                                    font.bold: true
                                    font.family: obdPage.globalFont
                                    color: App.Style.primaryTextColor
                                    rightPadding: App.Spacing.overallMargin
                                }
                            }

                            Slider {
                                id: flagAuraSpreadSlider
                                width: parent.width - App.Spacing.overallMargin * 2
                                anchors.horizontalCenter: parent.horizontalCenter
                                implicitHeight: App.Spacing.overallSliderHeight * 2.5
                                from: 0.05
                                to: 0.5
                                stepSize: 0.01
                                value: flagSettingsColumn.currentFlag && flagSettingsColumn.currentFlag.auraSpread !== undefined ? flagSettingsColumn.currentFlag.auraSpread : 0.18
                                onMoved: {
                                    rpmSettingsPopup.updateFlag(rpmSettingsPopup.selectedFlagIndex, "auraSpread", value)
                                }

                                background: Rectangle {
                                    x: flagAuraSpreadSlider.leftPadding
                                    y: flagAuraSpreadSlider.topPadding + flagAuraSpreadSlider.availableHeight / 2 - height / 2
                                    width: flagAuraSpreadSlider.availableWidth
                                    height: App.Spacing.overallSliderHeight * 0.5
                                    radius: height / 2
                                    color: Qt.darker(App.Style.contentColor, 1.2)

                                    Rectangle {
                                        width: flagAuraSpreadSlider.visualPosition * parent.width
                                        height: parent.height
                                        radius: height / 2
                                        color: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.accent
                                    }
                                }

                                handle: Rectangle {
                                    x: flagAuraSpreadSlider.leftPadding + flagAuraSpreadSlider.visualPosition * (flagAuraSpreadSlider.availableWidth - width)
                                    y: flagAuraSpreadSlider.topPadding + flagAuraSpreadSlider.availableHeight / 2 - height / 2
                                    width: App.Spacing.overallSliderHeight * 1.5
                                    height: width
                                    radius: width / 2
                                    color: flagAuraSpreadSlider.pressed ? Qt.darker("white", 1.1) : "white"
                                    border.color: flagSettingsColumn.currentFlag ? flagSettingsColumn.currentFlag.color : App.Style.accent
                                    border.width: 2
                                }
                            }
                        }
                    }

                    // Empty state message
                    Text {
                        width: parent.width
                        text: "No flags added yet. Click \"Add Flag\" to create your first shift light trigger point."
                        font.pixelSize: App.Spacing.overallText * 1.1
                        font.family: obdPage.globalFont
                        color: Qt.darker(App.Style.primaryTextColor, 1.3)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: rpmSettingsPopup.flags.length === 0
                        topPadding: App.Spacing.overallSpacing
                        bottomPadding: App.Spacing.overallSpacing
                    }

                    // Bottom padding
                    Item {
                        width: parent.width
                        height: App.Spacing.overallSpacing
                    }
                }
            }
        }
    }
}