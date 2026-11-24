import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Shapes 1.15
import "." as App
import "dashboards" as Dashboards
import "obd" as Obd
import OCTAVE.OBD 1.0

Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: obdPage
    objectName: "obdMenu"

    // Full IMU rate while the page is on screen (a dashboard previewed from
    // a browser can carry compass and G-force gauges)
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

    // The live-data page: one card for every reading the connected vehicle
    // supports. Dashboards are not shown or chosen here (Home shows one,
    // picked in Settings > Display); the one exception is a dashboard being
    // edited in a browser, which previews here (`previewing`).

    // Until obdManager reports a connection the page shows the connect
    // prompt (parameterCardsEmpty) in place of the cards.
    readonly property bool obdConnected: typeof obdManager !== "undefined" && obdManager
                                         && obdManager.connected === true

    // A dashboard being edited on another device (dashboardManager.preview):
    // shown here in place of the active one, with or without a car, until
    // that device ends it (or 60 s after its last change)
    readonly property var _preview: (typeof dashboardManager !== "undefined" && dashboardManager)
                                    ? dashboardManager.preview : ({})
    readonly property bool previewing: !!(_preview && _preview.spec)

    // OBD parameters from centralized singleton
    property var allParameters: App.OBDParameterModel.allParameters

    // Cards read live values only while the parameter grid is actually on
    // screen. This page stays alive hidden (StackView cache, pre-built at
    // startup), and with the IMU streaming, cards that kept listening held
    // the render loop near 50 fps and cost ~10% of a Pi core while nothing of
    // it was visible. Hidden, the cards settle once to "--"; on return they
    // pick up the live values. On screen each card listens to its own value
    // (OBDParameterModel.liveValue -> paramStore[id]), not to every value.
    readonly property bool _gridLive: obdConnected && !previewing
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
    // active StackView page: every PID the vehicle supports, or the previewed
    // dashboard's while there is one. Leaving the page withdraws both demands.
    readonly property bool _pageActiveForDemand:
        StackView.status === StackView.Active || StackView.status === StackView.Activating
    readonly property var _dashboardDemand:
        (_pageActiveForDemand && obdConnected && previewing) ? dashboardRenderer.paramIds : []
    readonly property var _cardsDemand: {
        if (!_pageActiveForDemand || !obdConnected || previewing) return [];
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
    }

    Rectangle {
        anchors.fill: parent
        color: backgroundColor

        // ── A dashboard previewed from a browser editor ───────────────────
        Dashboards.DashboardRenderer {
            id: dashboardRenderer
            objectName: "activeDashboardRenderer"
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                leftMargin: dp(10)
                rightMargin: dp(10)
                topMargin: dp(6)
                bottomMargin: dp(10)
            }
            visible: obdPage.previewing
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
                  ? obdPage._preview.spec : null
        }

        Flickable {
            id: cardsScroll
            objectName: "parameterCardsScroll"
            visible: !obdPage.previewing && obdPage.obdConnected
            anchors {
                top: parent.top
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
                     || obdPage.vehicleParameters.length === 0)
            anchors.centerIn: cardsScroll
            width: Math.min(cardsScroll.width - dp(40), dp(560))
            spacing: dp(10)

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
    }

    // RPM shift light editor (the RPM card opens it)
    Obd.ShiftLightEditor {
        id: rpmSettingsPopup
        fontFamily: obdPage.globalFont
    }
}
