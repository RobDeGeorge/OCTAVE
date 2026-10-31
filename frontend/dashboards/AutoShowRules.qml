// AutoShowRules.qml
//
// "Show this dashboard when ..." (a spec's showWhen {paramId, above,
// below}, limits in the PID's own unit). OBDMenu feeds it the dashboards
// with a rule and the one showing, and calls check() twice a second while
// its page is on screen with a car connected:
//
//   - a rule that starts to hold brings its dashboard up (switchTo), the
//     first in `rules` order winning;
//   - once the rule has stopped holding for `clearChecks` checks in a row
//     (5 s), the dashboard that was showing comes back (switchTo);
//   - if the user picks another dashboard meanwhile, nothing comes back;
//   - a rule fires once per crossing: swiped away while it still holds, it
//     stays away until it has stopped holding and starts again.

import QtQml 2.15
import ".." as App

QtObject {
    id: auto

    property var rules: []           // [{id, showWhen}]
    property string activeId: ""
    property int clearChecks: 10

    signal switchTo(string id)

    property string shownId: ""      // brought up by a rule, still showing
    property string _returnId: ""
    property var _holding: ({})      // dashboard id -> true while its rule holds
    property int _clearCount: 0

    function holds(when) {
        if (!when || !when.paramId) return false
        var v = App.OBDParameterModel.rawValue(when.paramId)
        if (typeof v !== "number" || !isFinite(v)) return false
        return (typeof when.above === "number" && v >= when.above)
            || (typeof when.below === "number" && v <= when.below)
    }

    function check() {
        var holding = {}
        var fire = ""
        for (var i = 0; i < rules.length; i++) {
            var r = rules[i]
            if (!holds(r.showWhen)) continue
            holding[r.id] = true
            if (!_holding[r.id] && fire === "") fire = r.id
        }
        _holding = holding
        if (fire !== "" && fire !== activeId) {
            if (shownId === "") _returnId = activeId
            shownId = fire
            _clearCount = 0
            switchTo(fire)
            return
        }
        if (shownId === "") return
        if (activeId !== shownId) {          // the user moved on
            shownId = ""
            _returnId = ""
        } else if (holding[shownId]) {
            _clearCount = 0
        } else if (++_clearCount >= clearChecks) {
            var back = _returnId
            shownId = ""
            _returnId = ""
            _clearCount = 0
            if (back !== "") switchTo(back)
        }
    }
}
