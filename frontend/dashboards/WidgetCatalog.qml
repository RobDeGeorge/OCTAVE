// WidgetCatalog.qml
//
// The gauge primitives a dashboard can contain, for the QML UI: the data
// (display metadata, spans, supported kinds, curated editable props) comes
// from frontend/dashboards/widget-catalog.json through
// dashboardManager.widgetCatalog; this file adds each type's QML url.
//
// DashboardRenderer (render-time), the editor palette (which types you can add),
// and the properties panel (which props you can tweak) all read from here, so
// the supported-widget set can't drift between rendering and editing. It's also
// the natural home for the type whitelist the deferred renderer validation will
// use (see TODO/dashboards-roadmap.md, "fast-follow").
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
        "ClockWidget":         Qt.resolvedUrl("widgets/ClockWidget.qml")
    })

    readonly property var _data: (typeof dashboardManager !== "undefined" && dashboardManager)
                                 ? dashboardManager.widgetCatalog : ({})

    // Ordered list (palette order), each the catalog entry plus its `url`
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

    // Swatches offered by the properties panel's Color row. Unset (the first,
    // "theme" swatch) keeps the widget on the live EnvironmentTheme colors.
    readonly property var colorSwatches: _data.colorSwatches || []

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
    // the PID's own unit; the renderer, the editor canvas and the properties
    // panel convert to the gauges' unit (OBDParameterModel.toDisplay).
    function paramUnitProps(typeName) {
        var m = metaFor(typeName)
        if (!m) return []
        var keys = m.rangeProps ? ["min", "max"] : []
        var ed = m.editableProps || []
        for (var i = 0; i < ed.length; i++)
            if (ed[i].paramUnit === true) keys.push(ed[i].key)
        return keys
    }

    function isKnownType(typeName) {
        return metaFor(typeName) !== null
    }

    // The gallery's categories, in order
    readonly property var categories: _data.categories || []

    // Whether a widget type can show a parameter of `kind`
    function acceptsKind(typeName, kind) {
        var m = metaFor(typeName)
        if (!m) return false
        for (var i = 0; i < m.supportedKinds.length; i++)
            if (m.supportedKinds[i] === "*" || m.supportedKinds[i] === kind) return true
        return false
    }

    // The widget the data-first add proposes for a parameter: the catalog's
    // `suggestions` by id, else by kind, else a plain readout. Self-binding
    // widgets (G-force, compass) are proposed for their sensor ids and take
    // no paramId (supportedKinds empty).
    function suggestFor(paramId, kind) {
        var s = _data.suggestions || {}
        var byId = (s.byId || {})[paramId]
        if (byId && metaFor(byId)) return byId
        var byKind = (s.byKind || {})[kind]
        if (byKind && acceptsKind(byKind, kind)) return byKind
        return "DigitalReadout"
    }
}
