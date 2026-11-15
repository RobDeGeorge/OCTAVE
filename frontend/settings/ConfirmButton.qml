import QtQuick 2.15
import ".." as App

// A SettingsButton for an action that cannot be taken back (forget a device,
// revoke, remove, sign out): the first tap arms it, and it reads
// `confirmLabel` in the theme's danger colour; a second tap within `armMs`
// emits confirmed(). Left alone, hidden or disabled, it disarms.
//
//   ConfirmButton {
//       label: "Forget"; confirmLabel: "Confirm forget"
//       Layout.preferredHeight: dp(48)        // as SettingsButton: no height of its own
//       onConfirmed: phoneMirrorManager.forgetWireless()
//   }
//
// No dialog on purpose: nothing new to aim at in a moving car, and the same
// two taps in the same place as Shut Down, Update Now and Clear history.
SettingsButton {
    id: control

    property string label: ""
    property string confirmLabel: "Confirm"
    property color idleColor: App.Style.accent
    property int armMs: 4000
    property bool armed: false

    signal confirmed()

    text: armed ? confirmLabel : label
    buttonColor: armed ? App.Style.statusDanger : idleColor

    onClicked: {
        if (!enabled)
            return
        if (armed) {
            armed = false
            armTimer.stop()
            control.confirmed()
        } else {
            armed = true
            armTimer.restart()
        }
    }

    // Anything but the second tap takes it back
    onVisibleChanged: if (!visible) armed = false
    onEnabledChanged: if (!enabled) armed = false
    onLabelChanged: armed = false

    Timer {
        id: armTimer
        interval: control.armMs
        onTriggered: control.armed = false
    }
}
