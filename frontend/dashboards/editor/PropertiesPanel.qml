// PropertiesPanel.qml
//
// Inspector for the selected canvas cell: the editor's settings sheet
// (beside the dashboard, or under it on a portrait screen), in sections:
//   - Data: the bound PID (button opens the PIDPicker), its live value, a
//     note when the connected car doesn't report it
//   - Look: title / unit overrides (WidgetCatalog `labelProps`; empty =
//     follow the PID), colour swatches (`colorProps`, unset = theme) and
//     the editableProps marked `section: "look"`
//   - Range & alerts: min / max overrides (`rangeProps`), the "range"
//     editableProps (warning / critical limits, flash, red band, triggers)
//     and a Test value slider that drives this widget only (never saved)
//   - Advanced: the other editableProps and the position / size steppers
//   - JSON: the cell's props as text, applied through the spec rules
// Option controls: bool → switch, enum → segmented buttons, int/real/string
// → text field (empty = unset, so the gauge's own default applies). Ends
// with a delete button (undoable from the editor's snackbar).
//
// The panel never mutates the cell itself — every control calls back into
// DashboardEditor (`editorPage`), which owns the working spec.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../.." as App

Rectangle {
    id: panel

    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    // DashboardEditor instance — owns all mutations.
    property var editorPage: null
    property int cellIndex: -1
    property var cell: null

    readonly property var meta: cell ? App.WidgetCatalog.metaFor(cell.type) : null

    // ── Sections (review §4.2): Data · Look · Range & alerts · Advanced · JSON
    readonly property bool hasPid: meta !== null && meta.supportedKinds.length > 0
    readonly property bool _hasRange: meta !== null && (meta.rangeProps === true || optionsIn("range").length > 0)
    readonly property var sections: {
        var out = []
        if (hasPid) out.push({ "id": "data", "label": "Data" })
        out.push({ "id": "look", "label": "Look" })
        if (_hasRange) out.push({ "id": "range", "label": "Range & alerts" })
        out.push({ "id": "advanced", "label": "Advanced" })
        out.push({ "id": "json", "label": "JSON" })
        return out
    }
    // Kept across selections; a section the widget lacks falls back to its first
    property string chosenSection: "data"
    readonly property string section: {
        for (var i = 0; i < sections.length; i++)
            if (sections[i].id === chosenSection) return chosenSection
        return sections.length > 0 ? sections[0].id : "look"
    }
    function _choose(id) { chosenSection = id }

    // The catalog's editable props of one section
    function optionsIn(sec) {
        if (!meta || !meta.editableProps) return []
        return meta.editableProps.filter(function(o) { return (o.section || "advanced") === sec })
    }

    // The test value of this widget (editor-held, never saved), or undefined
    readonly property bool canTest: hasPid && !!cell && !!cell.paramId && meta.rangeProps === true
    readonly property var testValue: (editorPage && editorPage.testValues) ? editorPage.testValues[cellIndex] : undefined

    readonly property int _col: (cell && cell.col     !== undefined) ? cell.col     : 0
    readonly property int _row: (cell && cell.row     !== undefined) ? cell.row     : 0
    readonly property int _cs:  (cell && cell.colSpan !== undefined) ? cell.colSpan : 1
    readonly property int _rs:  (cell && cell.rowSpan !== undefined) ? cell.rowSpan : 1

    // Effective value of a curated prop: explicit spec value, else the
    // gauge default recorded in the catalog.
    function propValue(key, def) {
        if (cell && cell.props && cell.props[key] !== undefined)
            return shown(key, cell.props[key])
        return def
    }

    // Range props are stored in the PID's own unit and edited in the gauges'
    // (a °F user types °F; the spec keeps °C)
    function _isUnitKey(key) {
        return !!cell && !!cell.paramId && App.WidgetCatalog.paramUnitProps(cell.type).indexOf(key) !== -1
    }
    function shown(key, v) { return _isUnitKey(key) ? App.OBDParameterModel.toDisplay(cell.paramId, v) : v }
    function stored(key, v) { return _isUnitKey(key) ? App.OBDParameterModel.fromDisplay(cell.paramId, v) : v }

    // Metadata of the bound PID (title/unit/min/max), or null.
    readonly property var pidMeta: {
        if (!cell || !cell.paramId) return null
        var all = App.OBDParameterModel.displayParameters   // in the gauges' units
        for (var i = 0; i < all.length; i++)
            if (all[i].id === cell.paramId) return all[i]
        return null
    }

    // True when a vehicle is scanned and the bound PID isn't one it reports.
    readonly property bool pidUnsupported:
        cell !== null && !!cell.paramId && App.OBDParameterModel.vehicleKnown
        && !App.OBDParameterModel.isSupported(cell.paramId)

    // Min/max as they will render (override, else PID default) — drives the
    // "min must be below max" warning.
    readonly property real _effMin: (cell && cell.props && cell.props.min !== undefined)
                                    ? shown("min", cell.props.min) : (pidMeta ? pidMeta.min : 0)
    readonly property real _effMax: (cell && cell.props && cell.props.max !== undefined)
                                    ? shown("max", cell.props.max) : (pidMeta ? pidMeta.max : 100)

    // Current override color ("" = theme), read from the first color prop.
    readonly property string currentColor: {
        if (!meta || !meta.colorProps || meta.colorProps.length === 0) return ""
        var v = (cell && cell.props) ? cell.props[meta.colorProps[0]] : undefined
        return v === undefined ? "" : String(v).toUpperCase()
    }

    function setColor(hex) {
        if (!editorPage || !meta) return
        editorPage.setCellProps(cellIndex, meta.colorProps, hex === "" ? undefined : hex)
    }

    // Parse a text field into a number/string prop value, clamped to the
    // option's bounds. Returns undefined when the text isn't usable.
    function parseOption(opt, t) {
        if (opt.kind === "string") return t
        var num = opt.kind === "int" ? parseInt(t) : parseFloat(t)
        if (isNaN(num)) return undefined
        if (opt.min !== undefined) num = Math.max(opt.min, num)
        if (opt.max !== undefined) num = Math.min(opt.max, num)
        return num
    }

    function pidTitle() {
        if (!cell || !cell.paramId || cell.paramId === "") return "Choose PID…"
        var all = App.OBDParameterModel.allParameters
        for (var i = 0; i < all.length; i++)
            if (all[i].id === cell.paramId) return all[i].title
        return cell.paramId
    }

    color: App.Style.obdBoxBackground
    radius: dpMin(10, 3)
    border.color: Qt.darker(App.Style.obdBarColor, 1.6)
    border.width: 1
    visible: cell !== null && meta !== null

    // ── Reusable ◄ value ► stepper row ──────────────────────────────────
    component StepperRow: RowLayout {
        id: stepper
        property string label: ""
        property int value: 0
        signal decrement()
        signal increment()

        spacing: panel.dp(6)

        Text {
            Layout.fillWidth: true
            text: stepper.label
            color: App.Style.obdLabelColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 0.9
        }

        Rectangle {
            objectName: "stepper_" + stepper.label + "_dec"
            Layout.preferredWidth: panel.dp(38)
            Layout.preferredHeight: panel.dp(38)
            radius: panel.dpMin(6, 2)
            color: decMouse.pressed
                   ? Qt.lighter(App.Style.obdBoxBackground, 1.3)
                   : Qt.darker(App.Style.obdBoxBackground, 1.12)
            border.color: Qt.darker(App.Style.obdBarColor, 1.6)
            border.width: 1
            Text {
                anchors.centerIn: parent
                text: "◄"
                color: App.Style.obdValueColor
                font.pixelSize: App.Spacing.overallText * 0.9
            }
            MouseArea { id: decMouse; anchors.fill: parent; onClicked: stepper.decrement() }
        }

        Text {
            Layout.preferredWidth: panel.dp(28)
            horizontalAlignment: Text.AlignHCenter
            text: stepper.value
            color: App.Style.obdValueColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText
            font.bold: true
        }

        Rectangle {
            objectName: "stepper_" + stepper.label + "_inc"
            Layout.preferredWidth: panel.dp(38)
            Layout.preferredHeight: panel.dp(38)
            radius: panel.dpMin(6, 2)
            color: incMouse.pressed
                   ? Qt.lighter(App.Style.obdBoxBackground, 1.3)
                   : Qt.darker(App.Style.obdBoxBackground, 1.12)
            border.color: Qt.darker(App.Style.obdBarColor, 1.6)
            border.width: 1
            Text {
                anchors.centerIn: parent
                text: "►"
                color: App.Style.obdValueColor
                font.pixelSize: App.Spacing.overallText * 0.9
            }
            MouseArea { id: incMouse; anchors.fill: parent; onClicked: stepper.increment() }
        }
    }

    // ── Override text field: empty = follow the default shown as placeholder ──
    component OverrideField: RowLayout {
        id: of
        property string label: ""
        property string key: ""
        property bool numeric: false
        property string placeholder: ""

        spacing: panel.dp(6)

        Text {
            Layout.fillWidth: true
            text: of.label
            elide: Text.ElideRight
            color: App.Style.obdLabelColor
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 0.9
        }

        TextField {
            id: ofField
            objectName: "panelOverride_" + of.key
            Layout.preferredWidth: of.numeric ? panel.dp(90) : panel.dp(150)
            horizontalAlignment: of.numeric ? TextInput.AlignRight : TextInput.AlignLeft
            inputMethodHints: of.numeric ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
            font.family: App.Style.fontFamily
            font.pixelSize: App.Spacing.overallText * 0.9
            color: App.Style.primaryTextColor
            placeholderText: of.placeholder
            placeholderTextColor: Qt.darker(App.Style.obdLabelColor, 1.4)
            background: Rectangle {
                radius: panel.dpMin(6, 2)
                color: Qt.darker(App.Style.obdBoxBackground, 1.25)
                border.color: parent.activeFocus ? App.Style.accent : Qt.darker(App.Style.obdBarColor, 1.6)
                border.width: 1
            }

            function _currentText() {
                var v = (panel.cell && panel.cell.props) ? panel.cell.props[of.key] : undefined
                return (v === undefined || v === null) ? "" : String(panel.shown(of.key, v))
            }
            text: _currentText()
            // Typing breaks the binding; re-sync when the selection changes.
            property var _cellRef: panel.cell
            on_CellRefChanged: if (!activeFocus) text = _currentText()

            onEditingFinished: {
                var t = text.trim()
                if (t === "") {
                    panel.editorPage.clearCellProp(panel.cellIndex, of.key)
                    return
                }
                if (of.numeric) {
                    var num = parseFloat(t)
                    if (!isNaN(num)) panel.editorPage.setCellProp(panel.cellIndex, of.key, panel.stored(of.key, num))
                    else text = _currentText()
                } else {
                    panel.editorPage.setCellProp(panel.cellIndex, of.key, t)
                }
            }
        }
    }

    // ── The catalog's editable props of one settings section ────────────
    // (editableProps `section`: look | range | advanced)
    component OptionList: ColumnLayout {
        property string section: "advanced"
        spacing: panel.dp(10)
        Repeater {
            model: panel.optionsIn(parent.section)
            delegate: RowLayout {
                Layout.fillWidth: true
                spacing: panel.dp(6)

                Text {
                    Layout.fillWidth: true
                    text: modelData.label
                    elide: Text.ElideRight
                    color: App.Style.obdLabelColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 0.9
                }

                // Enum → segmented buttons.
                Row {
                    visible: modelData.kind === "enum"
                    spacing: panel.dp(4)
                    // The option entry — the inner Repeater's modelData
                    // (the enum value) shadows it inside the buttons.
                    readonly property var opt: modelData
                    Repeater {
                        model: modelData.kind === "enum" ? modelData.options : []
                        delegate: Rectangle {
                            id: enumBtn
                            readonly property var opt: parent ? parent.opt : null
                            readonly property bool _selected: opt !== null
                                && panel.propValue(opt.key, opt.def) === modelData
                            objectName: "panelOptEnum_" + (opt ? opt.key : "") + "_" + modelData
                            width: enumLabel.implicitWidth + panel.dp(14)
                            height: panel.dp(34)
                            radius: panel.dpMin(6, 2)
                            color: _selected ? App.Style.accent : Qt.darker(App.Style.obdBoxBackground, 1.12)
                            border.color: Qt.darker(App.Style.obdBarColor, 1.6)
                            border.width: 1
                            Text {
                                id: enumLabel
                                anchors.centerIn: parent
                                text: modelData
                                color: enumBtn._selected ? App.Style.obdBoxBackground : App.Style.obdValueColor
                                font.family: App.Style.fontFamily
                                font.pixelSize: App.Spacing.overallText * 0.8
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: if (enumBtn.opt)
                                    panel.editorPage.setCellProp(panel.cellIndex, enumBtn.opt.key, modelData)
                            }
                        }
                    }
                }

                Switch {
                    objectName: "panelOpt_" + modelData.key
                    visible: modelData.kind === "bool"
                    checked: panel.propValue(modelData.key, modelData.def) === true
                    onToggled: panel.editorPage.setCellProp(panel.cellIndex, modelData.key, checked)

                    // User toggles break the `checked` binding above, and
                    // Repeater delegates survive selection changes between
                    // same-type cells — re-sync explicitly.
                    property var _cellRef: panel.cell
                    on_CellRefChanged: checked = panel.propValue(modelData.key, modelData.def) === true
                }

                TextField {
                    objectName: "panelOptField_" + modelData.key
                    visible: modelData.kind !== "bool" && modelData.kind !== "enum"
                    Layout.preferredWidth: modelData.kind === "string" ? panel.dp(150) : panel.dp(90)
                    horizontalAlignment: modelData.kind === "string" ? TextInput.AlignLeft : TextInput.AlignRight
                    inputMethodHints: modelData.kind === "string" ? Qt.ImhNone : Qt.ImhFormattedNumbersOnly
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 0.9
                    color: App.Style.primaryTextColor
                    placeholderText: modelData.def === null ? "off"
                                     : (modelData.kind === "string" && modelData.def === "" ? "none"
                                        : String(modelData.def))
                    placeholderTextColor: Qt.darker(App.Style.obdLabelColor, 1.4)
                    background: Rectangle {
                        radius: panel.dpMin(6, 2)
                        color: Qt.darker(App.Style.obdBoxBackground, 1.25)
                        border.color: parent.activeFocus ? App.Style.accent : Qt.darker(App.Style.obdBarColor, 1.6)
                        border.width: 1
                    }

                    function _currentText() {
                        var v = panel.propValue(modelData.key, modelData.def)
                        return (v === null || v === undefined || v !== v) ? "" : String(v)
                    }
                    text: _currentText()

                    // Same re-sync as the Switch: typing breaks the binding,
                    // and the delegate is reused across same-type cells.
                    property var _cellRef: panel.cell
                    on_CellRefChanged: if (!activeFocus) text = _currentText()

                    onEditingFinished: {
                        var t = text.trim()
                        if (t === "") {
                            panel.editorPage.clearCellProp(panel.cellIndex, modelData.key)
                            return
                        }
                        var v = panel.parseOption(modelData, t)
                        if (v !== undefined) {
                            panel.editorPage.setCellProp(panel.cellIndex, modelData.key, panel.stored(modelData.key, v))
                            text = String(v)     // show the clamped value
                        } else {
                            text = _currentText()
                        }
                    }
                }
            }
        }
    }

    component SectionLabel: Text {
        color: App.Style.obdLabelColor
        font.family: App.Style.fontFamily
        font.pixelSize: App.Spacing.overallText * 0.8
        font.bold: true
        opacity: 0.8
    }

    // The panel floats over the canvas — swallow taps on its background so
    // they don't fall through to grid cells underneath.
    MouseArea {
        anchors.fill: parent
        onWheel: function(wheel) { wheel.accepted = true }
    }

    Flickable {
        objectName: "editorPanelScroll"
        anchors.fill: parent
        anchors.margins: panel.dp(14)
        contentWidth: width
        contentHeight: panelColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: panelColumn
            width: parent.width
            spacing: panel.dp(10)

            // ── Header: widget type + deselect ──────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: panel.dp(8)

                Text {
                    text: panel.meta ? panel.meta.glyph : ""
                    color: App.Style.accent
                    font.pixelSize: App.Spacing.overallText * 1.3
                }
                Text {
                    Layout.fillWidth: true
                    // A type this build doesn't have stays in the file, inert
                    text: panel.meta ? panel.meta.displayName
                                     : (panel.cell ? panel.cell.type + " (needs a newer OCTAVE)" : "")
                    color: App.Style.primaryTextColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 1.1
                    font.bold: true
                }
                Rectangle {
                    Layout.preferredWidth: panel.dp(44)
                    Layout.preferredHeight: panel.dp(44)
                    radius: panel.dpMin(6, 2)
                    color: "transparent"
                    border.color: Qt.darker(App.Style.obdBarColor, 1.6)
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: App.Style.obdLabelColor
                        font.pixelSize: App.Spacing.overallText * 0.9
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: if (panel.editorPage) panel.editorPage.closeSheet()
                    }
                }
            }

            // ── Sections ────────────────────────────────────────────────
            Flow {
                Layout.fillWidth: true
                spacing: panel.dp(6)
                Repeater {
                    model: panel.sections
                    delegate: Rectangle {
                        objectName: "panelSection_" + modelData.id
                        readonly property bool _on: panel.section === modelData.id
                        width: secLabel.implicitWidth + panel.dp(22)
                        height: panel.dp(40)
                        radius: height / 2
                        color: _on ? App.Style.accent : Qt.darker(App.Style.obdBoxBackground, 1.2)
                        border.width: _on ? 0 : 1
                        border.color: Qt.darker(App.Style.obdBarColor, 1.6)
                        Text {
                            id: secLabel
                            anchors.centerIn: parent
                            text: modelData.label
                            color: parent._on ? App.Style.obdBoxBackground : App.Style.primaryTextColor
                            font.family: App.Style.fontFamily
                            font.bold: parent._on
                            font.pixelSize: App.Spacing.overallText * 0.85
                        }
                        MouseArea { anchors.fill: parent; onClicked: panel._choose(modelData.id) }
                    }
                }
            }

            // ── Data: what it shows ──
            ColumnLayout {
                Layout.fillWidth: true
                visible: panel.section === "data"
                spacing: panel.dp(10)

                SectionLabel {
                    text: "PID"
                    visible: panel.meta !== null && panel.meta.supportedKinds.length > 0
                }

                Rectangle {
                    objectName: "panelPidButton"
                    visible: panel.meta !== null && panel.meta.supportedKinds.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: panel.dp(44)
                    radius: panel.dpMin(6, 2)
                    color: pidMouse.pressed
                           ? Qt.lighter(App.Style.obdBoxBackground, 1.3)
                           : Qt.darker(App.Style.obdBoxBackground, 1.12)
                    border.color: (!panel.cell || !panel.cell.paramId || panel.cell.paramId === "")
                                  ? App.Style.statusDanger
                                  : Qt.darker(App.Style.obdBarColor, 1.6)
                    border.width: 1

                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: panel.dp(10)
                        anchors.rightMargin: panel.dp(10)
                        anchors.verticalCenter: parent.verticalCenter
                        text: panel.pidTitle()
                        elide: Text.ElideRight
                        color: App.Style.obdValueColor
                        font.family: App.Style.fontFamily
                        font.pixelSize: App.Spacing.overallText
                    }

                    MouseArea {
                        id: pidMouse
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (panel.editorPage) panel.editorPage.openPidPicker()
                    }
                }

                // Vehicle-gate warning: bound PID won't read on the connected car.
                Text {
                    objectName: "panelPidUnsupported"
                    visible: panel.pidUnsupported
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Your vehicle doesn't report this PID, so it won't show data on this car."
                    color: App.Style.statusDanger
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 0.8
                }

                // The live value (or the test value) of the bound PID
                Text {
                    objectName: "panelLiveValue"
                    visible: panel.hasPid && panel.cell && !!panel.cell.paramId
                    Layout.fillWidth: true
                    text: {
                        var v = App.OBDParameterModel.sample(panel.cell ? panel.cell.paramId : "")
                        return isFinite(v) ? "Now: " + v.toFixed(Math.max(0, panel.pidMeta ? panel.pidMeta.decimals : 1))
                                             + (panel.pidMeta && panel.pidMeta.unit ? " " + panel.pidMeta.unit : "")
                                           : "No reading yet"
                    }
                    color: App.Style.obdLabelColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 0.85
                }
                Text {
                    visible: !panel.hasPid
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "This widget reads its own data."
                    color: App.Style.obdLabelColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 0.85
                }
            }

            // ── Look: title, unit, colour, style ──
            ColumnLayout {
                Layout.fillWidth: true
                visible: panel.section === "look"
                spacing: panel.dp(10)

                SectionLabel { text: "LABEL"; visible: panel.meta !== null && panel.meta.labelProps === true }
                OverrideField {
                    Layout.fillWidth: true
                    visible: panel.meta !== null && panel.meta.labelProps === true
                    label: "Title"; key: "title"
                    placeholder: panel.pidMeta ? panel.pidMeta.title : "PID title"
                }
                OverrideField {
                    Layout.fillWidth: true
                    visible: panel.meta !== null && panel.meta.labelProps === true
                    label: "Unit"; key: "unit"
                    placeholder: panel.pidMeta ? panel.pidMeta.unit : "PID unit"
                }
                OptionList { Layout.fillWidth: true; section: "look" }
                SectionLabel {
                    text: "COLOR"
                    Layout.topMargin: panel.dp(6)
                    visible: panel.meta !== null && panel.meta.colorProps && panel.meta.colorProps.length > 0
                }

                Flow {
                    Layout.fillWidth: true
                    visible: panel.meta !== null && panel.meta.colorProps && panel.meta.colorProps.length > 0
                    spacing: panel.dp(6)

                    // First swatch = theme (unset).
                    Repeater {
                        model: [""].concat(App.WidgetCatalog.colorSwatches)
                        delegate: Rectangle {
                            objectName: "panelSwatch_" + (modelData === "" ? "theme" : modelData.substring(1))
                            readonly property bool _selected: panel.currentColor === modelData.toUpperCase()
                            width: panel.dp(34)
                            height: panel.dp(34)
                            radius: width / 2
                            color: modelData === "" ? App.Style.obdBarColor : modelData
                            border.color: _selected ? App.Style.primaryTextColor
                                                    : Qt.darker(App.Style.obdBarColor, 1.6)
                            border.width: _selected ? 3 : 1

                            // "Theme" swatch: label so it reads as "follow theme".
                            Text {
                                visible: modelData === ""
                                anchors.centerIn: parent
                                text: "T"
                                color: App.Style.obdBoxBackground
                                font.family: App.Style.fontFamily
                                font.pixelSize: App.Spacing.overallText * 0.8
                                font.bold: true
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.setColor(modelData)
                            }
                        }
                    }
                }
            }

            // ── Range & alerts ──
            ColumnLayout {
                Layout.fillWidth: true
                visible: panel.section === "range"
                spacing: panel.dp(10)

                SectionLabel { text: "RANGE"; visible: panel.meta !== null && panel.meta.rangeProps === true }
                OverrideField {
                    Layout.fillWidth: true
                    visible: panel.meta !== null && panel.meta.rangeProps === true
                    label: "Min"; key: "min"; numeric: true
                    placeholder: panel.pidMeta ? String(panel.pidMeta.min) : "0"
                }
                OverrideField {
                    Layout.fillWidth: true
                    visible: panel.meta !== null && panel.meta.rangeProps === true
                    label: "Max"; key: "max"; numeric: true
                    placeholder: panel.pidMeta ? String(panel.pidMeta.max) : "100"
                }
                Text {
                    objectName: "panelRangeWarning"
                    visible: panel.meta !== null && panel.meta.rangeProps === true
                             && panel._effMin >= panel._effMax
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Min must be lower than max. The gauge will stay empty until it is."
                    color: App.Style.statusDanger
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 0.8
                }

                OptionList { Layout.fillWidth: true; section: "range" }

                // A value for this widget only, to see its needle, bands and
                // text at any reading; never saved
                SectionLabel {
                    text: "TEST VALUE"
                    Layout.topMargin: panel.dp(6)
                    visible: panel.canTest
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: panel.canTest
                    spacing: panel.dp(8)
                    Slider {
                        id: testSlider
                        objectName: "panelTestSlider"
                        Layout.fillWidth: true
                        from: panel._effMin
                        to: panel._effMax > panel._effMin ? panel._effMax : panel._effMin + 1
                        value: panel.testValue !== undefined ? panel.testValue : (from + to) / 2
                        onMoved: panel.editorPage.setTestValue(panel.cellIndex, value)
                    }
                    Text {
                        Layout.preferredWidth: panel.dp(56)
                        horizontalAlignment: Text.AlignRight
                        text: panel.testValue !== undefined ? Number(panel.testValue).toFixed(1) : "Live"
                        color: panel.testValue !== undefined ? App.Style.accent : App.Style.obdLabelColor
                        font.family: App.Style.fontFamily
                        font.pixelSize: App.Spacing.overallText * 0.85
                    }
                    Rectangle {
                        objectName: "panelTestReset"
                        visible: panel.testValue !== undefined
                        Layout.preferredWidth: resetLabel.implicitWidth + panel.dp(20)
                        Layout.preferredHeight: panel.dp(40)
                        radius: height / 2
                        color: Qt.darker(App.Style.obdBoxBackground, 1.2)
                        border.color: Qt.darker(App.Style.obdBarColor, 1.6)
                        border.width: 1
                        Text {
                            id: resetLabel
                            anchors.centerIn: parent
                            text: "Live"
                            color: App.Style.primaryTextColor
                            font.family: App.Style.fontFamily
                            font.pixelSize: App.Spacing.overallText * 0.85
                        }
                        MouseArea { anchors.fill: parent; onClicked: panel.editorPage.clearTestValue(panel.cellIndex) }
                    }
                }
            }

            // ── Advanced ──
            ColumnLayout {
                Layout.fillWidth: true
                visible: panel.section === "advanced"
                spacing: panel.dp(10)

                OptionList { Layout.fillWidth: true; section: "advanced" }

                SectionLabel { text: "POSITION & SIZE"; Layout.topMargin: panel.dp(6) }
                StepperRow {
                    Layout.fillWidth: true
                    label: "Column"; value: panel._col
                    onDecrement: panel.editorPage.nudgeCell(panel.cellIndex, -1, 0)
                    onIncrement: panel.editorPage.nudgeCell(panel.cellIndex, 1, 0)
                }
                StepperRow {
                    Layout.fillWidth: true
                    label: "Row"; value: panel._row
                    onDecrement: panel.editorPage.nudgeCell(panel.cellIndex, 0, -1)
                    onIncrement: panel.editorPage.nudgeCell(panel.cellIndex, 0, 1)
                }
                StepperRow {
                    Layout.fillWidth: true
                    label: "Width"; value: panel._cs
                    onDecrement: panel.editorPage.resizeCell(panel.cellIndex, -1, 0)
                    onIncrement: panel.editorPage.resizeCell(panel.cellIndex, 1, 0)
                }
                StepperRow {
                    Layout.fillWidth: true
                    label: "Height"; value: panel._rs
                    onDecrement: panel.editorPage.resizeCell(panel.cellIndex, 0, -1)
                    onIncrement: panel.editorPage.resizeCell(panel.cellIndex, 0, 1)
                }
            }

            // ── JSON ──
            ColumnLayout {
                Layout.fillWidth: true
                visible: panel.section === "json"
                spacing: panel.dp(10)

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "This widget's options as JSON. Keys it doesn't know are dropped when you apply."
                    color: App.Style.obdLabelColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 0.8
                }
                TextArea {
                    id: jsonArea
                    objectName: "panelJson"
                    Layout.fillWidth: true
                    Layout.preferredHeight: panel.dp(200)
                    wrapMode: TextEdit.Wrap
                    font.family: "monospace"
                    font.pixelSize: App.Spacing.overallText * 0.8
                    color: App.Style.primaryTextColor
                    background: Rectangle {
                        radius: panel.dpMin(6, 2)
                        color: Qt.darker(App.Style.obdBoxBackground, 1.25)
                        border.color: jsonArea.activeFocus ? App.Style.accent : Qt.darker(App.Style.obdBarColor, 1.6)
                        border.width: 1
                    }
                    function _current() { return JSON.stringify((panel.cell && panel.cell.props) || {}, null, 2) }
                    text: _current()
                    property var _cellRef: panel.cell
                    on_CellRefChanged: if (!activeFocus) text = _current()
                }
                Text {
                    id: jsonStatus
                    objectName: "panelJsonStatus"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    visible: text.length > 0
                    color: App.Style.statusDanger
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 0.8
                }
                Rectangle {
                    objectName: "panelJsonApply"
                    Layout.preferredWidth: applyLabel.implicitWidth + panel.dp(28)
                    Layout.preferredHeight: panel.dp(44)
                    radius: height / 2
                    color: App.Style.accent
                    Text {
                        id: applyLabel
                        anchors.centerIn: parent
                        text: "Apply"
                        color: App.Style.obdBoxBackground
                        font.family: App.Style.fontFamily
                        font.bold: true
                        font.pixelSize: App.Spacing.overallText * 0.9
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            jsonStatus.text = panel.editorPage.setCellPropsJson(panel.cellIndex, jsonArea.text)
                            if (jsonStatus.text === "") jsonArea.text = jsonArea._current()
                        }
                    }
                }
            }

            // ── Delete ──────────────────────────────────────────────────
            Rectangle {
                objectName: "panelRemoveButton"
                Layout.fillWidth: true
                Layout.preferredHeight: panel.dp(44)
                Layout.topMargin: panel.dp(12)
                radius: panel.dpMin(6, 2)
                color: deleteMouse.pressed
                       ? Qt.darker(App.Style.statusDanger, 1.3)
                       : "transparent"
                border.color: App.Style.statusDanger
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "Remove widget"
                    color: App.Style.statusDanger
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText
                    font.bold: true
                }

                MouseArea {
                    id: deleteMouse
                    anchors.fill: parent
                    onClicked: if (panel.editorPage) panel.editorPage.deleteCell(panel.cellIndex)
                }
            }
        }
    }
}
