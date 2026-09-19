import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import ".." as App

Flow {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: control
    spacing: App.Spacing.overallSpacing
    Layout.fillWidth: true

    property var options: []
    property string currentValue: ""
    property var onSelected: function(value) {}
    property var chipColors: ({})  // optional: { "itemName": "#color" } for per-chip text color
    property var chipImages: ({})  // optional: { "itemName": "file:///path/to/art.jpg" } for album art chips
    property var chipPalettes: ({})  // optional: { "itemName": ["#base", "#baseAlt", "#accent"] } for color-swatch chips
    property var chipFonts: ({})  // optional: { "itemName": "FontFamilyName" } for font-sample chips with name label below
    property var deletableItems: []  // list of item names that show a delete button
    property var onItemDeleted: function(value) {}  // callback when delete is clicked

    // Split options into text chips and image chips for separate row layout
    property var textOptions: {
        var result = []
        for (var i = 0; i < options.length; i++)
            if (!(chipImages && chipImages[options[i]])) result.push(options[i])
        return result
    }
    property var imageOptions: {
        var result = []
        for (var i = 0; i < options.length; i++)
            if (chipImages && chipImages[options[i]]) result.push(options[i])
        return result
    }

    // Shared delete-confirmation popup (one instance for all chips)
    Popup {
        id: deletePopup
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        anchors.centerIn: Overlay.overlay
        width: dp(260)

        property string targetItem: ""

        background: Rectangle {
            color: App.Style.backgroundColor
            radius: dpMin(8, 2)
            border.color: App.Style.accent
            border.width: 1

            layer.enabled: true
            layer.effect: DropShadow {
                horizontalOffset: 0
                verticalOffset: 4
                radius: 20.0
                samples: 21
                color: Qt.rgba(0, 0, 0, 0.4)
            }
        }

        contentItem: ColumnLayout {
            spacing: dp(12)

            Text {
                Layout.fillWidth: true
                text: "Delete \"" + deletePopup.targetItem + "\"?"
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText
                font.bold: true
                font.family: App.Style.fontFamily
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: dp(8)

                // Cancel button
                Rectangle {
                    Layout.fillWidth: true
                    height: App.Spacing.formElementHeight * 0.75
                    radius: dpMin(6, 2)
                    color: cancelMA.containsMouse ? App.Style.hoverColor : "transparent"
                    border.width: 1
                    border.color: Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.2)

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                    }
                    MouseArea {
                        id: cancelMA
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: deletePopup.close()
                    }
                }

                // Delete button
                Rectangle {
                    Layout.fillWidth: true
                    height: App.Spacing.formElementHeight * 0.75
                    radius: dpMin(6, 2)
                    color: deleteMA.containsMouse ? Qt.lighter(App.Style.accent, 1.2) : App.Style.accent

                    Text {
                        anchors.centerIn: parent
                        text: "Delete"
                        color: "white"
                        font.pixelSize: App.Spacing.overallText
                        font.bold: true
                        font.family: App.Style.fontFamily
                    }
                    MouseArea {
                        id: deleteMA
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            control.onItemDeleted(deletePopup.targetItem)
                            deletePopup.close()
                        }
                    }
                }
            }
        }
    }

    Component {
        id: chipDelegate
        Item {
            required property int index
            required property var modelData

            id: chipWrapper
            property bool isDeletable: control.deletableItems.indexOf(modelData) !== -1
            property bool hasImage: control.chipImages && control.chipImages[modelData] ? true : false
            property bool hasPalette: !hasImage
                && !!control.chipPalettes
                && !!control.chipPalettes[modelData]
                && control.chipPalettes[modelData].length > 0
            property bool hasFont: !hasImage && !hasPalette
                && control.chipFonts
                && control.chipFonts[modelData] !== undefined
            width: chipRect.width
            height: chipRect.height

            // Glow behind selected chip (spacecraft) — replaces DropShadow
            Rectangle {
                anchors.centerIn: chipRect
                width: chipRect.width + 4
                height: chipRect.height + 4
                radius: chipRect.radius + 2
                color: Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.25)
                visible: App.EnvironmentTheme.active.chipAccentBorder && modelData === control.currentValue
            }

            Rectangle {
                id: chipRect
                // Image and palette chips are square; font chips are wider
                // rectangles so the font name has room to render legibly;
                // plain text chips size to their content.
                width: chipWrapper.hasFont
                    ? App.Spacing.formElementHeight * 4.0
                    : (chipWrapper.hasImage || chipWrapper.hasPalette)
                        ? height
                        : chipText.width + App.Spacing.overallSpacing * 3 + (chipWrapper.isDeletable ? textDeleteBtn.width + dp(4) : 0)
                height: (chipWrapper.hasImage || chipWrapper.hasPalette || chipWrapper.hasFont)
                    ? App.Spacing.formElementHeight * 2.4
                    : App.Spacing.formElementHeight * 0.8
                radius: (chipWrapper.hasImage || chipWrapper.hasPalette || chipWrapper.hasFont)
                    ? 0
                    : (App.EnvironmentTheme.active.chipRadius === -1
                        ? height / 2 : dpMin(App.EnvironmentTheme.active.chipRadius, 2))

                clip: true

                color: (chipWrapper.hasImage || chipWrapper.hasPalette || chipWrapper.hasFont)
                    ? "#222222"
                    : (modelData === control.currentValue ? App.Style.accent : App.Style.hoverColor)

                border.width: (chipWrapper.hasImage || chipWrapper.hasPalette || chipWrapper.hasFont)
                    ? (modelData === control.currentValue ? dpMin(4, 2) : 1)
                    : (App.EnvironmentTheme.active.chipAccentBorder
                        ? 1
                        : (modelData === control.currentValue ? 0 : 1))
                border.color: (chipWrapper.hasImage || chipWrapper.hasPalette || chipWrapper.hasFont)
                    ? (modelData === control.currentValue
                        ? App.Style.accent
                        : Qt.rgba(App.Style.primaryTextColor.r, App.Style.primaryTextColor.g, App.Style.primaryTextColor.b, 0.2))
                    : (App.EnvironmentTheme.active.chipAccentBorder
                        ? (modelData === control.currentValue
                            ? Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.8)
                            : Qt.rgba(App.Style.accent.r, App.Style.accent.g, App.Style.accent.b, 0.3))
                        : Qt.rgba(App.Style.primaryTextColor.r,
                                  App.Style.primaryTextColor.g,
                                  App.Style.primaryTextColor.b, 0.1))

                // Chip click area — click to select, press-and-hold to delete (image/palette chips)
                MouseArea {
                    id: chipMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: control.onSelected(modelData)
                    onPressAndHold: {
                        if ((chipWrapper.hasImage || chipWrapper.hasPalette) && chipWrapper.isDeletable) {
                            deletePopup.targetItem = modelData
                            deletePopup.open()
                        }
                    }
                    onEntered: chipRect.scale = 1.05
                    onExited: chipRect.scale = 1.0
                }

                // ========== IMAGE CHIP CONTENT ==========
                Image {
                    id: chipArtImage
                    visible: chipWrapper.hasImage
                    anchors.fill: parent
                    anchors.margins: 1
                    source: chipWrapper.hasImage ? control.chipImages[modelData] : ""
                    sourceSize.width: chipRect.width * 2
                    sourceSize.height: chipRect.height * 2
                    fillMode: Image.PreserveAspectCrop
                    smooth: true
                    asynchronous: true
                    cache: true
                }

                // ========== PALETTE CHIP CONTENT ==========
                Column {
                    visible: chipWrapper.hasPalette
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0

                    Rectangle {
                        width: parent.width
                        height: parent.height / 3
                        color: chipWrapper.hasPalette ? control.chipPalettes[modelData][0] : "transparent"
                    }
                    Rectangle {
                        width: parent.width
                        height: parent.height / 3
                        color: chipWrapper.hasPalette ? control.chipPalettes[modelData][1] : "transparent"
                    }
                    Rectangle {
                        width: parent.width
                        height: parent.height - 2 * Math.floor(parent.height / 3)
                        color: chipWrapper.hasPalette ? control.chipPalettes[modelData][2] : "transparent"
                    }
                }

                // ========== FONT CHIP CONTENT ==========
                // The font's own name, rendered in that font. Auto-scales
                // between minimumPixelSize and font.pixelSize so the full
                // name always fits without ellipsis.
                Text {
                    visible: chipWrapper.hasFont
                    anchors.fill: parent
                    anchors.leftMargin: dp(10)
                    anchors.rightMargin: dp(10)
                    anchors.topMargin: dp(4)
                    anchors.bottomMargin: dp(4)
                    text: String(modelData).split(" ")[0]
                    color: "white"
                    font.family: chipWrapper.hasFont
                        ? (control.chipFonts[modelData] || App.Style.systemDefaultFont || App.Style.fontFamily)
                        : App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 1.3
                    fontSizeMode: Text.Fit
                    minimumPixelSize: dp(8)
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideNone
                    wrapMode: Text.NoWrap
                }

                // ========== TEXT CHIP CONTENT ==========
                Row {
                    visible: !chipWrapper.hasImage && !chipWrapper.hasPalette && !chipWrapper.hasFont
                    anchors.centerIn: parent
                    spacing: dp(4)

                    Text {
                        id: chipText
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        visible: !chipWrapper.hasImage && !chipWrapper.hasPalette && !chipWrapper.hasFont
                        color: modelData === control.currentValue ? "white" : ((control.chipColors && control.chipColors[modelData]) || App.Style.primaryTextColor)
                        font.pixelSize: App.Spacing.overallText
                        font.family: App.Style.fontFamily
                    }

                    // Delete button for text chips (inline × button)
                    Rectangle {
                        id: textDeleteBtn
                        visible: chipWrapper.isDeletable && !chipWrapper.hasImage && !chipWrapper.hasPalette && !chipWrapper.hasFont
                        width: textDeleteBtnText.width + dp(8)
                        height: parent.height
                        color: "transparent"
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            id: textDeleteBtnText
                            anchors.centerIn: parent
                            text: "\u00D7"
                            color: textDeleteBtnMA.containsMouse
                                ? (modelData === control.currentValue ? "white" : Qt.lighter(App.Style.accent, 1.3))
                                : (modelData === control.currentValue ? Qt.rgba(1,1,1,0.6) : App.Style.secondaryTextColor)
                            font.pixelSize: App.Spacing.overallText * 1.2
                            font.bold: true
                            font.family: App.Style.fontFamily
                        }

                        MouseArea {
                            id: textDeleteBtnMA
                            anchors.fill: parent
                            hoverEnabled: true
                            z: 10
                            onClicked: control.onItemDeleted(modelData)
                        }
                    }
                }

                // Standard DropShadow (hidden in spacecraft mode)
                layer.enabled: !App.EnvironmentTheme.active.chipAccentBorder && modelData === control.currentValue
                layer.effect: DropShadow {
                    horizontalOffset: 0
                    verticalOffset: 2
                    radius: 4.0
                    samples: 9
                    color: Qt.rgba(0, 0, 0, 0.2)
                }

                Behavior on scale {
                    NumberAnimation {
                        duration: 100
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
    }

    // Text chips
    Repeater {
        model: control.textOptions
        delegate: chipDelegate
    }

    // Line break to force image chips onto their own row — only needed when
    // text chips precede them. Without text chips, this 1px item plus the
    // Flow's row spacing would add unwanted vertical padding above the
    // image chips.
    Item {
        width: control.width
        height: 1
        visible: control.imageOptions.length > 0 && control.textOptions.length > 0
    }

    // Image chips (album art) — flow together on their own row(s)
    Repeater {
        model: control.imageOptions
        delegate: chipDelegate
    }
}
