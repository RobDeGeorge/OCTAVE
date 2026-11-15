// ValueGaugeBase.qml
//
// What every PID-bound value gauge shares (Circular, Arc, Bar, Segmented,
// Linear, DigitalReadout, Sparkline): the paramId binding, the metadata that
// defaults from the PID, the reading, and the alert rule with its colours
// and flash. A gauge is `ValueGaugeBase { id: root ... }` and draws; all of
// these stay its own public properties, overridable per widget.
//
// See docs/GAUGE_AUTHORING.md, "Shared binding API" and "Alerts".

import QtQuick 2.15
import ".." as App
import "AlertLevel.js" as Alerts

Item {
    id: base

    // ── Data binding ────────────────────────────────────────────────
    property string paramId: ""

    // The PID's row in the gauges' units, null without one
    readonly property var _pInfo: App.OBDParameterModel.displayInfo(paramId)

    property string title: _pInfo ? _pInfo.title : ""
    property string unit: _pInfo ? _pInfo.unit : ""
    property real min: _pInfo ? _pInfo.min : 0
    property real max: _pInfo ? _pInfo.max : 100
    // NaN = no reading yet: "--", nothing drawn, no alert colour
    property real value: App.OBDParameterModel.sample(paramId)
    readonly property bool hasValue: isFinite(value)
    property int decimals: 0

    // ── Alerts (gauges/AlertLevel.js) ───────────────────────────────
    // Limits in the display unit, NaN = unset. Level 1 colours the value
    // warnColor, level 2 critColor; alertFlash blinks it while critical.
    property real warnAbove: NaN
    property real warnBelow: NaN
    property real critAbove: NaN
    property real critBelow: NaN
    property bool alertFlash: false
    property color warnColor: App.Style.statusWarning
    property color critColor: App.Style.statusDanger
    // How far back inside a limit the value must come before the level
    // drops (display unit). NaN = 1 % of the range, at least one step of
    // the readout; 0 = no hold.
    property real alertHysteresis: NaN
    readonly property real _alertMargin: Alerts.margin(min, max, decimals, alertHysteresis)
    // The level shown last, for the hold. A plain object written inside the
    // alertLevel binding: it does not notify, so there is no binding loop.
    property var _alertMemo: ({ "level": 0, "key": "" })
    readonly property int alertLevel:
        Alerts.track(_alertMemo, value, warnAbove, warnBelow, critAbove, critBelow, _alertMargin)
    readonly property color _alertColor: alertLevel === 2 ? critColor : warnColor
    property bool _flashOn: true
    readonly property real _alertOpacity: (alertFlash && alertLevel === 2 && !_flashOn) ? 0.3 : 1.0
    Timer {
        // Not while hidden (another page, a dashboard swiped away)
        running: base.alertFlash && base.alertLevel === 2 && base.visible
        interval: 400
        repeat: true
        onTriggered: base._flashOn = !base._flashOn
        onRunningChanged: if (!running) base._flashOn = true
    }
}
