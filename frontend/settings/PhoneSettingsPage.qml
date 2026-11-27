import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as App

// Settings > Phone: the three ways a phone reaches the screen, a tile each.
// Phone Mirror (Android over adb), Android Auto, and an iPhone over AirPlay
// (its tile only where the build has the receiver).
//
// Every card follows the same rules: the state of the thing first, one
// setting per SettingCategory with its switch in the title row, and a
// description only where the title does not say it. The how-to lives in the
// wiki and in the pairing screens, not here.
Flickable {
    id: pageRoot

    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    property var stackView: null
    property var mainWindow: null
    property string currentSection: ""

    // False when the host shows tileModel as a tile grid instead of this
    // page (the Sidebar layout); the stacked cards are then never built.
    property bool renderCards: true

    readonly property var _pm: typeof phoneMirrorManager !== "undefined" ? phoneMirrorManager : null
    readonly property var _aa: typeof androidAutoManager !== "undefined" ? androidAutoManager : null
    readonly property var _ap: typeof airPlayManager !== "undefined" ? airPlayManager : null

    // Tile model — consumed by SettingsSidebarLayout (grid + popup).
    property var tileModel: {
        var tiles = [
            { cardId: "phone_mirror", title: "Phone Mirror", component: phoneMirrorContent,
              summary: function() {
                  // Always on where it can run (no on / off switch since
                  // 2026-11-25: Home's phone window is the way to it)
                  if (!pageRoot._pm || !pageRoot._pm.nativeAvailable) return { state: "Off" }
                  var running = !!(pageRoot._pm && pageRoot._pm.isRunning)
                  return { state: running ? (pageRoot._pm.connectionType === "wifi" ? "Wi-Fi" : "USB") : "Waiting",
                           active: running,
                           status: running ? App.Style.statusConnected : App.Style.statusWarning }
              } },
            { cardId: "phone_android_auto", title: "Android Auto", component: androidAutoContent,
              summary: function() {
                  if (!settingsManager || !settingsManager.androidAutoEnabled) return { state: "Off" }
                  var aa = pageRoot._aa
                  if (!aa || aa.backend !== "native") return { state: "On" }
                  var st = aa.nativeState
                  if (st === "running")
                      return { state: "Connected", active: true, status: App.Style.statusConnected }
                  if (st === "error" || st === "needsCertificate")
                      return { state: st === "error" ? "Error" : "Needs certificate", status: App.Style.statusError }
                  return { state: "Waiting", status: App.Style.statusWarning }
              } }
        ]
        if (pageRoot._ap && pageRoot._ap.available) {
            tiles.push({ cardId: "phone_airplay", title: "iPhone", component: airPlayContent,
              summary: function() {
                  var ap = pageRoot._ap
                  if (!ap || !ap.enabled) return { state: "Off" }
                  var playing = ap.state === "mirroring" || ap.state === "audio"
                  return { state: ap.state === "mirroring" ? "Mirroring" : ap.state === "audio" ? "Audio"
                                : ap.state === "error" ? "Error" : "Waiting",
                           active: playing,
                           status: playing ? App.Style.statusConnected
                                 : ap.state === "error" ? App.Style.statusError : App.Style.statusWarning }
              } })
        }
        return tiles
    }

    contentWidth: width
    contentHeight: settingsContent.implicitHeight
    flickableDirection: Flickable.VerticalFlick
    clip: true
    boundsBehavior: Flickable.DragAndOvershootBounds
    flickDeceleration: 1200
    maximumFlickVelocity: 4000
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

    // The state of a connection, first thing in its card
    component StatusLine: Text {
        Layout.fillWidth: true
        color: App.Style.secondaryTextColor
        font.pixelSize: App.Spacing.overallText
        font.family: App.Style.fontFamily
        font.bold: true
        wrapMode: Text.WordWrap
    }

    // ─────────────────────────────────────────── Phone Mirror
    Component {
        id: phoneMirrorContent
        ColumnLayout {
            id: mirrorColumn
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            readonly property var pm: pageRoot._pm
            readonly property bool ready: !!pm && pm.nativeAvailable
            readonly property bool desktopMode: settingsManager ? settingsManager.scrcpyDisplaySize !== "" : false

            StatusLine {
                readonly property bool running: !!mirrorColumn.pm && mirrorColumn.pm.isRunning
                text: !mirrorColumn.pm ? "Phone mirroring is not available"
                    : !mirrorColumn.ready ? mirrorColumn.pm.getInstallInstructions()
                    : running ? "Mirroring over " + (mirrorColumn.pm.connectionType === "wifi" ? "Wi-Fi" : "USB")
                    : "Ready. Plug in a phone with USB debugging on, or pair it below."
                color: !mirrorColumn.ready ? App.Style.statusError
                     : running ? App.Style.statusConnected : App.Style.secondaryTextColor
            }

            // Wi-Fi: QR pairing (Android 11+ Wireless debugging). The manager
            // remembers the phone and reconnects to it on its own.
            SettingCategory {
                id: wirelessSection
                title: "Wireless"
                visible: mirrorColumn.ready
                description: remembered !== ""
                    ? "Remembered: " + remembered + ". Reconnects on the same Wi-Fi; a USB cable takes over while plugged in."
                    : "Pair over Wi-Fi with a QR code (Android 11+)."

                readonly property string remembered: pageRoot._pm ? pageRoot._pm.wirelessAddress : ""
                property string resultText: ""
                property bool resultOk: true
                // This page started the pairing now on screen. The page is
                // pre-built and stays alive hidden, so without this its modal
                // also popped up over Phone Mirror's own QR screen.
                property bool pairingFromHere: false

                Connections {
                    target: pageRoot._pm
                    function onWirelessResult(ok, message) {
                        wirelessSection.resultOk = ok
                        wirelessSection.resultText = message
                        wirelessSection.pairingFromHere = false
                        qrPopup.sync()
                    }
                }
                Connections {
                    target: pageRoot._pm
                    ignoreUnknownSignals: true   // the mobile stand-in has no QR pairing
                    function onQrPairingChanged() {
                        if (!pageRoot._pm.qrPairingActive)
                            wirelessSection.pairingFromHere = false   // cancelled, timed out or done
                        qrPopup.sync()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    SettingsButton {
                        objectName: "phoneMirrorQrButton"
                        text: wirelessSection.remembered !== "" ? "Pair a Different Phone" : "Pair a Phone"
                        Layout.preferredHeight: pageRoot.dp(56)
                        enabled: pageRoot._pm ? !pageRoot._pm.wirelessBusy : false
                        opacity: enabled ? 1.0 : 0.6
                        onClicked: {
                            wirelessSection.resultText = ""
                            wirelessSection.pairingFromHere = true
                            qrPanel.start()
                            qrPopup.sync()
                        }
                    }

                    Item { Layout.fillWidth: true }

                    ConfirmButton {
                        label: "Forget"
                        confirmLabel: "Confirm forget"
                        idleColor: App.Style.statusError
                        Layout.preferredHeight: pageRoot.dp(56)
                        visible: wirelessSection.remembered !== ""
                        onConfirmed: {
                            wirelessSection.resultOk = true
                            wirelessSection.resultText = "Forgot " + wirelessSection.remembered
                                + ". To revoke it completely, remove OCTAVE from the phone's paired devices."
                            pageRoot._pm.forgetWireless()
                        }
                    }
                }

                SettingDescription {
                    visible: wirelessSection.resultText !== ""
                    text: wirelessSection.resultText
                    color: wirelessSection.resultOk ? App.Style.secondaryTextColor : App.Style.statusError
                }

                // The code in a centred modal (the card may be scrolled anywhere)
                Popup {
                    id: qrPopup
                    parent: Overlay.overlay
                    anchors.centerIn: parent
                    modal: true
                    dim: true
                    closePolicy: Popup.NoAutoClose
                    padding: App.Spacing.overallSpacing * 1.5
                    // Opened and closed by sync(), not bound to `visible`: the
                    // binding's onClosed cleared pairingFromHere, which the
                    // binding was reading at that moment ("Binding loop
                    // detected for property visible" at every Cancel)
                    function sync() {
                        if (wirelessSection.pairingFromHere
                                && (pageRoot._pm ? pageRoot._pm.qrPairingActive : false))
                            open()
                        else
                            close()
                    }
                    width: parent ? parent.width * 0.75 : pageRoot.dp(900)

                    background: Rectangle {
                        color: App.Style.contentColor
                        radius: pageRoot.dpMin(12, 2)
                        border.color: App.Style.accent
                        border.width: 1
                    }

                    contentItem: App.PhoneQrPairingPanel {
                        id: qrPanel
                        qrSize: qrPopup.parent ? Math.round(qrPopup.parent.height * 0.55) : pageRoot.dp(380)
                    }
                }
            }

            // The network the head unit hosts (the reference box: hostapd on
            // ap0). At a cable session the manager puts the phone on it from
            // the phone's own shell, so it is never picked on the phone.
            SettingCategory {
                title: "Car Wi-Fi"
                visible: mirrorColumn.ready
                description: "The Wi-Fi this head unit hosts. A phone on a cable joins it by itself, then stays connected without the cable."

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.rowSpacing

                    SettingsTextField {
                        objectName: "carWifiNameField"
                        Layout.fillWidth: true
                        text: settingsManager ? settingsManager.get_setting_with_default("carWifiName", "") : ""
                        placeholderText: "Network name"
                        onEditingFinished: if (settingsManager) settingsManager.save_setting("carWifiName", text.trim())
                    }
                    SettingsTextField {
                        objectName: "carWifiPasswordField"
                        Layout.fillWidth: true
                        text: settingsManager ? settingsManager.get_setting_with_default("carWifiPassword", "") : ""
                        placeholderText: "Password"
                        echoMode: TextInput.PasswordEchoOnEdit
                        onEditingFinished: if (settingsManager) settingsManager.save_setting("carWifiPassword", text)
                    }
                }
            }

            SettingCategory {
                title: "Display Mode"
                description: mirrorColumn.desktopMode
                    ? "A separate desktop the phone draws for OCTAVE (Android 11+; DeX on Samsung)."
                    : ""

                SettingsSegmentedControl {
                    id: scrcpyModeControl
                    Layout.fillWidth: true
                    options: ["Mirror Phone", "Desktop (DeX)"]
                    currentValue: settingsManager && settingsManager.scrcpyDisplaySize === "" ? "Mirror Phone" : "Desktop (DeX)"

                    onSelected: function(value) {
                        if (!settingsManager || value === currentValue) return
                        var size = value === "Mirror Phone" ? "" : "auto"
                        settingsManager.save_scrcpy_display_size(size)
                        if (pageRoot._pm) pageRoot._pm.setDisplaySize(size)
                    }
                }

                // The rest only applies to the desktop display (always the
                // size of the window: no Fit to Screen switch since 2026-11-25)
                SettingLabel {
                    text: "Text Size"
                    visible: scrcpyModeControl.currentValue !== "Mirror Phone"
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing
                    visible: scrcpyModeControl.currentValue !== "Mirror Phone"

                    // 0 = Auto, otherwise dpi in steps of 20
                    SettingsSlider {
                        id: scrcpyDpiSlider
                        from: 100
                        to: 400
                        stepSize: 20
                        value: settingsManager && settingsManager.scrcpyDisplayDpi > 0 ? settingsManager.scrcpyDisplayDpi : 100
                        Layout.fillWidth: true
                        onPressedChanged: {
                            if (!pressed && settingsManager)
                                settingsManager.save_scrcpy_display_dpi(value <= 100 ? 0 : value)
                        }
                        Connections {
                            target: settingsManager
                            function onScrcpyDisplayDpiChanged() {
                                scrcpyDpiSlider.value = settingsManager.scrcpyDisplayDpi > 0 ? settingsManager.scrcpyDisplayDpi : 100
                            }
                        }
                    }

                    ValueDisplay {
                        text: scrcpyDpiSlider.value <= 100 ? "Auto" : scrcpyDpiSlider.value.toFixed(0) + " dpi"
                        Layout.fillWidth: false
                    }
                }
            }

            // No Picture Quality settings since 2026-11-25: the bit rate adapts on its own
            // (PhoneMirrorManager kUsbMaxBps ...), so there is nothing to tune

            // adb over Wi-Fi, switched on at every USB session. The warning
            // stays in the description: until the phone restarts it accepts
            // adb from any network it joins.
            SettingCategory {
                title: "Stay Wireless After USB"
                description: "After a USB session the phone connects without a cable until it restarts. "
                             + "It then accepts connections on any network it joins, so use it only on networks you trust."

                inlineContent: SettingsToggle {
                    id: scrcpyAutoWifiToggle
                    compact: true
                    checked: settingsManager ? settingsManager.scrcpyAutoWifi : false
                    onToggled: function(checked) {
                        if (settingsManager) settingsManager.save_scrcpy_auto_wifi(checked)
                    }
                }
            }

            SettingCategory {
                title: "Phone Screen Off While Mirroring"
                description: "Some phones only dim."

                inlineContent: SettingsToggle {
                    id: scrcpyPhoneScreenOffToggle
                    compact: true
                    checked: settingsManager ? settingsManager.scrcpyPhoneScreenOff : false
                    onToggled: function(checked) {
                        if (settingsManager) settingsManager.save_scrcpy_phone_screen_off(checked)
                    }
                }
            }

            SettingCategory {
                title: "Phone Audio Level"
                description: "Boost or trim the phone's sound before OCTAVE's volume."

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing

                    SettingsSlider {
                        id: scrcpyAudioGainSlider
                        from: -12
                        to: 18
                        stepSize: 1
                        value: settingsManager ? Math.round(settingsManager.scrcpyAudioGainDb) : 6
                        Layout.fillWidth: true
                        onPressedChanged: {
                            if (!pressed && settingsManager)
                                settingsManager.save_scrcpy_audio_gain_db(value)
                        }
                        Connections {
                            target: settingsManager
                            function onScrcpyAudioGainChanged() {
                                scrcpyAudioGainSlider.value = Math.round(settingsManager.scrcpyAudioGainDb)
                            }
                        }
                    }

                    ValueDisplay {
                        text: (scrcpyAudioGainSlider.value >= 0 ? "+" : "") + scrcpyAudioGainSlider.value.toFixed(0) + " dB"
                        Layout.fillWidth: false
                    }
                }
            }

            SettingCategory {
                title: "Duck Local Music"
                description: "Turns OCTAVE's music down while the phone makes sound."

                inlineContent: SettingsToggle {
                    id: scrcpyAudioDuckEnabledToggle
                    compact: true
                    checked: settingsManager ? settingsManager.scrcpyAudioDuckEnabled : true
                    onToggled: function(checked) {
                        if (settingsManager) settingsManager.save_scrcpy_audio_duck_enabled(checked)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: App.Spacing.overallSpacing
                    visible: scrcpyAudioDuckEnabledToggle.checked

                    SettingsSlider {
                        id: scrcpyAudioDuckLevelSlider
                        from: -40
                        to: -6
                        stepSize: 1
                        value: settingsManager ? Math.round(settingsManager.scrcpyAudioDuckLevelDb) : -20
                        Layout.fillWidth: true
                        onPressedChanged: {
                            if (!pressed && settingsManager)
                                settingsManager.save_scrcpy_audio_duck_level_db(value)
                        }
                        Connections {
                            target: settingsManager
                            function onScrcpyAudioDuckLevelChanged() {
                                scrcpyAudioDuckLevelSlider.value = Math.round(settingsManager.scrcpyAudioDuckLevelDb)
                            }
                        }
                    }

                    ValueDisplay {
                        text: scrcpyAudioDuckLevelSlider.value.toFixed(0) + " dB"
                        Layout.fillWidth: false
                    }
                }
            }
        }
    }

    // ─────────────────────────────────────────── Android Auto
    Component {
        id: androidAutoContent
        ColumnLayout {
            id: androidAutoColumn
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            readonly property var aa: pageRoot._aa
            readonly property bool builtIn: !!aa && aa.backend === "native"
            readonly property string st: aa ? aa.nativeState : "idle"

            StatusLine {
                visible: androidAutoColumn.builtIn
                text: androidAutoColumn.st === "idle"
                      ? "Not connected. Open Android Auto from the nav bar."
                      : (androidAutoColumn.aa ? androidAutoColumn.aa.nativeStatus : "")
                color: androidAutoColumn.st === "running" ? App.Style.statusConnected
                     : (androidAutoColumn.st === "error" || androidAutoColumn.st === "needsCertificate") ? App.Style.statusError
                     : App.Style.secondaryTextColor
            }

            // What it takes, until it is running; the steps are in the wiki
            SettingDescription {
                visible: androidAutoColumn.builtIn && androidAutoColumn.st !== "running"
                text: "Needs Android Auto's developer mode and head unit server on the phone, and a head-unit "
                      + "certificate here. Steps: the Android Auto page in the wiki."
            }
            SettingDescription {
                visible: androidAutoColumn.builtIn
                         && (androidAutoColumn.st === "needsCertificate" || androidAutoColumn.st === "error")
                text: "Certificate folder: " + (androidAutoColumn.aa ? androidAutoColumn.aa.certificateDir : "")
            }

            SettingCategory {
                title: "Show in Nav Bar"

                inlineContent: SettingsToggle {
                    id: androidAutoEnabledToggle
                    compact: true
                    checked: settingsManager ? settingsManager.androidAutoEnabled : false
                    onToggled: function(checked) {
                        if (settingsManager) settingsManager.save_android_auto_enabled(checked)
                    }
                }
            }

            SettingCategory {
                title: "Head Unit"
                description: "Built-in is OCTAVE's own (beta). Google DHU runs Google's Desktop Head Unit from the Android SDK."
                visible: androidAutoColumn.aa ? androidAutoColumn.aa.nativeAvailable : false

                SettingsSegmentedControl {
                    Layout.fillWidth: true
                    options: ["Built-in", "Google DHU"]
                    currentValue: androidAutoColumn.builtIn ? "Built-in" : "Google DHU"
                    onSelected: function(value) {
                        if (settingsManager)
                            settingsManager.save_setting("androidAutoBackend", value === "Built-in" ? "native" : "dhu")
                    }
                }
            }

            SettingCategory {
                id: androidAutoResolution
                title: "Video Size"
                description: "Applies at the next connection."
                visible: androidAutoColumn.builtIn
                property string value: settingsManager
                    ? settingsManager.get_setting_with_default("androidAutoResolution", "1280x720") : "1280x720"

                SettingsSegmentedControl {
                    Layout.fillWidth: true
                    options: ["800x480", "1280x720", "1920x1080"]
                    currentValue: androidAutoResolution.value
                    onSelected: function(value) {
                        if (!settingsManager) return
                        settingsManager.save_setting("androidAutoResolution", value)
                        androidAutoResolution.value = value
                    }
                }
            }
        }
    }

    // ─────────────────────────────────────────── iPhone (AirPlay)
    Component {
        id: airPlayContent
        ColumnLayout {
            id: airPlayColumn
            width: parent ? parent.width : 0
            spacing: App.Spacing.sectionSpacing

            readonly property var ap: pageRoot._ap
            readonly property bool on: !!ap && ap.enabled

            StatusLine {
                visible: airPlayColumn.on
                text: airPlayColumn.ap ? airPlayColumn.ap.status : ""
                color: airPlayColumn.ap && airPlayColumn.ap.state === "error" ? App.Style.statusError
                     : airPlayColumn.ap && (airPlayColumn.ap.state === "mirroring" || airPlayColumn.ap.state === "audio")
                       ? App.Style.statusConnected : App.Style.secondaryTextColor
            }

            SettingCategory {
                title: "AirPlay Receiver"
                description: "On the iPhone, on the same Wi-Fi: Control Center > Screen Mirroring > "
                             + (airPlayColumn.ap ? airPlayColumn.ap.receiverName : "OCTAVE") + "."

                inlineContent: SettingsToggle {
                    objectName: "airPlayEnabledToggle"
                    compact: true
                    checked: airPlayColumn.on
                    onToggled: function(checked) { if (airPlayColumn.ap) airPlayColumn.ap.setEnabled(checked) }
                }
            }

            SettingCategory {
                title: "Ask New iPhones for a PIN"
                visible: airPlayColumn.on

                inlineContent: SettingsToggle {
                    compact: true
                    checked: settingsManager ? settingsManager.get_setting_with_default("airPlayRequirePin", true) : true
                    onToggled: function(checked) { if (settingsManager) settingsManager.save_setting("airPlayRequirePin", checked) }
                }
            }

            // OCTAVE as a Bluetooth mouse for the mirrored iPhone
            SettingCategory {
                title: "Touch Through Bluetooth"
                description: "Pair once, and turn on AssistiveTouch on the iPhone."
                visible: airPlayColumn.on && airPlayColumn.ap.touchAvailable

                inlineContent: SettingsToggle {
                    compact: true
                    checked: airPlayColumn.ap ? airPlayColumn.ap.touchEnabled : true
                    onToggled: function(checked) { if (airPlayColumn.ap) airPlayColumn.ap.setTouchEnabled(checked) }
                }

                SettingDescription {
                    visible: airPlayColumn.ap ? airPlayColumn.ap.touchEnabled : false
                    text: airPlayColumn.ap ? airPlayColumn.ap.touchStatus : ""
                }
                SettingsButton {
                    visible: airPlayColumn.ap ? airPlayColumn.ap.touchEnabled && airPlayColumn.ap.touchState === "waiting" : false
                    text: airPlayColumn.ap && airPlayColumn.ap.touchPairing ? "Stop Pairing" : "Pair Touch"
                    Layout.preferredHeight: pageRoot.dp(48)
                    onClicked: airPlayColumn.ap.pairTouch(!airPlayColumn.ap.touchPairing)
                }
            }

            SettingCategory {
                title: "Name"
                visible: airPlayColumn.on

                SettingsTextField {
                    Layout.fillWidth: true
                    text: airPlayColumn.ap ? airPlayColumn.ap.receiverName : "OCTAVE"
                    placeholderText: "OCTAVE"
                    onEditingFinished: if (settingsManager) settingsManager.save_setting("airPlayName", text.trim())
                }
            }

            SettingCategory {
                id: airPlayResolution
                title: "Video Size"
                description: "Larger is sharper and uses more CPU."
                visible: airPlayColumn.on
                property string value: settingsManager
                    ? settingsManager.get_setting_with_default("airPlayResolution", "1280x720") : "1280x720"

                SettingsSegmentedControl {
                    Layout.fillWidth: true
                    options: ["1280x720", "1920x1080"]
                    currentValue: airPlayResolution.value
                    onSelected: function(value) {
                        if (!settingsManager) return
                        settingsManager.save_setting("airPlayResolution", value)
                        airPlayResolution.value = value
                    }
                }
            }

            ConfirmButton {
                visible: airPlayColumn.on
                label: "Forget Paired iPhones"
                confirmLabel: "Confirm: forget all"
                Layout.preferredHeight: pageRoot.dp(48)
                onConfirmed: if (airPlayColumn.ap) airPlayColumn.ap.forgetDevices()
            }
        }
    }

    // ── Stacked SettingsCards, for a host that leaves renderCards on (none does now).
    ColumnLayout {
        id: settingsContent
        width: parent.width
        spacing: App.Spacing.sectionSpacing

        Repeater {
            model: pageRoot.renderCards ? pageRoot.tileModel : []

            SettingsCard {
                Layout.fillWidth: true
                objectName: modelData.title
                cardId: modelData.cardId
                title: modelData.title

                Loader {
                    Layout.fillWidth: true
                    sourceComponent: modelData.component
                }
            }
        }

        Item {
            Layout.fillHeight: true
            Layout.minimumHeight: App.Spacing.bottomBarHeight
        }
    }
}
