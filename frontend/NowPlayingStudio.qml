import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as App
import "settings"

// The Now Playing look editor, as the panels MediaRoom shows around itself.
//
// It is not a page and has no preview of its own: the customize button at
// the top right of MediaRoom shrinks the page itself into a mirror at the
// top right (MediaRoom.studioOpen / studioProgress) and loads this file over
// the room that frees. A tap on the mirror brings the page back. So every
// control here is seen on the real page while it moves.
//
//   wide window                         tall window
//   ┌───────────────┬────────┐          ┌────────┬────────┐
//   │               │ mirror │          │ output │ mirror │
//   │     panel     ├────────┤          ├────────┴────────┤
//   │               │ output │          │      panel      │
//   └───────────────┴────────┘          └─────────────────┘
//
// MediaRoom owns the mirror (its size comes in as mirrorWidth / mirrorHeight,
// its place is `margin` from the top right); the panel holds the look's
// controls, the strip by the mirror the audio output picker.
//
// ── VISUAL STANDARD ──────────────────────────────────────────────────────
// Every setting is a full-width row. Labels stack at the LEFT edge of the
// section, controls stack at the RIGHT (toggles) or fill the row width
// (sliders, chips). Spacing is named, never inline magic numbers:
//   studio.rowSpacing         siblings inside a section
//   studio.sectionSpacing     between sections
//   studio.subClusterSpacing  master toggle → its dependents
//   studio.headerSpacing      section header → first control
//   studio.subClusterIndent   leftMargin on indented dependents
// Toggles keep their `checked` binding (no Connections needed); sliders
// follow the blurSlider pattern (ColumnLayout with a RowLayout header).
Item {
    id: studio
    function dp(s) { return Math.round(s * (App.Spacing.effectiveScale || 1.0)) }

    // The mirror MediaRoom draws `margin` from the top right; the panels go around it
    property real mirrorWidth: 0
    property real mirrorHeight: 0
    property int margin: dp(10)
    property int gap: dp(10)
    // Tall window: the output strip sits beside the mirror, the panel below both
    property bool stacked: false


    readonly property int rowSpacing:        dp(8)   // sibling controls inside a section
    readonly property int sectionSpacing:    dp(20)  // between sections
    readonly property int subClusterSpacing: dp(8)   // master toggle → its dependents
    readonly property int headerSpacing:     dp(8)   // section header → first control
    readonly property int subClusterIndent:  dp(8)   // left margin on indented dependents
    readonly property int subClusterBarWidth: 2      // accent-bar width on dependents

    readonly property color panelBorder: Qt.rgba(App.Style.accent.r,
                                                  App.Style.accent.g,
                                                  App.Style.accent.b, 0.25)
    readonly property color groupDivider: Qt.rgba(App.Style.primaryTextColor.r,
                                                  App.Style.primaryTextColor.g,
                                                  App.Style.primaryTextColor.b, 0.12)

    // Left edge of the mirror, and the width left of it
    readonly property real _mirrorX: width - margin - mirrorWidth
    readonly property real _besideMirror: Math.max(0, _mirrorX - gap - margin)
    readonly property real _belowMirror: margin + mirrorHeight + gap

    // ── MAIN PANEL: every control, one scrolling column ──
    Rectangle {
        id: leftPanel
        x: studio.margin
        y: studio.stacked ? studio._belowMirror : studio.margin
        width: studio.stacked ? Math.max(0, studio.width - 2 * studio.margin) : studio._besideMirror
        height: Math.max(0, studio.height - y - studio.margin)
        // Transparent fill: each setting card carries its own background
        color: "transparent"
        radius: studio.dp(8)
        border.width: 1
        border.color: studio.panelBorder

        // Swallows presses between the cards, so they don't reach the page
        MouseArea { anchors.fill: parent; onWheel: function(wheel) { wheel.accepted = true } }

        Flickable {
            id: leftFlick
            anchors.fill: parent
            anchors.margins: studio.dp(20)
            clip: true
            contentWidth: width
            contentHeight: leftCol.implicitHeight
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.DragAndOvershootBounds
            // In the panel's right margin, clear of toggles and slider tracks
            ScrollBar.vertical: ScrollBar {
                id: leftPanelScrollBar
                parent: leftPanel
                policy: ScrollBar.AsNeeded
                // Geometry, not anchors: the attached ScrollBar is created
                // under the Flickable and only later reparented to leftPanel,
                // and anchoring to a non-sibling at that first evaluation
                // warns at startup.
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

                    SettingCategory {
                        title: "Waveform"
                        inlineContent: SettingsToggle {
                            id: waveformToggle
                            compact: true
                            Layout.preferredWidth: studio.dp(104)
                            Layout.minimumWidth: studio.dp(104)
                            checked: settingsManager ? settingsManager.showWaveformVisualizer : true
                            activeColor: App.Style.accent
                            inactiveColor: App.Style.hoverColor
                            onToggled: function(c) { if (settingsManager) settingsManager.save_show_waveform_visualizer(c) }
                        }
                    }

                }
            }
        }
    }

    // ── STRIP: where the music plays ──
    // One row per audio output, as there can be many and their names are
    // long: a list that scrolls, not chips. Rows are told apart by id (two
    // outputs can have the same name). The same setting as Settings > Media >
    // Audio Output: a picked output that is unplugged falls back to the
    // system default until it returns.
    Rectangle {
        id: outputStrip
        objectName: "studioOutputStrip"
        x: studio.stacked ? studio.margin : studio._mirrorX
        y: studio.stacked ? studio.margin : studio._belowMirror
        width: studio.stacked ? studio._besideMirror : studio.mirrorWidth
        height: studio.stacked ? studio.mirrorHeight : Math.max(0, studio.height - y - studio.margin)
        // As the main panel: a frame, the card inside carries the background
        color: "transparent"
        radius: studio.dp(8)
        border.width: 1
        border.color: studio.panelBorder

        readonly property var outputs: typeof mediaManager !== "undefined" && mediaManager ? mediaManager.audioOutputs : []
        readonly property string playingOn: typeof mediaManager !== "undefined" && mediaManager ? mediaManager.currentAudioOutput : ""
        readonly property string picked: settingsManager ? settingsManager.audioOutputDevice : ""
        readonly property bool pickedPresent: {
            for (var i = 0; i < outputs.length; i++)
                if (outputs[i].id === picked) return true
            return false
        }
        // The row that is lit: the picked output while it is there, else the default
        readonly property string shownId: pickedPresent ? picked : ""
        readonly property var rows: {
            var list = [{ id: "", name: "System default" }]
            for (var i = 0; i < outputs.length; i++)
                list.push({ id: outputs[i].id, name: outputs[i].name })
            return list
        }

        // Swallows presses beside the card, so they don't reach the page
        MouseArea { anchors.fill: parent; onWheel: function(wheel) { wheel.accepted = true } }

        Flickable {
            id: outputFlick
            anchors.fill: parent
            anchors.margins: studio.dp(20)
            clip: true
            contentWidth: width
            contentHeight: outputCol.implicitHeight
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.DragAndOvershootBounds
            ScrollBar.vertical: ScrollBar {
                parent: outputStrip
                policy: ScrollBar.AsNeeded
                // Geometry, not anchors (see leftPanelScrollBar)
                x: outputStrip.width - width - studio.dp(6)
                y: outputFlick.y
                height: outputFlick.height
            }

            ColumnLayout {
                id: outputCol
                width: outputFlick.width
                spacing: studio.rowSpacing

                // Section label and card, as in the main panel
                Text {
                    text: "AUDIO"
                    color: App.Style.secondaryTextColor
                    font.pixelSize: App.Spacing.overallText - 3
                    font.bold: true
                    font.letterSpacing: 1.2
                    font.family: App.Style.fontFamily
                }

                SettingCategory {
                    title: "Output"
                    description: outputStrip.picked !== "" && !outputStrip.pickedPresent
                        ? "The picked output is not connected; playing on " + (outputStrip.playingOn || "the system default") + " until it is."
                        : (outputStrip.playingOn ? "Playing on " + outputStrip.playingOn + "." : "Where the music plays.")

                    // The buttons of NowPlayingButtonGrid, one to a row
                    Repeater {
                        model: outputStrip.rows
                        delegate: Rectangle {
                            id: outputRow
                            readonly property bool isSelected: outputStrip.shownId === modelData.id
                            Layout.fillWidth: true
                            Layout.preferredHeight: studio.dp(36)
                            radius: studio.dp(8)
                            color: isSelected
                                ? App.Style.accent
                                : (outputRowMouse.containsMouse
                                    ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.18)
                                    : Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g,
                                              App.Style.primaryTextColor.b, 0.06))
                            border.width: 1
                            border.color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.4)
                            scale: outputRowMouse.pressed ? 0.96 : 1.0
                            Behavior on color { ColorAnimation { duration: 120 } }
                            Behavior on scale { NumberAnimation { duration: 80 } }

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: studio.dp(10)
                                anchors.rightMargin: studio.dp(10)
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                text: modelData.name
                                color: outputRow.isSelected ? "white" : App.Style.primaryTextColor
                                font.pixelSize: App.Spacing.overallText - 1
                                font.family: App.Style.fontFamily
                                elide: Text.ElideRight
                            }
                            MouseArea {
                                id: outputRowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (settingsManager) settingsManager.save_audio_output_device(modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }
}
