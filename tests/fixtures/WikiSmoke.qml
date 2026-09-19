import QtQuick
import QtQuick.Controls
import "../../frontend" as App
import "../../frontend/settings" as Settings

ApplicationWindow {
    id: window
    width: 1100; height: 760; visible: true
    property int stage: 0
    property string debugMessage: ""
    property var popup
    property var browser
    property var viewer
    function find(item, name) {
        if (item.objectName === name) return item
        var children = item.data || item.children || []
        for (var i = 0; i < children.length; ++i) {
            var match = find(children[i], name)
            if (match) return match
        }
        return null
    }
    function check(ok, message) {
        if (!ok) { window.debugMessage = message; console.error("FAIL:", message); Qt.exit(1) }
        return ok
    }
    property bool sideNavigation: false
    property int navigationClicks: 0
    Item {
        id: pageArea
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.left: window.sideNavigation ? navigation.right : parent.left
        anchors.bottom: window.sideNavigation ? parent.bottom : navigation.top
        property var currentItem: null
        Item {
            id: settingsHost
            objectName: "settingsMenu"
            anchors.fill: parent
            property var activeWikiPopup: null
            function registerWikiPopup(candidate) { activeWikiPopup = candidate }

            Settings.AboutPage {
                id: about
                anchors.fill: parent
                stackView: pageArea
                visible: pageArea.currentItem === settingsHost
            }
        }
    }
    Button {
        id: navigation
        objectName: "appNavigationButton"
        text: "App navigation"
        x: 0; y: window.sideNavigation ? 0 : window.height - height
        width: window.sideNavigation ? 100 : window.width
        height: window.sideNavigation ? window.height : 80
        onClicked: { window.navigationClicks++; pageArea.currentItem = null }
    }
    Component.onCompleted: {
        App.Spacing.applicationWidth = 1100
        App.Spacing.applicationHeight = 760
    }
    Timer {
        interval: 100; repeat: true; running: true
        onTriggered: {
            if (stage === 0) {
                if (pageArea.currentItem !== settingsHost) {
                    pageArea.currentItem = settingsHost
                    return
                }
                var button = find(about, "openWikiButton")
                if (!check(button !== null, "About wiki button exists")) return
                button.clicked()
                popup = testProbe.find("wikiPopup")
                stage = 1
            } else if (stage === 1) {
                if (!popup || !popup.opened) return
                if (!check(!popup.modal && popup.parent === pageArea && popup.height === pageArea.height,
                           "reader fits inside page area and leaves navigation interactive")) return
                var loader = find(popup.contentItem, "wikiBrowserLoader")
                if (!wikiViewerAvailable) {
                    if (!check(loader && !loader.active && !loader.item, "unavailable build avoids WebEngine import")) return
                    popup.close()
                    stage = 11
                    return
                }
                if (!loader || !loader.item) return
                viewer = loader.item
                browser = find(viewer, "wikiWebView")
                if (!browser || browser.loading || !browser.title) return
                stage = -1
                browser.runJavaScript("document.title.includes('OCTAVE') && !!window.octaveWiki && parseFloat(getComputedStyle(document.body).fontSize) >= 20 && getComputedStyle(document.querySelector('#sidebar')).visibility === 'hidden' ", function(ok) {
                    if (!ok) { stage = 1; return }
                    for (var i = 0; i < 4; ++i)
                        find(viewer, "wikiSmallerTextButton").clicked()
                    stage = 19
                })
            } else if (stage === 19) {
                stage = -1
                browser.runJavaScript("parseFloat(getComputedStyle(document.body).fontSize)", function(fontSize) {
                    if (fontSize > 14.1) { stage = 19; return }
                    if (!check(fontSize >= 14, "reader text can be reduced to 14px")) return
                    for (var i = 0; i < 6; ++i)
                        find(viewer, "wikiLargerTextButton").clicked()
                    find(viewer, "wikiTopicsButton").clicked()
                    stage = 20
                })
            } else if (stage === 20) {
                stage = -1
                browser.runJavaScript("({font:parseFloat(getComputedStyle(document.body).fontSize),open:document.querySelector('#sidebar').classList.contains('open'),target:document.querySelector('.sidebar-link').getBoundingClientRect().height,styles:Array.from(document.styleSheets).map(s=>s.href)})", function(state) {
                    // QML property changes and the injected stylesheet both
                    // cross the WebEngine process boundary asynchronously.
                    if (state.font < 24 || !state.open || state.target < 52) {
                        debugMessage = JSON.stringify(state)
                        stage = 20
                        return
                    }
                    find(viewer, "wikiTopicsButton").clicked()
                    stage = 21
                })
            } else if (stage === 21) {
                stage = -1
                browser.runJavaScript("window.scrollTo(0,0); var p=document.querySelector('.content p'); p.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,pointerType:'mouse',button:0,pointerId:7,clientX:500,clientY:350})); document.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,cancelable:true,pointerType:'mouse',pointerId:7,clientX:500,clientY:200})); document.dispatchEvent(new PointerEvent('pointerup',{bubbles:true,pointerType:'mouse',pointerId:7})); window.scrollY > 0", function(ok) {
                    if (!check(ok, "mouse-emulated touchscreen drag scrolls the article")) return
                    find(viewer, "wikiSearchButton").clicked()
                    browser.runJavaScript("var input=document.querySelector('#search-input'); input.value='Spotify'; input.dispatchEvent(new Event('input'))")
                    stage = 2
                })
            } else if (stage === 2) {
                stage = -1
                browser.runJavaScript("document.querySelectorAll('.search-result-item').length", function(count) {
                    if (!count) { stage = 2; return }
                    browser.url = String(wikiHomeUrl).replace('index.html', 'getting-started.html')
                    stage = 3
                })
            } else if (stage === 3) {
                if (browser.loading || !String(browser.url).endsWith('getting-started.html')) return
                stage = -1
                browser.runJavaScript("!!window.octaveWiki && parseFloat(getComputedStyle(document.body).fontSize) >= 24", function(ok) {
                    stage = ok ? 31 : 3
                })
            } else if (stage === 31) {
                if (!check(browser.canGoBack, "history has a previous page")) return
                find(viewer, "wikiBackButton").clicked()
                stage = 4
            } else if (stage === 4) {
                if (browser.loading || String(browser.url) !== String(wikiHomeUrl)) return
                if (!check(browser.canGoForward, "forward navigation available")) return
                find(viewer, "wikiForwardButton").clicked()
                stage = 5
            } else if (stage === 5) {
                if (browser.loading || !String(browser.url).endsWith('getting-started.html')) return
                find(viewer, "wikiHomeButton").clicked()
                stage = 6
            } else if (stage === 6) {
                if (browser.loading || String(browser.url) !== String(wikiHomeUrl)) return
                browser.url = String(wikiHomeUrl).replace('index.html', 'missing-smoke-test.html')
                stage = 7
            } else if (stage === 7) {
                if (viewer.loadError === "") return
                find(viewer, "wikiHomeButton").clicked()
                stage = 8
            } else if (stage === 8) {
                if (browser.loading || String(browser.url) !== String(wikiHomeUrl) || viewer.loadError !== "") return
                find(popup.contentItem, "closeWikiButton").clicked()
                stage = 9
            } else if (stage === 9) {
                var closedLoader = find(popup.contentItem, "wikiBrowserLoader")
                if (popup.opened) return
                if (!check(!closedLoader.item, "Close releases browser")) return
                find(about, "openWikiButton").clicked()
                stage = 10
            } else if (stage === 12) {
                if (!popup.opened || !popup.suspended) return
                if (!check(navigationClicks === 1, "bottom navigation receives click while reader is open")) return
                var suspendedLoader = find(popup.contentItem, "wikiBrowserLoader")
                if (!check(suspendedLoader.item === viewer, "navigation keeps the browser alive in the background")) return
                pageArea.currentItem = settingsHost
                stage = 121
            } else if (stage === 121) {
                if (popup.suspended) return
                stage = -1
                browser.runJavaScript("window.scrollY", function(scrollY) {
                    if (!check(scrollY >= 200, "returning to Settings restores the wiki scroll position")) return
                    // Mirrors pressing Settings again while the wiki is visible.
                    popup.close()
                    stage = 122
                })
            } else if (stage === 122) {
                if (popup.opened) return
                if (!check(pageArea.currentItem === settingsHost && about.visible,
                           "closing the reader leaves About visible")) return
                window.sideNavigation = true
                find(about, "openWikiButton").clicked()
                stage = 13
            } else if (stage === 13) {
                if (!popup.opened) return
                if (!check(popup.width === pageArea.width && popup.height === pageArea.height,
                           "reader follows side navigation geometry")) return
                testProbe.click(navigation.x + navigation.width / 2, navigation.y + navigation.height / 2)
                stage = 14
            } else if (stage === 14) {
                if (!popup.opened || !popup.suspended) return
                if (!check(navigationClicks === 2, "side navigation receives click")) return
                var sideLoader = find(popup.contentItem, "wikiBrowserLoader")
                if (!check(sideLoader.item, "side navigation also preserves the browser")) return
                popup.close()
                testProbe.passTest()
                Qt.exit(0)
            } else if (stage === 11) {
                if (popup.opened) return
                testProbe.passTest()
                Qt.exit(0)
            } else if (stage === 10) {
                var reopened = find(popup.contentItem, "wikiBrowserLoader")
                if (!reopened.item) return
                var fresh = find(reopened.item, "wikiWebView")
                if (fresh.loading || !fresh.title) return
                if (!check(String(fresh.url) === String(wikiHomeUrl), "reopen starts at wiki home")) return
                viewer = reopened.item
                browser = fresh
                stage = -1
                browser.runJavaScript("window.scrollTo(0, 240); window.scrollY", function(scrollY) {
                    if (scrollY < 200) { stage = 10; return }
                    testProbe.click(navigation.x + navigation.width / 2, navigation.y + navigation.height / 2)
                    stage = 12
                })
            }
        }
    }
    Timer { interval: 30000; running: true; onTriggered: { console.error("FAIL: wiki smoke timeout at stage", stage, debugMessage, "readerScale", viewer ? viewer.readerScale : -1); Qt.exit(2) } }
}
