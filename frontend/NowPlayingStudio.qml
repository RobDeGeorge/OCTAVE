import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App
import "settings"

// Window-level immersive editor for Now Playing visuals.
// PiP locks top-right; left panel runs full height; BACKGROUND lives in
// the strip directly below the PiP. No nav bar, no settings tabs.
//
// ── VISUAL STANDARD ──────────────────────────────────────────────────────
// Every setting is a full-width row. Labels stack at the LEFT edge of the
// section, controls stack at the RIGHT (toggles) or fill the row width
// (sliders, chips). Spacing is named — never inline magic numbers — so
// adding a 30th setting adopts the same rhythm as the first.
//
//   ┌── SECTION HEADER ────────────────────────────────────────────────┐
//   │                                                                  │
//   │ Master Toggle Label                                  [On]        │ ← ToggleRow
//   │   │  Dependent Label                       value                 │
//   │   │  ━━━━━━━━━━━━━━━━━●━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ │ ← SliderRow (gated)
//   │                                                                  │
//   │ Standalone Toggle                                    [Off]       │ ← ToggleRow
//   │ Standalone Slider Label                              value       │
//   │ ━━━━━━━━━━━━━━━━━━━━━━━━━●━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━── │ ← SliderRow
//   │ Chip Label                                                       │
//   │ [chip] [chip] [chip] [chip]                                      │ ← ChipsRow
//   └──────────────────────────────────────────────────────────────────┘
//
// ── Spacing tokens (use these exclusively) ──
//   studio.rowSpacing         — siblings inside a section
//   studio.sectionSpacing     — between sections (with thin divider)
//   studio.subClusterSpacing  — master toggle → its dependents
//   studio.headerSpacing      — section header → first control
//   studio.subClusterIndent   — leftMargin on indented dependents
//
// ── Recipes (copy-paste then rename) ──
// Toggle row:
//   NowPlayingMiniToggle {
//       label: "My Toggle"
//       checked: settingsManager ? settingsManager.myProp : false
//       onToggled: function(c) { if (settingsManager) settingsManager.save_my_prop(c) }
//       Connections {
//           target: settingsManager
//           function onMyPropChanged() { checked = settingsManager.myProp }
//       }
//   }
// Slider row:        mirror radiusSlider's ColumnLayout-with-RowLayout-header pattern.
// Chips row:         mirror trackTransitionCtl (ColumnLayout: SettingLabel → SettingsChips).
// Sub-cluster (master toggle + gated dependent): mirror the Rounded ↳ Radius block —
//   ColumnLayout(spacing: studio.subClusterSpacing) with the toggle on top and an
//   indented RowLayout below holding the accent bar (2dp) + the dependent.
Item {
    id: studio
    function dp(s) { return Math.round(s * (App.Spacing.effectiveScale || 1.0)) }

    property var mainWindow: null
    signal closeRequested()

    // ── Hero-morph open animation ─────────────────────────────────────
    // Mirrors SettingsCardPopup: 0 = collapsed onto the originating tile,
    // 1 = filling the window. Geometry interpolates between the two; the
    // dashboard's content fades in during the second half so the user
    // sees the card grow first and the controls resolve once there's room.
    property real openProgress: 0.0
    property rect originRect: Qt.rect(0, 0, 0, 0)
    readonly property real contentOpacity: Math.max(0.0, (openProgress - 0.45) / 0.55)

    // Open-state target rect (where the studio sits at openProgress = 1).
    // Defaults to filling the parent; Main.qml overrides this to leave room
    // for the BottomBar so the nav stays visible while the studio is open.
    property real targetX: 0
    property real targetY: 0
    property real targetWidth:  parent ? parent.width  : 0
    property real targetHeight: parent ? parent.height : 0

    // Geometry: interpolates between originRect (collapsed) and the target
    // rect (fully open). No anchors here — Main.qml omits anchors.fill so
    // these bindings drive the layout end-to-end.
    x: originRect.x + (targetX - originRect.x) * openProgress
    y: originRect.y + (targetY - originRect.y) * openProgress
    width:  originRect.width  + (targetWidth  - originRect.width)  * openProgress
    height: originRect.height + (targetHeight - originRect.height) * openProgress

    // Don't animate on the very first frame so the studio doesn't appear
    // to "open" itself at launch.
    property bool _animEnabled: false
    Component.onCompleted: Qt.callLater(function() { _animEnabled = true })

    Behavior on openProgress {
        enabled: studio._animEnabled
        NumberAnimation { duration: 320; easing.type: Easing.OutCubic }
    }

    readonly property int gap: dp(10)
    // Vertical room reserved at the top of the left panel for the floating
    // back chip (the chip overlaps this space; content starts below it).
    readonly property int backChipReserve: dp(72)

    // ── Visual standard tokens (used everywhere in the studio) ──
    // Adopt these for every new setting so spacing stays consistent and
    // scanning the panel always lands the eye in the same x-positions.
    // One uniform gap (dp(8)) between every element in a section — the same
    // margin we use between a master toggle and its sub-menu — so the whole
    // panel reads with a single consistent vertical rhythm. Section-to-
    // section spacing stays larger (with a divider) for clear breaks.
    readonly property int rowSpacing:        dp(8)   // sibling controls inside a section
    readonly property int sectionSpacing:    dp(20)  // between sections (with thin divider)
    readonly property int subClusterSpacing: dp(8)   // master toggle → its dependents
    readonly property int headerSpacing:     dp(8)   // section header → first control
    readonly property int subClusterIndent:  dp(8)   // left margin on indented dependents
    readonly property int subClusterBarWidth: 2      // accent-bar width on dependents

    // L-shape geometry. PiP locks into the top-right corner at window aspect;
    // the left panel runs the full height; the bottom panel fills under the
    // PiP, between the left panel and the right edge.
    readonly property real windowAspect: (mainWindow && mainWindow.height > 0)
        ? (mainWindow.width / mainWindow.height) : (1280.0 / 720.0)

    // Aspect-correct PiP cap — 50% of the dashboard height, never under 180dp.
    // The left panel takes whatever horizontal slack the PiP doesn't claim, so
    // shrinking the PiP here directly grows the left scroll area.
    readonly property real pipMaxHeight: Math.max(dp(180), dashboard.height * 0.50)
    readonly property real pipNaturalWidth: pipMaxHeight * windowAspect

    // Left panel takes whatever horizontal slack the PiP doesn't claim, so it
    // always meets the right column flush — no gap between cards. Floor at
    // 300dp for legibility on narrow windows; no upper cap, since the PiP's
    // aspect-bounded width already prevents the panel from over-eating space
    // on wide monitors (it just stops growing once the PiP hits its natural
    // width).
    readonly property int leftPanelWidth: Math.max(dp(300),
        Math.round(dashboard.width - pipNaturalWidth))

    // PiP fills whatever the left panel didn't take, capped at its natural
    // (height-bound) width so it never blows past the aspect-correct rect.
    readonly property real pipFitWidth: Math.min(
        Math.max(1, dashboard.width - leftPanelWidth), pipNaturalWidth)
    readonly property real pipFitHeight: pipFitWidth / windowAspect

    readonly property color panelBg: Qt.rgba(App.Style.primaryTextColor.r,
                                             App.Style.primaryTextColor.g,
                                             App.Style.primaryTextColor.b, 0.05)
    readonly property color panelBorder: Qt.rgba(App.Style.accent.r,
                                                  App.Style.accent.g,
                                                  App.Style.accent.b, 0.25)
    readonly property color groupDivider: Qt.rgba(App.Style.primaryTextColor.r,
                                                  App.Style.primaryTextColor.g,
                                                  App.Style.primaryTextColor.b, 0.12)

    readonly property var presetDefs: [
        { label: "Minimal", settings: {
            roundedAlbumArt: true, showAlbumArtShadow: false, vinylRecordMode: false,
            showWaveformVisualizer: false, show3DButtonTilt: false, show3DAlbumPreview: false,
            backgroundBlurRadius: 60, backgroundOverlayOpacity: 80,
            albumArtCornerRadius: 16, albumArtTransition: "Crossfade", backgroundGrid: "Normal"
        }},
        { label: "Vinyl", settings: {
            roundedAlbumArt: false, showAlbumArtShadow: true, vinylRecordMode: true,
            showWaveformVisualizer: true, show3DButtonTilt: false, show3DAlbumPreview: false,
            backgroundBlurRadius: 30, backgroundOverlayOpacity: 60,
            albumArtTransition: "Vinyl Lift", backgroundGrid: "Normal"
        }},
        { label: "Showcase 3D", settings: {
            roundedAlbumArt: true, showAlbumArtShadow: true, vinylRecordMode: false,
            showWaveformVisualizer: false, show3DButtonTilt: true, show3DAlbumPreview: true,
            backgroundBlurRadius: 50, backgroundOverlayOpacity: 70,
            albumArtCornerRadius: 16, albumArtTransition: "Coverflow",
            backgroundGrid: "4x4",
            sideCardOpacity: 0.4, sideCardAngle: 30, buttonTiltDuration: 200
        }},
        { label: "Cinema", settings: {
            roundedAlbumArt: true, showAlbumArtShadow: true, vinylRecordMode: false,
            showWaveformVisualizer: true, show3DButtonTilt: false, show3DAlbumPreview: false,
            backgroundBlurRadius: 80, backgroundOverlayOpacity: 90,
            albumArtCornerRadius: 24, albumArtTransition: "Dissolve", backgroundGrid: "4x4"
        }}
    ]

    function applyPreset(preset) {
        if (!settingsManager || !preset || !preset.settings) return
        var s = preset.settings
        if ("roundedAlbumArt" in s)         settingsManager.save_rounded_album_art(s.roundedAlbumArt)
        if ("showAlbumArtShadow" in s)      settingsManager.save_show_album_art_shadow(s.showAlbumArtShadow)
        if ("vinylRecordMode" in s)         settingsManager.save_vinyl_record_mode(s.vinylRecordMode)
        if ("showWaveformVisualizer" in s)  settingsManager.save_show_waveform_visualizer(s.showWaveformVisualizer)
        if ("show3DButtonTilt" in s)        settingsManager.save_show_3d_button_tilt(s.show3DButtonTilt)
        if ("show3DAlbumPreview" in s)      settingsManager.save_show_3d_album_preview(s.show3DAlbumPreview)
        if ("backgroundBlurRadius" in s)    settingsManager.save_background_blur_radius(s.backgroundBlurRadius)
        if ("backgroundOverlayOpacity" in s) settingsManager.save_background_overlay_opacity(s.backgroundOverlayOpacity)
        if ("albumArtCornerRadius" in s)    settingsManager.save_album_art_corner_radius(s.albumArtCornerRadius)
        if ("albumArtTransition" in s)      settingsManager.save_album_art_transition(s.albumArtTransition)
        if ("backgroundGrid" in s)          settingsManager.save_background_grid(s.backgroundGrid)
        if ("sideCardOpacity" in s)         settingsManager.save_side_card_opacity(s.sideCardOpacity)
        if ("sideCardAngle" in s)           settingsManager.save_side_card_angle(s.sideCardAngle)
        if ("buttonTiltDuration" in s)      settingsManager.save_button_tilt_duration(s.buttonTiltDuration)
    }

    // ── Backdrop ──
    Rectangle {
        anchors.fill: parent
        color: App.Style.backgroundColor
    }

    // ── Dashboard region — fills the entire window so the PiP can hug the
    //    literal top-right corner and the left panel runs floor-to-ceiling.
    Item {
        id: dashboard
        anchors.fill: parent
        // Tiny bottom inset so leftPanel + pipBgStrip don't kiss the
        // BottomBar (or the window edge if the nav is hidden).
        anchors.bottomMargin: studio.dp(6)
        // Fade dashboard content in during the second half of the open
        // animation so the card grows first, then the controls resolve.
        opacity: studio.contentOpacity
        clip: true

        // ─────────────────────────────────────────────────────────────────
        // L-shape: PiP locks top-right, left panel runs full height,
        // bottom panel fills under the PiP between leftPanel.right and the
        // right edge.
        // ─────────────────────────────────────────────────────────────────

        // ── HEADER BOX — back / "Now Playing" header ──
        // Mirrors SettingsCardPopup's header bar exactly (the "→ Now Playing"
        // header users see when entering any other settings sub-page), so
        // the back-nav for this studio looks identical to the rest of the
        // app: arrow + bold title + accent gradient underline, sized to
        // App.Spacing.settingsButtonHeight.
        Rectangle {
            id: headerBox
            anchors.top: parent.top
            anchors.left: parent.left
            width: studio.leftPanelWidth
            height: App.Spacing.settingsButtonHeight
            color: studio.panelBg
            radius: studio.dp(8)
            border.width: 1
            border.color: studio.panelBorder

            Row {
                id: headerRow
                anchors.left: parent.left
                anchors.leftMargin: App.Spacing.settingsContentMargin
                anchors.verticalCenter: parent.verticalCenter
                spacing: App.Spacing.overallSpacing * 0.5

                Text {
                    text: "→"
                    color: headerArea.containsMouse ? App.Style.accent : App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.overallText * 1.6
                    font.family: App.Style.fontFamily
                    anchors.verticalCenter: parent.verticalCenter
                    Behavior on color { ColorAnimation { duration: 150 } }
                }
                Text {
                    text: "Now Playing"
                    color: headerArea.containsMouse ? App.Style.accent : App.Style.primaryTextColor
                    font.pixelSize: App.Spacing.overallText * 1.6
                    font.bold: true
                    font.family: App.Style.fontFamily
                    font.letterSpacing: App.EnvironmentTheme.active.labelLetterSpacing
                    font.capitalization: App.EnvironmentTheme.active.labelUppercase ? Font.AllUppercase : Font.MixedCase
                    anchors.verticalCenter: parent.verticalCenter
                    Behavior on color { ColorAnimation { duration: 150 } }
                }
            }

            // Bottom accent gradient line — same as SettingsCardPopup header.
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: headerBox.radius
                anchors.rightMargin: headerBox.radius
                height: 1
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: App.Style.accent }
                    GradientStop { position: 0.6; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }

            MouseArea {
                id: headerArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: studio.closeRequested()
            }
        }

        // ── LEFT PANEL — vertically scrollable column of all controls ──
        // Boxed with a subtle background + border so it reads as a card
        // matching the BACKGROUND strip below the PiP and the PiP frame.
        Rectangle {
            id: leftPanel
            anchors.top: headerBox.bottom
            anchors.topMargin: studio.gap
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            width: studio.leftPanelWidth
            // Transparent fill — each individual setting card carries its
            // own subtle background, so the panel itself just frames them.
            color: "transparent"
            radius: studio.dp(8)
            border.width: 1
            border.color: studio.panelBorder

            Flickable {
                id: leftFlick
                anchors.fill: parent
                anchors.margins: studio.dp(20)
                clip: true
                contentWidth: width
                contentHeight: leftCol.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.DragAndOvershootBounds
                // ScrollBar lives in the right margin of leftPanel — between
                // the content area (leftFlick.right) and the card border —
                // so it never draws over toggle text or slider tracks.
                ScrollBar.vertical: ScrollBar {
                    id: leftPanelScrollBar
                    parent: leftPanel
                    policy: ScrollBar.AsNeeded
                    // Geometry, not anchors: the attached ScrollBar is
                    // created under the Flickable and only later reparented
                    // to leftPanel, and anchoring to a non-sibling at that
                    // first evaluation warns at startup.
                    x: leftPanel.width - width - studio.dp(6)
                    y: leftFlick.y
                    height: leftFlick.height
                }

                ColumnLayout {
                    id: leftCol
                    width: leftFlick.width
                    spacing: studio.sectionSpacing

                    // ─────────────────────────────────────────────────────────
                    // Each individual setting is its own SettingCategory or
                    // SettingSubcategory card — same chrome the Display
                    // settings page uses, so every sub-setting is clearly
                    // delineated. Section labels (BACKGROUND, ALBUM ART, …)
                    // group cards visually but don't add chrome themselves.
                    // ─────────────────────────────────────────────────────────

                    // ── BACKGROUND ────────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: studio.rowSpacing
                        Text {
                            text: "BACKGROUND"
                            color: App.Style.secondaryTextColor
                            font.pixelSize: App.Spacing.overallText - 3
                            font.bold: true
                            font.letterSpacing: 1.2
                            font.family: App.Style.fontFamily
                        }

                        SettingCategory {
                            title: "Layout"
                            NowPlayingButtonGrid {
                                id: bgGridCtl
                                options: ["Normal", "2x2", "4x4"]
                                currentValue: settingsManager ? settingsManager.backgroundGrid : "4x4"
                                onSelected: function(v) { if (settingsManager) settingsManager.save_background_grid(v) }
                                Connections {
                                    target: settingsManager
                                    function onBackgroundGridChanged() { bgGridCtl.currentValue = settingsManager.backgroundGrid }
                                }
                            }
                        }

                        SettingCategory {
                            title: "Blur"
                            inlineContent: ValueDisplay { text: blurSlider.value.toFixed(0) }
                            SettingsSlider {
                                id: blurSlider
                                Layout.fillWidth: true
                                from: 0; to: 100; stepSize: 1
                                value: settingsManager ? settingsManager.backgroundBlurRadius : 40
                                activeColor: App.Style.accent
                                onMoved: { if (settingsManager) settingsManager.save_background_blur_radius(blurSlider.value) }
                                Connections {
                                    target: settingsManager
                                    function onBackgroundBlurRadiusChanged() { blurSlider.value = settingsManager.backgroundBlurRadius }
                                }
                            }
                        }

                        SettingSubcategory {
                            title: "Dark Overlay"
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: studio.dp(4)
                                RowLayout {
                                    Layout.fillWidth: true
                                    SettingLabel { text: "Darkness"; Layout.fillWidth: true }
                                    ValueDisplay { text: darkSlider.value.toFixed(0) + "%" }
                                }
                                SettingsSlider {
                                    id: darkSlider
                                    Layout.fillWidth: true
                                    from: 0; to: 100; stepSize: 1
                                    value: settingsManager ? settingsManager.backgroundOverlayOpacity : 80
                                    activeColor: App.Style.accent
                                    onMoved: { if (settingsManager) settingsManager.save_background_overlay_opacity(darkSlider.value) }
                                    Connections {
                                        target: settingsManager
                                        function onBackgroundOverlayOpacityChanged() { darkSlider.value = settingsManager.backgroundOverlayOpacity }
                                    }
                                }
                            }
                        }
                    }

                    // ── ALBUM ART ─────────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: studio.rowSpacing
                        Text {
                            text: "ALBUM ART"
                            color: App.Style.secondaryTextColor
                            font.pixelSize: App.Spacing.overallText - 3
                            font.bold: true
                            font.letterSpacing: 1.2
                            font.family: App.Style.fontFamily
                        }

                        SettingSubcategory {
                            id: roundedSub
                            title: "Rounded"
                            togglable: true
                            checked: settingsManager ? settingsManager.roundedAlbumArt : true
                            onToggled: function(c) { if (settingsManager) settingsManager.save_rounded_album_art(c) }
                            Connections {
                                target: settingsManager
                                function onRoundedAlbumArtChanged() { roundedSub.checked = settingsManager.roundedAlbumArt }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: studio.dp(4)
                                RowLayout {
                                    Layout.fillWidth: true
                                    SettingLabel { text: "Radius"; Layout.fillWidth: true }
                                    ValueDisplay { text: radiusSlider.value.toFixed(0) + " dp" }
                                }
                                SettingsSlider {
                                    id: radiusSlider
                                    Layout.fillWidth: true
                                    from: 2; to: 32; stepSize: 1
                                    value: settingsManager ? settingsManager.albumArtCornerRadius : 16
                                    activeColor: App.Style.accent
                                    onMoved: { if (settingsManager) settingsManager.save_album_art_corner_radius(radiusSlider.value) }
                                    Connections {
                                        target: settingsManager
                                        function onAlbumArtCornerRadiusChanged() { radiusSlider.value = settingsManager.albumArtCornerRadius }
                                    }
                                }
                            }
                        }

                        SettingCategory {
                            title: "Shadow"
                            inlineContent: SettingsToggle {
                                id: shadowToggle
                                compact: true
                                Layout.preferredWidth: studio.dp(104)
                                Layout.minimumWidth: studio.dp(104)
                                text: checked ? "On" : "Off"
                                checked: settingsManager ? settingsManager.showAlbumArtShadow : true
                                activeColor: App.Style.accent
                                inactiveColor: App.Style.hoverColor
                                onToggled: function(c) { if (settingsManager) settingsManager.save_show_album_art_shadow(c) }
                                Connections {
                                    target: settingsManager
                                    function onShowAlbumArtShadowChanged() { shadowToggle.checked = settingsManager.showAlbumArtShadow }
                                }
                            }
                        }

                        SettingCategory {
                            title: "Vinyl"
                            inlineContent: SettingsToggle {
                                id: vinylToggle
                                compact: true
                                Layout.preferredWidth: studio.dp(104)
                                Layout.minimumWidth: studio.dp(104)
                                text: checked ? "On" : "Off"
                                checked: settingsManager ? settingsManager.vinylRecordMode : false
                                activeColor: App.Style.accent
                                inactiveColor: App.Style.hoverColor
                                onToggled: function(c) { if (settingsManager) settingsManager.save_vinyl_record_mode(c) }
                                Connections {
                                    target: settingsManager
                                    function onVinylRecordModeChanged() { vinylToggle.checked = settingsManager.vinylRecordMode }
                                }
                            }
                        }

                        SettingCategory {
                            title: "Waveform"
                            inlineContent: SettingsToggle {
                                id: waveformToggle
                                compact: true
                                Layout.preferredWidth: studio.dp(104)
                                Layout.minimumWidth: studio.dp(104)
                                text: checked ? "On" : "Off"
                                checked: settingsManager ? settingsManager.showWaveformVisualizer : true
                                activeColor: App.Style.accent
                                inactiveColor: App.Style.hoverColor
                                onToggled: function(c) { if (settingsManager) settingsManager.save_show_waveform_visualizer(c) }
                                Connections {
                                    target: settingsManager
                                    function onShowWaveformVisualizerChanged() { waveformToggle.checked = settingsManager.showWaveformVisualizer }
                                }
                            }
                        }

                    }

                    // ── 3D PREVIEW ────────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: studio.rowSpacing
                        Text {
                            text: "3D PREVIEW"
                            color: App.Style.secondaryTextColor
                            font.pixelSize: App.Spacing.overallText - 3
                            font.bold: true
                            font.letterSpacing: 1.2
                            font.family: App.Style.fontFamily
                        }

                        SettingSubcategory {
                            id: enable3dSub
                            title: "Enable"
                            togglable: true
                            checked: settingsManager ? settingsManager.show3DAlbumPreview : false
                            onToggled: function(c) { if (settingsManager) settingsManager.save_show_3d_album_preview(c) }
                            Connections {
                                target: settingsManager
                                function onShow3DAlbumPreviewChanged() { enable3dSub.checked = settingsManager.show3DAlbumPreview }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: studio.dp(10)
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: studio.dp(4)
                                    RowLayout {
                                        Layout.fillWidth: true
                                        SettingLabel { text: "Side Card Angle"; Layout.fillWidth: true }
                                        ValueDisplay { text: angleSlider.value.toFixed(0) + "°" }
                                    }
                                    SettingsSlider {
                                        id: angleSlider
                                        Layout.fillWidth: true
                                        from: 5; to: 60; stepSize: 1
                                        value: settingsManager ? settingsManager.sideCardAngle : 30
                                        activeColor: App.Style.accent
                                        onMoved: { if (settingsManager) settingsManager.save_side_card_angle(angleSlider.value) }
                                        Connections {
                                            target: settingsManager
                                            function onSideCardAngleChanged() { angleSlider.value = settingsManager.sideCardAngle }
                                        }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: studio.dp(4)
                                    RowLayout {
                                        Layout.fillWidth: true
                                        SettingLabel { text: "Side Card Opacity"; Layout.fillWidth: true }
                                        ValueDisplay { text: Math.round(sideOpSlider.value * 100) + "%" }
                                    }
                                    SettingsSlider {
                                        id: sideOpSlider
                                        Layout.fillWidth: true
                                        from: 0.1; to: 1.0; stepSize: 0.05
                                        value: settingsManager ? settingsManager.sideCardOpacity : 0.4
                                        activeColor: App.Style.accent
                                        onMoved: { if (settingsManager) settingsManager.save_side_card_opacity(sideOpSlider.value) }
                                        Connections {
                                            target: settingsManager
                                            function onSideCardOpacityChanged() { sideOpSlider.value = settingsManager.sideCardOpacity }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── ANIMATION & TEXT ─────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: studio.rowSpacing
                        Text {
                            text: "ANIMATION & TEXT"
                            color: App.Style.secondaryTextColor
                            font.pixelSize: App.Spacing.overallText - 3
                            font.bold: true
                            font.letterSpacing: 1.2
                            font.family: App.Style.fontFamily
                        }

                        SettingCategory {
                            title: "Text Scroll Speed"
                            inlineContent: ValueDisplay { text: (textScrollSpeedSlider.value / 1000).toFixed(1) + " s" }
                            SettingsSlider {
                                id: textScrollSpeedSlider
                                Layout.fillWidth: true
                                from: 1000; to: 10000; stepSize: 500
                                value: settingsManager ? settingsManager.textScrollSpeed : 5000
                                activeColor: App.Style.accent
                                onMoved: { if (settingsManager) settingsManager.save_text_scroll_speed(textScrollSpeedSlider.value) }
                                Connections {
                                    target: settingsManager
                                    function onTextScrollSpeedChanged() { textScrollSpeedSlider.value = settingsManager.textScrollSpeed }
                                }
                            }
                        }

                        SettingSubcategory {
                            id: tiltSub
                            title: "Button Tilt"
                            togglable: true
                            checked: settingsManager ? settingsManager.show3DButtonTilt : false
                            onToggled: function(c) { if (settingsManager) settingsManager.save_show_3d_button_tilt(c) }
                            Connections {
                                target: settingsManager
                                function onShow3DButtonTiltChanged() { tiltSub.checked = settingsManager.show3DButtonTilt }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: studio.dp(4)
                                RowLayout {
                                    Layout.fillWidth: true
                                    SettingLabel { text: "Tilt Duration"; Layout.fillWidth: true }
                                    ValueDisplay { text: tiltDurationSlider.value.toFixed(0) + " ms" }
                                }
                                SettingsSlider {
                                    id: tiltDurationSlider
                                    Layout.fillWidth: true
                                    from: 50; to: 500; stepSize: 10
                                    value: settingsManager ? settingsManager.buttonTiltDuration : 200
                                    activeColor: App.Style.accent
                                    onMoved: { if (settingsManager) settingsManager.save_button_tilt_duration(tiltDurationSlider.value) }
                                    Connections {
                                        target: settingsManager
                                        function onButtonTiltDurationChanged() { tiltDurationSlider.value = settingsManager.buttonTiltDuration }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── PiP locked literally at the top-right corner ──
        MediaRoomLivePreview {
            id: centerPip
            anchors.top: parent.top
            anchors.right: parent.right
            width: studio.pipFitWidth
            height: studio.pipFitHeight
            designWidth: studio.mainWindow ? studio.mainWindow.width : 1280
            designHeight: studio.mainWindow ? studio.mainWindow.height : 720
        }

        // ── BACKGROUND strip directly under the PiP ───────────────────
        // The blurred art + tint that lives behind everything in MediaRoom
        // sits beneath the PiP so the user sees its result framing the
        // preview directly. Section header + Layout chips + Blur slider +
        // Dark Overlay sub-cluster (toggle + Darkness).
        Rectangle {
            id: pipBgStrip
            anchors.top: centerPip.bottom
            anchors.topMargin: studio.gap
            anchors.left: centerPip.left
            anchors.right: centerPip.right
            anchors.bottom: parent.bottom  // flush to dashboard bottom; aligned with leftPanel
            color: studio.panelBg
            radius: studio.dp(8)
            border.width: 1
            border.color: studio.panelBorder

            Flickable {
                id: pipBgFlick
                anchors.fill: parent
                anchors.margins: studio.dp(14)
                clip: true
                contentWidth: width
                contentHeight: pipBgCol.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                // ScrollBar in pipBgStrip's right margin, same idea as the
                // left panel — out of the content, in the gutter.
                ScrollBar.vertical: ScrollBar {
                    id: bottomScrollBar
                    parent: pipBgStrip
                    policy: ScrollBar.AsNeeded
                    // Geometry, not anchors: the attached ScrollBar is
                    // created under the Flickable and only later reparented
                    // to pipBgStrip, and anchoring to a non-sibling at that
                    // first evaluation warns at startup.
                    x: pipBgStrip.width - width - studio.dp(6)
                    y: pipBgFlick.y
                    height: pipBgFlick.height
                }

                ColumnLayout {
                    id: pipBgCol
                    width: pipBgFlick.width
                    spacing: studio.rowSpacing

                    // ── PRESETS (moved here from the left panel) ──
                    // Lives under the PiP because tapping a preset shifts
                    // many visuals at once — having the live preview right
                    // above lets the user see all the changes immediately.
                    Text {
                        text: "PRESETS"
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText - 3
                        font.bold: true
                        font.letterSpacing: 1.2
                        font.family: App.Style.fontFamily
                    }
                    // Equal-size button grid: every preset button takes the
                    // same cell width regardless of label length, so the
                    // grid reads as a tidy 2-column matrix instead of an
                    // uneven chip flow.
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: studio.dp(8)
                        rowSpacing: studio.dp(8)
                        Repeater {
                            model: studio.presetDefs
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: studio.dp(40)
                                radius: studio.dp(8)
                                color: presetMouse.containsMouse
                                    ? App.Style.accent
                                    : Qt.rgba(App.Style.primaryTextColor.r,
                                              App.Style.primaryTextColor.g,
                                              App.Style.primaryTextColor.b, 0.06)
                                border.width: 1
                                border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.4)
                                scale: presetMouse.pressed ? 0.96 : 1.0
                                Behavior on color { ColorAnimation { duration: 120 } }
                                Behavior on scale { NumberAnimation { duration: 80 } }

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: presetMouse.containsMouse ? "white" : App.Style.primaryTextColor
                                    font.pixelSize: App.Spacing.overallText - 1
                                    font.family: App.Style.fontFamily
                                    elide: Text.ElideRight
                                }
                                MouseArea {
                                    id: presetMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: studio.applyPreset(modelData)
                                }
                            }
                        }
                    }

                    // ── TRACK TRANSITION ─────────────────────────────
                    // Sits under presets so the live PiP above shows the
                    // chosen animation fire as soon as a chip is tapped.
                    Text {
                        Layout.topMargin: studio.dp(12)
                        text: "TRACK TRANSITION"
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText - 3
                        font.bold: true
                        font.letterSpacing: 1.2
                        font.family: App.Style.fontFamily
                    }
                    NowPlayingButtonGrid {
                        id: trackTransitionCtl
                        options: ["Crossfade", "Slide", "Vinyl Lift", "Dissolve", "Flip",
                                  "Coverflow", "Conveyor", "Stack", "Depth", "Swing"]
                        currentValue: settingsManager ? settingsManager.albumArtTransition : "Crossfade"
                        onSelected: function(v) {
                            if (!settingsManager) return
                            settingsManager.save_album_art_transition(v)
                            centerPip.previewTrackTransition(1, v)
                        }
                        Connections {
                            target: settingsManager
                            function onAlbumArtTransitionChanged() {
                                trackTransitionCtl.currentValue = settingsManager.albumArtTransition
                            }
                        }
                    }
                }
            }
        }

        // (Back chip moved into headerBox at the top of the dashboard.)
    }
}
