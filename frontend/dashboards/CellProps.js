// CellProps.js
//
// How a cell's spec props reach its loaded widget, shared by
// DashboardRenderer and the editor canvas (which had a copy each).
//
// A prop is applied only when the widget catalog names it for the cell's
// type (WidgetCatalog.specKeys: editableProps, colorProps, specProps, title /
// unit, min / max). A spec is data that may come from anyone: before this
// list, any key that happened to be a property of the QML item was written,
// so a shared file could hide a gauge (`visible`), shrink it (`width`) or
// freeze it on a made-up reading (`value`).
//
// Each write is on its own: a value of the wrong type ("fillColor": "redd")
// throws in QML, and used to end the loop, silently leaving the later keys
// (a needle, a warning limit) unset.
//
// Not a `.pragma library`: Qt.binding needs the caller's QML context.

// "NaN" is how a spec (JSON has no NaN) says "unset" for a limit
function coerce(v) {
    return v === "NaN" ? NaN : v
}

// Range props (min, max, limits) are in the PID's own unit in the spec:
// bound through the gauges' unit, so they follow a unit setting change
function unitBinding(model, paramId, v) {
    return Qt.binding(function() { return model.toDisplay(paramId, v) })
}

// A hold margin is a difference in the PID's own unit: the scale only
function deltaBinding(model, paramId, v) {
    return Qt.binding(function() { return model.toDisplayDelta(paramId, v) })
}

// item: the loaded widget; props: the cell's `props` object;
// catalog: App.WidgetCatalog; model: App.OBDParameterModel;
// who: the caller's name, for the log
function apply(item, typeName, paramId, props, catalog, model, who) {
    if (!item || !props)
        return
    var allowed = catalog.specKeys(typeName)
    var unitKeys = paramId ? catalog.paramUnitProps(typeName) : []
    for (var key in props) {
        if (!props.hasOwnProperty(key))
            continue
        if (allowed[key] !== true) {
            // Only worth a line when it would have done something
            if (item[key] !== undefined)
                console.warn(who + ": \"" + key + "\" is not an option of " + typeName + ", ignored")
            continue
        }
        if (item[key] === undefined)
            continue
        try {
            var v = coerce(props[key])
            if (unitKeys.indexOf(key) !== -1)
                item[key] = unitBinding(model, paramId, v)
            else if (key === "alertHysteresis" && paramId && typeof v === "number")
                item[key] = deltaBinding(model, paramId, v)
            else
                item[key] = v
        } catch (e) {
            console.warn(who + ": " + typeName + " option \"" + key + "\" has an unusable value ("
                         + JSON.stringify(props[key]) + "), left at its default")
        }
    }
}
