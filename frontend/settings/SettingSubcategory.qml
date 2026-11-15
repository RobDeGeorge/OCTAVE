import QtQuick 2.15
import QtQuick.Layouts 1.15
import ".." as App

// Nested "card-within-a-card" used inside SettingCategory for grouped
// sub-controls (e.g. an enable toggle + the controls it gates). When
// `togglable` is true, the header carries the switch and the body is
// only rendered while `checked` is true.
Rectangle {
    id: sub

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    default property alias subContent: contentLayout.data
    property string title: ""
    property string description: ""
    property bool togglable: false
    property bool checked: false

    // Whether the body should currently be revealed.
    readonly property bool _bodyVisible: !togglable || checked

    // Suppress the reveal animation on initial mount so a sub-card that loads
    // already-on doesn't visibly slide down.
    property bool _ready: false
    Component.onCompleted: Qt.callLater(function() { _ready = true })

    signal toggled(bool checked)

    Layout.fillWidth: true
    color: Qt.rgba(App.Style.primaryTextColor.r,
                   App.Style.primaryTextColor.g,
                   App.Style.primaryTextColor.b, 0.04)
    radius: dpMin(App.EnvironmentTheme.active.cardRadius * 0.7, 2)
    border.width: 1
    border.color: Qt.rgba(App.Style.primaryTextColor.r,
                          App.Style.primaryTextColor.g,
                          App.Style.primaryTextColor.b, 0.07)
    implicitHeight: mainLayout.implicitHeight + App.Spacing.overallSpacing * 1.5

    ColumnLayout {
        id: mainLayout
        anchors {
            fill: parent
            margins: Math.round(App.Spacing.overallSpacing * 0.75)
        }
        spacing: App.Spacing.rowSpacing

        // Header — title and (optional) description stacked
        SettingLabel {
            text: sub.title
            font.bold: true
            visible: sub.title !== ""
            Layout.fillWidth: true
        }

        SettingDescription {
            text: sub.description
            visible: sub.description !== ""
            Layout.fillWidth: true
        }

        // Enable switch — sits below the description, above the gated body
        SettingsToggle {
            id: enableToggle
            visible: sub.togglable
            compact: true
            text: ""
            checked: sub.checked
            activeColor: App.Style.accent
            inactiveColor: App.Style.hoverColor
            onToggled: function(c) { sub.toggled(c) }
        }

        // Body — the gated content. Animates open/closed via clipped height
        // + opacity. `enabled` follows visibility so collapsed children
        // don't catch input.
        Item {
            id: bodyHost
            Layout.fillWidth: true
            Layout.preferredHeight: sub._bodyVisible ? contentLayout.implicitHeight : 0
            opacity: sub._bodyVisible ? 1.0 : 0.0
            enabled: sub._bodyVisible
            clip: true

            Behavior on Layout.preferredHeight {
                enabled: sub._ready
                NumberAnimation { duration: 250; easing.type: Easing.OutQuad }
            }

            Behavior on opacity {
                enabled: sub._ready
                NumberAnimation { duration: 250; easing.type: Easing.OutQuad }
            }

            ColumnLayout {
                id: contentLayout
                width: parent.width
                spacing: Math.round(App.Spacing.rowSpacing * 0.5)
            }
        }
    }
}
