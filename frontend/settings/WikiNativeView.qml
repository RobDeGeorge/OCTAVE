import QtQuick
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtWebView
import ".." as App

// Android counterpart of WikiWebView.qml. Qt WebEngine (Chromium) does not
// exist on Android; Qt WebView wraps the platform's native WebView instead and
// reads the wiki straight out of the APK (file:///android_asset/wiki/...).
// Same properties, signals and toolbar as the WebEngine reader so WikiPopup
// can load either one; differences are only where the WebView API is smaller
// (no user scripts, so the reader bootstrap is injected after each load).
Item {
    id: viewer
    property url homeUrl: ""
    property string loadError: ""
    signal closeRequested()
    property real readerScale: 1.0
    readonly property bool compactToolbar: width < dp(900)
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function buildReaderConfig() {
        return {
            size: Math.max(14, Math.round(22 * (App.Spacing.effectiveScale || 1.0)
                                       * App.Spacing.textScale * readerScale)) + "px",
            font: JSON.stringify(App.Style.fontFamily || "sans-serif") + ", sans-serif",
            bg: String(App.Style.backgroundColor), surface: String(App.Style.contentColor),
            text: String(App.Style.primaryTextColor), muted: String(App.Style.secondaryTextColor),
            accent: String(App.Style.accent)
        }
    }
    function buildReaderBootstrap() {
        const base = String(homeUrl).slice(0, String(homeUrl).lastIndexOf("/") + 1)
        return "(function(){if(location.href.indexOf(" + JSON.stringify(base)
            + ")!==0)return;window.__octaveReaderConfig=" + JSON.stringify(buildReaderConfig())
            + ";if(window.octaveWiki){window.octaveWiki.configure(window.__octaveReaderConfig);return;}"
            + "if(document.getElementById('octave-reader-script'))return;"
            + "var s=document.createElement('script');s.id='octave-reader-script';s.src="
            + JSON.stringify(base + "embedded.js") + ";document.head.appendChild(s);})()"
    }
    function configureReader() {
        if (String(homeUrl) !== "") browser.runJavaScript(buildReaderBootstrap())
    }
    onReaderScaleChanged: configureReader()
    onHomeUrlChanged: browser.url = homeUrl

    component ReaderButton: ToolButton {
        implicitHeight: Math.max(48, viewer.dp(48))
        Layout.minimumWidth: Math.max(48, viewer.dp(60))
        font.family: App.Style.fontFamily
        font.pixelSize: Math.max(16, App.Spacing.overallText * 0.9)
        palette.buttonText: App.Style.primaryTextColor
        opacity: enabled ? 1.0 : 0.35
        background: Rectangle {
            radius: viewer.dp(8)
            color: parent.down ? App.Style.accent : App.Style.contentColor
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: viewer.dp(12)
            Layout.rightMargin: viewer.dp(12)
            Layout.topMargin: viewer.dp(4)
            Layout.bottomMargin: viewer.dp(4)
            spacing: viewer.dp(6)

            ReaderButton {
                objectName: "wikiBackButton"
                text: "Back"
                enabled: browser.canGoBack
                onClicked: browser.goBack()
            }
            ReaderButton {
                objectName: "wikiForwardButton"
                visible: !viewer.compactToolbar
                text: "Forward"
                enabled: browser.canGoForward
                onClicked: browser.goForward()
            }
            ReaderButton {
                objectName: "wikiHomeButton"
                visible: !viewer.compactToolbar
                text: "Home"
                onClicked: browser.url = viewer.homeUrl
            }
            ReaderButton {
                objectName: "wikiTopicsButton"
                text: "Topics"
                onClicked: browser.runJavaScript("if(window.octaveWiki) window.octaveWiki.toggleTopics()")
            }
            ReaderButton {
                objectName: "wikiSearchButton"
                text: "Search"
                onClicked: browser.runJavaScript("if(window.octaveWiki) window.octaveWiki.search()")
            }
            ReaderButton {
                text: "More"
                visible: viewer.compactToolbar
                onClicked: moreMenu.open()
                Menu {
                    id: moreMenu
                    MenuItem { text: "Forward"; enabled: browser.canGoForward; height: viewer.dp(52); onTriggered: browser.goForward() }
                    MenuItem { text: "Wiki home"; height: viewer.dp(52); onTriggered: browser.url = viewer.homeUrl }
                }
            }
            Text {
                Layout.fillWidth: true
                text: viewer.width >= viewer.dp(1150) ? browser.title : ""
                color: App.Style.secondaryTextColor
                font.family: App.Style.fontFamily
                font.pixelSize: App.Spacing.overallText * 0.85
                elide: Text.ElideRight
            }
            ReaderButton {
                objectName: "wikiSmallerTextButton"
                text: "A−"
                Accessible.name: "Smaller text"
                enabled: viewer.readerScale > 0.61
                onClicked: viewer.readerScale = Math.max(0.6, viewer.readerScale - 0.1)
            }
            ReaderButton {
                objectName: "wikiLargerTextButton"
                text: "A+"
                Accessible.name: "Larger text"
                enabled: viewer.readerScale < 1.39
                onClicked: viewer.readerScale = Math.min(1.4, viewer.readerScale + 0.1)
            }
            SettingsButton {
                objectName: "closeWikiButton"
                text: "Close"
                Layout.preferredHeight: Math.max(48, viewer.dp(48))
                onClicked: viewer.closeRequested()
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            Layout.preferredHeight: viewer.dp(3)
            from: 0; to: 100
            value: browser.loadProgress
            visible: browser.loading
        }

        Text {
            Layout.fillWidth: true
            Layout.margins: viewer.dp(16)
            visible: viewer.loadError !== ""
            text: "This page couldn’t be loaded. Return Home or close the reader."
            color: App.Style.primaryTextColor
            font.pixelSize: App.Spacing.overallText
            font.family: App.Style.fontFamily
            wrapMode: Text.WordWrap
        }

        WebView {
            id: browser
            objectName: "wikiWebView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            onLoadingChanged: function(loadRequest) {
                if (loadRequest.status === WebView.LoadStartedStatus)
                    viewer.loadError = ""
                else if (loadRequest.status === WebView.LoadSucceededStatus)
                    viewer.configureReader()   // no user scripts on the native view: inject per load
                else if (loadRequest.status === WebView.LoadFailedStatus) {
                    viewer.loadError = loadRequest.errorString
                    console.warn("Wiki page failed:", loadRequest.url, loadRequest.errorString)
                }
            }
        }
    }
}
