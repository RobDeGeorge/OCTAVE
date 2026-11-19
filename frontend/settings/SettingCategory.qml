import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as App

// Lightweight category card — wraps a single setting (or tightly coupled group
// of controls) with a promoted header and subtle background. Lighter chrome
// than SettingsCard: no border, brackets, or glass effects.
//
// Two content slots:
//   • categoryContent (default) — stacks vertically below the header (e.g. chips,
//     subcategories, multi-row inputs)
//   • inlineContent — sits on the right of the header, centred vertically on
//     the title and description (e.g. a single toggle, or a value display)
Rectangle {
    id: cat

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    default property alias categoryContent: contentLayout.data
    property alias inlineContent: inlineLayout.data
    property string title: ""
    property string description: ""

    Layout.fillWidth: true
    color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b,
                   cardTap.pressed ? 0.1 : 0.05)

    // The switch in the title row, if that is what the inline slot holds
    readonly property var inlineToggle: {
        for (var i = 0; i < inlineLayout.children.length; i++) {
            var c = inlineLayout.children[i]
            if (c && typeof c.flip === "function" && c.visible)
                return c
        }
        return null
    }

    // A card with a switch is the switch: a tap anywhere on it flips it.
    // Where the card also has controls under the title (a slider that
    // shows while it is on), only the title and description do, so a tap
    // that misses the slider changes nothing. Declared first, so it lies
    // under the card's own controls and they get their taps as before.
    MouseArea {
        id: cardTap
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: contentLayout.implicitHeight > 0 ? mainLayout.y + contentLayout.y : parent.height
        enabled: cat.inlineToggle !== null && cat.inlineToggle.enabled
        onClicked: cat.inlineToggle.flip()
    }
    radius: dpMin(App.EnvironmentTheme.active.cardRadius, 2)
    implicitHeight: mainLayout.implicitHeight + App.Spacing.overallSpacing * 2

    ColumnLayout {
        id: mainLayout
        anchors {
            fill: parent
            margins: App.Spacing.overallSpacing
        }
        spacing: 0

        // Header: title and description on the left, the inline controls on
        // the right, centred on the two together (so a switch sits at the
        // middle of a card that is only a title and a description).
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Spacing.overallSpacing
            visible: cat.title !== "" || cat.description !== "" || inlineLayout.children.length > 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: App.Spacing.rowSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing
                    visible: cat.title !== ""

                    SettingLabel {
                        text: cat.title
                        font.pixelSize: App.Spacing.overallText * 1.5
                        font.bold: true
                        Layout.fillWidth: false
                    }

                    // Accent line after the title where the theme has one and
                    // nothing sits inline; otherwise an empty spacer.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        height: 1
                        color: inlineLayout.children.length > 0 || !App.EnvironmentTheme.active.sectionHeaderLines
                            ? "transparent"
                            : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.4)
                    }
                }

                SettingDescription {
                    text: cat.description
                    visible: cat.description !== ""
                    Layout.fillWidth: true
                }
            }

            RowLayout {
                id: inlineLayout
                Layout.alignment: Qt.AlignVCenter
                spacing: App.Spacing.overallSpacing
            }
        }

        // Default stacked content (used when categoryContent has children)
        ColumnLayout {
            id: contentLayout
            Layout.fillWidth: true
            Layout.topMargin: implicitHeight > 0 ? App.Spacing.rowSpacing : 0
            spacing: App.Spacing.rowSpacing
        }
    }
}
