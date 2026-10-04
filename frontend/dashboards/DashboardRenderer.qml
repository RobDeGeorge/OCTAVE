// DashboardRenderer.qml
//
// Renders a dashboard spec (JS object, typically parsed from JSON) into a
// live grid of widget instances. This is the Phase 2 hinge that lets
// dashboards live as data files instead of hand-written QML.
//
// Uses manual absolute positioning rather than GridLayout. GridLayout's
// fillHeight + rowSpan interaction doesn't give proportional sizing (a
// cell with rowSpan=4 ends up the same height as a rowSpan=1 peer), and
// we want deterministic grid cells anyway — that's the snap-to-grid model
// the eventual editor (Phase 3) will work against.
//
// Spec format (schema 1):
// {
//   "id":          "sport",
//   "label":       "Sport",
//   "schema":      1,
//   "gridColumns": 12,
//   "gridRows":    8,
//   "margins":     20,        // optional dp — outer padding around grid (default 20)
//   "spacing":     16,        // optional dp — gap between cells (default 16)
//   "cells": [
//     {
//       "type":     "CircularGauge",
//       "paramId":  "RPM",
//       "col":      6,  "row":     0,
//       "colSpan":  6,  "rowSpan": 4,
//       "props":    { "showNeedle": true, "redlineStart": 6500 }
//     },
//     ...
//   ]
// }
//
// Unknown cell `type` values render as nothing (logged). Cells without a
// `paramId` are still placed — some widgets (WarningLight driven by
// explicit `value`) can be used decoratively.
//
// Vehicle gate: once a connected vehicle has been scanned
// (OBDParameterModel.vehicleKnown), a cell bound to a PID that vehicle
// doesn't report shows a "not supported" placeholder instead of a gauge
// that would sit at zero forever.

import QtQuick 2.15
import ".." as App

