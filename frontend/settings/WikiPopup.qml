import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as App

Popup {
    id: popup
    objectName: "wikiPopup"
    // Use the existing page area so the app's bottom/side navigation stays
    // visible and interactive. Standalone hosts can still use the full window.
    property Item contentArea: null
    property var navigationOwner: null
    readonly property bool suspended: opened && contentArea && navigationOwner
                                      && contentArea.currentItem !== navigationOwner
    parent: contentArea || Overlay.overlay
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    padding: 0
    modal: false
    dim: false
    focus: !suspended
    enabled: !suspended
    opacity: suspended ? 0 : 1
    closePolicy: Popup.CloseOnEscape

    readonly property bool viewerAvailable: typeof wikiViewerAvailable !== "undefined" && wikiViewerAvailable
    readonly property url homeUrl: typeof wikiHomeUrl !== "undefined" ? wikiHomeUrl : ""
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }

    background: Rectangle { color: App.Style.backgroundColor }

    Connections {
        target: browserLoader.item
        ignoreUnknownSignals: true
        function onCloseRequested() { popup.close() }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            // The browser supplies its own compact toolbar. Keep a Close
            // button available even when the optional browser cannot load.
            visible: !popup.viewerAvailable || browserLoader.status === Loader.Error
            Layout.fillWidth: true
            Layout.margins: popup.dp(12)
            spacing: popup.dp(8)

            Text {
                Layout.fillWidth: true
                text: "Wiki"
                color: App.Style.primaryTextColor
                font.family: App.Style.fontFamily
                font.pixelSize: App.Spacing.overallText * 1.2
                font.weight: Font.Medium
                elide: Text.ElideRight
            }

            SettingsButton {
                objectName: "wikiUnavailableCloseButton"
                text: "Close"
                Layout.preferredHeight: popup.dp(44)
                onClicked: popup.close()
            }
        }

        Loader {
            id: browserLoader
            objectName: "wikiBrowserLoader"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Hidden (not just transparent) while another page is shown:
            // on Android the reader is a native WebView layered above the
            // Qt window, which ignores QML opacity and would stay on screen.
            visible: popup.viewerAvailable && status !== Loader.Error && !popup.suspended
            // Neither Chromium nor the page remains alive after Close.
            active: popup.opened && popup.viewerAvailable
            // Chromium on desktop, the platform WebView on Android (see WikiNativeView.qml).
            source: (typeof isAndroid !== "undefined" && isAndroid) ? "WikiNativeView.qml" : "WikiWebView.qml"
            onLoaded: item.homeUrl = popup.homeUrl
        }

        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: popup.dp(24)
            visible: !popup.viewerAvailable || browserLoader.status === Loader.Error
            text: "The wiki viewer isn’t available in this build."
            color: App.Style.secondaryTextColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
