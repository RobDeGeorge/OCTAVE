import QtQuick
import QtQuick.Window
import "../../frontend/obd" as Obd

// The shift light editor against the real settingsManager and
// shiftLightManager (qmlfixturehost): opens, takes a tap on the enable row
// and on Add Flag, and passes every edit on to the manager.
Window {
    id: window
    visible: true
    width: 1000; height: 900
    property int phase: 0

    Obd.ShiftLightEditor { id: editor }

    function check(condition, message) {
        if (!condition) { console.error("SHIFT_LIGHT_EDITOR_FAIL: " + message); Qt.exit(1) }
    }
    function find(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        var kids = item.children || []
        for (var i = 0; i < kids.length; i++) {
            var hit = find(kids[i], name)
            if (hit) return hit
        }
        return null
    }
    function tap(name) {
        var item = find(editor.contentItem, name)
        check(item !== null, "no item named " + name)
        var p = item.mapToItem(null, item.width / 2, item.height / 2)
        testProbe.click(p.x, p.y)
    }

    Timer {
        interval: 150; running: true; repeat: true
        onTriggered: {
            if (window.phase === 0) {
                editor.open()
                window.phase = 1
            } else if (window.phase === 1) {
                if (!editor.opened) return
                window.check(editor.shiftLightEnabled === true && editor.maxRpm === 8000, "defaults on open")
                window.check(editor.flags.length === 0 && editor.selectedFlagIndex === -1, "no flags at first")
                window.check(!window.find(editor.contentItem, "shiftLightFlagSettings").visible, "no flag panel without a flag")
                window.tap("shiftLightEnabledRow")
                window.phase = 2
            } else if (window.phase === 2) {
                window.check(editor.shiftLightEnabled === false, "the row's tap turns the light off")
                window.check(settingsManager.get_setting_with_default("rpm_shift_light_enabled", true) === false, "and saves it")
                window.check(shiftLightManager.enabled === false, "and the manager follows")
                window.tap("shiftLightAddFlag")
                window.phase = 3
            } else if (window.phase === 3) {
                window.check(editor.flags.length === 1 && editor.selectedFlagIndex === 0, "Add Flag adds and selects")
                window.check(shiftLightManager.flags.length === 1, "the manager has the flag")
                window.check(editor.flags[0].rpmLow === 4800 && editor.flags[0].rpmHigh === 6800, "60 % to 85 % of the redline")
                var panel = window.find(editor.contentItem, "shiftLightFlagSettings")
                window.check(panel.visible && panel.flagNumber === 1, "the flag panel shows flag 1")
                // What the panel's controls send
                panel.edited("flash", true)
                panel.edited("rpmLow", 7000)
                panel.edited("color", "#0088FF")
                var f = shiftLightManager.flags[0]
                window.check(f.flash === true && f.color === "#0088FF", "edits reach the manager")
                window.check(f.rpmLow === 7000 && f.rpmHigh === 7000, "low past high pulls high up")
                // A second flag below the first sorts in front and stays selected
                editor.addFlag()
                window.check(editor.flags.length === 2 && editor.selectedFlagIndex === 0, "the new, lower flag is first and selected")
                // A lower redline pulls both ends back
                editor.setMaxRpm(6000)
                window.check(shiftLightManager.maxRpm === 6000, "the redline is saved")
                window.check(shiftLightManager.flags[1].rpmHigh === 6000 && shiftLightManager.flags[1].rpmLow === 6000, "flags are clamped to it")
                panel.deleteRequested()
                window.check(shiftLightManager.flags.length === 1 && editor.selectedFlagIndex === 0, "delete removes the selected flag")
                editor.close()
                window.phase = 4
            } else {
                console.log("SHIFT_LIGHT_EDITOR_PASS")
                Qt.exit(0)
            }
        }
    }
}
