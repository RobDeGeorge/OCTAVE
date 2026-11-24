// WidgetCatalog.qml
//
// The gauge primitives a dashboard can contain, for the QML UI: the data
// (display metadata, spans, supported kinds, curated editable props) comes
// from frontend/dashboards/widget-catalog.json through
// dashboardManager.widgetCatalog; this file adds each type's QML url.
//
// DashboardRenderer and Home read from here: which types exist, where each
// one's QML is, which props a spec may set on it.
//
// Url note: this file lives in frontend/dashboards/, so the `../gauges/` prefix
// resolves to frontend/gauges/ regardless of which context loads the catalog.
pragma Singleton
import QtQuick 2.15

QtObject {
    id: catalog

    // Each type's QML source. Everything else about a widget is data:
    // frontend/dashboards/widget-catalog.json, read by DashboardManager
    // (dashboardManager.widgetCatalog, the same object every service client
    // gets; docs/GAUGE_AUTHORING.md lists the fields). A type here with no
    // catalog entry, or the reverse, is not offered.
    readonly property var urls: ({
        "CircularGauge":       Qt.resolvedUrl("../gauges/CircularGauge.qml"),
        "ArcGauge":            Qt.resolvedUrl("../gauges/ArcGauge.qml"),
        "BarGauge":            Qt.resolvedUrl("../gauges/BarGauge.qml"),
        "SegmentedGauge":      Qt.resolvedUrl("../gauges/SegmentedGauge.qml"),
        "LinearGauge":         Qt.resolvedUrl("../gauges/LinearGauge.qml"),
        "DigitalReadout":      Qt.resolvedUrl("../gauges/DigitalReadout.qml"),
        "SparklineGauge":      Qt.resolvedUrl("../gauges/SparklineGauge.qml"),
        "WarningLight":        Qt.resolvedUrl("../gauges/WarningLight.qml"),
        "GForceGauge":         Qt.resolvedUrl("../gauges/GForceGauge.qml"),
        "CompassGauge":        Qt.resolvedUrl("../gauges/CompassGauge.qml"),
        "NowPlayingWidget":    Qt.resolvedUrl("widgets/NowPlayingWidget.qml"),
        "MediaControlsWidget": Qt.resolvedUrl("widgets/MediaControlsWidget.qml"),
        "VehicleWidget":       Qt.resolvedUrl("widgets/VehicleWidget.qml"),
        "ShiftLightWidget":    Qt.resolvedUrl("widgets/ShiftLightWidget.qml"),
        "ClockWidget":         Qt.resolvedUrl("widgets/ClockWidget.qml")
    })

    readonly property var _data: (typeof dashboardManager !== "undefined" && dashboardManager)
                                 ? dashboardManager.widgetCatalog : ({})

    // Each catalog entry plus its `url`, in the catalog's order
    readonly property var widgets: {
        var out = []
        var list = _data.widgets || []
        for (var i = 0; i < list.length; i++) {
            var url = urls[list[i].type]
            if (!url)
                continue
            var entry = Object.assign({}, list[i])
            entry.url = url
            out.push(entry)
        }
        return out
    }

    // type-string → entry, or null if unknown.
    function metaFor(typeName) {
        for (var i = 0; i < widgets.length; i++) {
            if (widgets[i].type === typeName)
                return widgets[i]
        }
        return null
    }

    // type-string → resolved QML url, or "" if unknown.
    function urlFor(typeName) {
        var m = metaFor(typeName)
        return m ? m.url : ""
    }

    // True if `typeName` is a supported primitive (whitelist check).
    // Props of `typeName` whose values are in the bound PID's unit: min / max
    // (rangeProps) and the editableProps marked paramUnit. Specs store them in
    // the PID's own unit; the renderer converts to the gauges' unit
    // (OBDParameterModel.toDisplay).
    function paramUnitProps(typeName) {
        var m = metaFor(typeName)
        if (!m) return []
        var keys = m.rangeProps ? ["min", "max"] : []
        var ed = m.editableProps || []
        for (var i = 0; i < ed.length; i++)
            if (ed[i].paramUnit === true) keys.push(ed[i].key)
        return keys
    }

    // type -> { key: true } of the props a spec may set on that type: the
    // catalog's editableProps, colorProps and specProps, title / unit with
    // labelProps, min / max with rangeProps. Nothing else in a cell's
    // `props` reaches the widget (dashboards/CellProps.js).
    readonly property var _specKeys: {
        var out = {}
        for (var i = 0; i < widgets.length; i++) {
            var m = widgets[i], keys = {}
            var ed = m.editableProps || []
            for (var e = 0; e < ed.length; e++) keys[ed[e].key] = true
            var lists = [m.colorProps || [], m.specProps || []]
            for (var l = 0; l < lists.length; l++)
                for (var k = 0; k < lists[l].length; k++) keys[lists[l][k]] = true
            if (m.labelProps === true) { keys["title"] = true; keys["unit"] = true }
            if (m.rangeProps === true) { keys["min"] = true; keys["max"] = true }
            out[m.type] = keys
        }
        return out
    }
    function specKeys(typeName) {
        return _specKeys[typeName] || ({})
    }

    function isKnownType(typeName) {
        return metaFor(typeName) !== null
    }
}
