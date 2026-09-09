// PropertiesPanel.qml
//
// Inspector for the selected canvas cell (Phase 3 Milestones B + D). Shows:
//   - the widget type and a deselect button
//   - the bound PID (button opens the PIDPicker)
//   - position / size steppers (◄ value ► — no drag handles, per roadmap)
//   - the widget's curated editable props from WidgetCatalog `editableProps`
//     (bool → switch, int/real → text field; empty field = unset, so the
//     gauge's own default applies)
//   - a delete button
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

    readonly property int _col: (cell && cell.col     !== undefined) ? cell.col     : 0
    readonly property int _row: (cell && cell.row     !== undefined) ? cell.row     : 0
    readonly property int _cs:  (cell && cell.colSpan !== undefined) ? cell.colSpan : 1
    readonly property int _rs:  (cell && cell.rowSpan !== undefined) ? cell.rowSpan : 1

    // Effective value of a curated prop: explicit spec value, else the
    // gauge default recorded in the catalog.
    function propValue(key, def) {
        if (cell && cell.props && cell.props[key] !== undefined)
            return cell.props[key]
        return def
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
                    text: panel.meta ? panel.meta.displayName : ""
                    color: App.Style.primaryTextColor
                    font.family: App.Style.fontFamily
                    font.pixelSize: App.Spacing.overallText * 1.1
                    font.bold: true
                }
                Rectangle {
                    Layout.preferredWidth: panel.dp(32)
                    Layout.preferredHeight: panel.dp(32)
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
                        onClicked: if (panel.editorPage) panel.editorPage.clearSelection()
                    }
                }
            }

            // ── PID binding ─────────────────────────────────────────────
            // Hidden for self-binding widgets (supportedKinds: [] in the
            // catalog — e.g. Now Playing, G-Force, Compass).
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

            // ── Position & size ─────────────────────────────────────────
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

            // ── Curated widget options ──────────────────────────────────
            SectionLabel {
                text: "OPTIONS"
                Layout.topMargin: panel.dp(6)
                visible: panel.meta !== null && panel.meta.editableProps.length > 0
            }

            Repeater {
                model: panel.meta ? panel.meta.editableProps : []
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
                        visible: modelData.kind !== "bool"
                        Layout.preferredWidth: panel.dp(90)
                        horizontalAlignment: TextInput.AlignRight
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        font.family: App.Style.fontFamily
                        font.pixelSize: App.Spacing.overallText * 0.9
                        color: App.Style.primaryTextColor
                        placeholderText: modelData.def === null ? "off" : String(modelData.def)

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
                            var num = modelData.kind === "int" ? parseInt(t) : parseFloat(t)
                            if (!isNaN(num))
                                panel.editorPage.setCellProp(panel.cellIndex, modelData.key, num)
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
                    onClicked: if (panel.editorPage) panel.editorPage.removeCell(panel.cellIndex)
                }
            }
        }
    }
}