Item {
    id: root

    // The dashboard spec to render. Typically a JS object parsed from JSON
    // by DashboardManager; can also be an inline object literal for testing.
    property var spec: null

    // Every distinct paramId the spec binds — the host registers these with
    // obdManager.setParameterDemand() so exactly these PIDs get polled.
    readonly property var paramIds: {
        var seen = {}, out = []
        var cells = (spec && spec.cells) ? spec.cells : []
        for (var i = 0; i < cells.length; i++) {
            var id = cells[i].paramId
            if (id && !seen[id]) { seen[id] = true; out.push(id) }
        }
        return out
    }

    // Widget registry: type-string → absolute QML source URL. Owned by the
    // App.WidgetCatalog singleton so the renderer, the editor palette, and the
    // editor properties panel all agree on the supported primitive set without
    // drift (see frontend/dashboards/WidgetCatalog.qml).
    function sourceForType(typeName) {
        return App.WidgetCatalog.urlFor(typeName)
    }

    function _coerce(v) {
        if (v === "NaN") return NaN
        return v
    }

    // Grid geometry (derived from spec, with sensible defaults).
    // `margins` = outer padding around the whole grid.
    // `spacing` = gap between adjacent cells. Kept separate so callers can
    // have tight cells inside a roomy outer frame, or vice versa.
    readonly property int _cols: (spec && spec.gridColumns) ? spec.gridColumns : 12
    readonly property int _rows: (spec && spec.gridRows)    ? spec.gridRows    : 6
    readonly property real _margin:
        (spec && spec.margins !== undefined) ? App.Spacing.dp(spec.margins)
                                             : App.Spacing.dp(20)
    readonly property real _spacing:
        (spec && spec.spacing !== undefined) ? App.Spacing.dp(spec.spacing)
                                             : App.Spacing.dp(16)

    // Cell size = remaining space after margins & gutters, divided by the
    // row/column count. Clamped to >=1 so widgets never go negative-width
    // while the parent is laying out.
    readonly property real _cellW: Math.max(1,
        (width  - 2 * _margin - _spacing * (_cols - 1)) / _cols)
    readonly property real _cellH: Math.max(1,
        (height - 2 * _margin - _spacing * (_rows - 1)) / _rows)

    Repeater {
        model: (root.spec && root.spec.cells) ? root.spec.cells : []
        delegate: Item {
            id: cell

            readonly property int _col:     modelData.col     !== undefined ? modelData.col     : 0
            readonly property int _row:     modelData.row     !== undefined ? modelData.row     : 0
            readonly property int _colSpan: modelData.colSpan !== undefined ? modelData.colSpan : 1
            readonly property int _rowSpan: modelData.rowSpan !== undefined ? modelData.rowSpan : 1

            x: root._margin + _col * (root._cellW + root._spacing)
            y: root._margin + _row * (root._cellH + root._spacing)
            width:  _colSpan * root._cellW + (_colSpan - 1) * root._spacing
            height: _rowSpan * root._cellH + (_rowSpan - 1) * root._spacing
            // Hard backstop: a widget's content may never paint outside its
            // cell, however small the user sizes it.
            clip: true

            // Spec validation: an unknown `type` (typo, or a widget removed
            // from WidgetCatalog) is logged and skipped; an unknown `paramId`
            // is logged and left unbound so the widget shows its placeholder
            // instead of binding to nothing.
            readonly property bool _knownType: App.WidgetCatalog.isKnownType(modelData.type)
            readonly property bool _knownParam:
                modelData.paramId === undefined || modelData.paramId === ""
                || App.OBDParameterModel.hasParameter(modelData.paramId)
            Component.onCompleted: {
                if (!_knownType)
                    console.warn("DashboardRenderer: unknown widget type \"" + modelData.type
                                 + "\" in dashboard \"" + (root.spec && root.spec.id ? root.spec.id : "?")
                                 + "\" — cell skipped")
                else if (!_knownParam)
                    console.warn("DashboardRenderer: unknown paramId \"" + modelData.paramId
                                 + "\" for " + modelData.type + " in dashboard \""
                                 + (root.spec && root.spec.id ? root.spec.id : "?") + "\" — left unbound")
            }

            readonly property bool _unsupported:
                _knownParam && modelData.paramId !== undefined && modelData.paramId !== ""
                && App.OBDParameterModel.vehicleKnown
                && !App.OBDParameterModel.isSupported(modelData.paramId)

            Loader {
                id: widgetLoader
                anchors.fill: parent
                asynchronous: true
                visible: !cell._unsupported
                active: cell._knownType
                source: cell._knownType ? root.sourceForType(modelData.type) : ""

                onLoaded: {
                    if (!item) {
                        console.warn("DashboardRenderer: widget failed to load:", modelData.type)
                        return
                    }

                    // Force the loaded Item to fill the Loader. Loader's
                    // built-in auto-sizing is unreliable when the loaded
                    // root Item has no implicit size and no internal
                    // anchors.fill — the widget ends up partial-size.
                    item.width  = Qt.binding(function() { return widgetLoader.width  })
                    item.height = Qt.binding(function() { return widgetLoader.height })

                    // Bind PID first so widget metadata (title/unit/min/max)
                    // auto-populates before any explicit prop overrides.
                    // Skipped for empty ids and for self-binding widgets
                    // that have no paramId property (Now Playing, G-Force…).
                    if (modelData.paramId !== undefined && modelData.paramId !== ""
                            && item.paramId !== undefined && cell._knownParam) {
                        item.paramId = modelData.paramId
                    }

                    // Apply per-cell props. Silently skip unknown keys.
                    var props = modelData.props || {}
                    for (var key in props) {
                        if (!props.hasOwnProperty(key)) continue
                        if (item[key] === undefined) continue
                        item[key] = root._coerce(props[key])
                    }
                }

                onStatusChanged: {
                    if (status === Loader.Error) {
                        console.warn("DashboardRenderer: load error for type",
                                     modelData.type, "source:", source)
                    }
                }
            }

            // "Not supported by this vehicle" placeholder (see header).
            Rectangle {
                objectName: "dashUnsupported_" + index
                visible: cell._unsupported
                anchors.fill: parent
                radius: App.Spacing.dp(8)
                color: Qt.darker(App.Style.obdBoxBackground, 1.08)
                border.color: Qt.darker(App.Style.obdBarColor, 1.8)
                border.width: 1
                opacity: 0.8

                Column {
                    anchors.centerIn: parent
                    width: parent.width - App.Spacing.dp(12)
                    spacing: App.Spacing.dp(2)
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: (modelData.props && modelData.props.title)
                              ? modelData.props.title
                              : App.OBDParameterModel.getParamInfo(modelData.paramId).title
                        color: App.Style.obdLabelColor
                        font.family: App.Style.fontFamily
                        font.pixelSize: Math.max(9, Math.min(App.Spacing.overallText, cell.height * 0.18))
                        font.bold: true
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        text: "Not supported by this vehicle"
                        color: App.Style.obdLabelColor
                        opacity: 0.75
                        font.family: App.Style.fontFamily
                        font.pixelSize: Math.max(8, Math.min(App.Spacing.overallText * 0.75, cell.height * 0.13))
                    }
                }
            }
        }
    }
}
