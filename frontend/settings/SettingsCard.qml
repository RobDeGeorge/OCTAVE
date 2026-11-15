import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as App
import "ExpandedCards.js" as ExpandedCards

Rectangle {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: card
    default property alias cardContent: contentLayout.data

    // Collapsible card properties
    property string cardId: ""
    property string title: ""
    property string description: ""

    // Read persisted state from the in-memory cache (no disk I/O).
    // The cache is a .pragma library singleton loaded once at startup
    // (from SettingsMenu.qml), so no settingsManager reference here
    // to avoid binding re-evaluation cascades.
    property bool expanded: {
        if (cardId === "") return true
        return ExpandedCards.isExpanded(cardId)
    }

    // Whether this card is collapsible (requires cardId)
    readonly property bool collapsible: cardId !== ""

    // True once the component is fully constructed — gates animations
    property bool _initialized: false

    Layout.fillWidth: true
    color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.07)
    radius: dpMin(App.EnvironmentTheme.active.cardRadius, 2)
    implicitHeight: mainLayout.implicitHeight + App.Spacing.overallSpacing * 3
    clip: true

    Component.onCompleted: _initialized = true

    // Persist expanded state changes — update cache immediately, debounce disk write
    onExpandedChanged: {
        if (!_initialized) return
        if (collapsible) {
            ExpandedCards.setExpanded(cardId, expanded)
            _saveTimer.restart()
        }
    }

    // Debounced save — coalesces rapid expand/collapse into a single disk write
    Timer {
        id: _saveTimer
        interval: 500
        repeat: false
        onTriggered: ExpandedCards.save(settingsManager)
    }

    // Accent border (spacecraft)
    border.width: App.EnvironmentTheme.active.accentBorder ? 1 : 0
    border.color: App.EnvironmentTheme.active.accentBorder
        ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b,
                  App.EnvironmentTheme.active.accentBorderOpacity) : "transparent"

    // Top-edge accent highlight (spacecraft)
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: dp(App.EnvironmentTheme.active.cardRadius)
        anchors.rightMargin: dp(App.EnvironmentTheme.active.cardRadius)
        height: 1
        color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.2)
        visible: App.EnvironmentTheme.active.accentBorder && !App.EnvironmentTheme.active.cardGlassEffect
    }

    // Frosted glass gradient overlay (deep sea)
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        visible: App.EnvironmentTheme.active.cardGlassEffect
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.05) }
            GradientStop { position: 0.4; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.03) }
        }
    }

    // Soft top-edge glow (deep sea — inset accent shimmer)
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 1
        anchors.leftMargin: dp(App.EnvironmentTheme.active.cardRadius * 0.5)
        anchors.rightMargin: dp(App.EnvironmentTheme.active.cardRadius * 0.5)
        height: 1
        radius: 0.5
        color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.15)
        visible: App.EnvironmentTheme.active.cardGlassEffect
    }

    App.CornerBrackets {
        bracketLength: dp(15)
        visible: App.EnvironmentTheme.active.cornerBrackets
    }

    ColumnLayout {
        id: mainLayout
        anchors {
            fill: parent
            margins: App.Spacing.overallSpacing * 1.5
        }
        spacing: App.Spacing.rowSpacing

        // Collapsible header (only shown when cardId is set)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: App.Spacing.rowSpacing
            visible: card.collapsible

            // Title row with collapse toggle
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: headerRow.implicitHeight
                color: "transparent"

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: card.expanded = !card.expanded
                }

                RowLayout {
                    id: headerRow
                    anchors {
                        left: parent.left
                        right: parent.right
                    }
                    spacing: App.Spacing.overallSpacing

                    // Title with HUD accent line
                    SettingLabel {
                        text: card.title
                        font.pixelSize: App.Spacing.overallText * 1.5
                        font.bold: true
                        Layout.fillWidth: false
                    }

                    // Filler between title and the collapse arrow. Always takes
                    // layout space so the arrow stays right-anchored even when
                    // the HUD accent line is theme-hidden — otherwise Qt
                    // Layouts drop an invisible item and the arrow packs next
                    // to the title at a title-width-dependent x position.
                    Item {
                        Layout.fillWidth: true
                        height: 1
                        Layout.alignment: Qt.AlignVCenter

                        // HUD accent line (spacecraft themes only)
                        Rectangle {
                            anchors.fill: parent
                            color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.4)
                            visible: App.EnvironmentTheme.active.sectionHeaderLines
                        }
                    }

                    // Collapse indicator arrow
                    Text {
                        id: collapseArrow
                        text: card.expanded ? "\u25BC" : "\u25B6"
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        opacity: 0.6

                        Behavior on text {
                            SequentialAnimation {
                                NumberAnimation { target: collapseArrow; property: "opacity"; to: 0; duration: 80 }
                                NumberAnimation { target: collapseArrow; property: "opacity"; to: 0.6; duration: 170 }
                            }
                        }
                    }
                }
            }

            // Description (visible when expanded and description is set)
            SettingDescription {
                text: card.description
                visible: card.description !== "" && card.expanded
            }
        }

        // Content area
        ColumnLayout {
            id: contentLayout
            Layout.fillWidth: true
            spacing: App.Spacing.rowSpacing
            visible: !card.collapsible || card.expanded

            // Animated collapse
            Layout.preferredHeight: card.collapsible
                ? (card.expanded ? implicitHeight : 0)
                : implicitHeight

            Behavior on Layout.preferredHeight {
                enabled: card.collapsible && card._initialized
                NumberAnimation { duration: 250; easing.type: Easing.InOutQuad }
            }
        }
    }
}
