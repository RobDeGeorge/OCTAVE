// A compact segmented scale for RPM, load, fuel and other numeric PIDs.
import QtQuick 2.15
import ".." as App
import "AlertLevel.js" as Alerts

ValueGaugeBase {
    id: root

    readonly property var octaveSupportedKinds: ["*"]
    // paramId, title, unit, min, max, value, hasValue, decimals and the alert
    // properties (warnAbove ... alertLevel) are ValueGaugeBase's

    property int segmentCount: 20
    property bool showLabel: true
    property bool showValue: true
    property color fillColor: App.Style.obdBarColor
    property color trackColor: Qt.darker(App.Style.obdBoxBackground, 1.2)
    property color labelColor: App.Style.obdLabelColor
    property color valueColor: App.Style.obdValueColor

    readonly property bool _validRange: isFinite(min) && isFinite(max) && max > min
    readonly property int _count: Math.max(2, Math.min(40, segmentCount))
    readonly property real _normalized: hasValue && _validRange
        ? Math.max(0, Math.min(1, (value - min) / (max - min))) : 0
    readonly property int _litSegments: Math.ceil(_normalized * _count)
    readonly property string _formatted: hasValue
        ? value.toFixed(Math.max(0, Math.min(4, decimals))) : "--"
    readonly property real _headerH: (showLabel || showValue) ? Math.max(0, height * 0.42) : 0
    readonly property real _gap: Math.min(App.Spacing.dp(4), Math.max(0, height * 0.08))

    Item {
        id: header
        width: parent.width
        height: root._headerH

        Text {
            visible: root.showLabel
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: root.showValue ? parent.width * 0.48 : parent.width
            text: root.title
            color: root.labelColor
            font.family: App.Style.fontFamily
            font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText * 0.9, parent.height * 0.7))
            elide: Text.ElideRight
        }
        Text {
            objectName: "segmentedValue"
            visible: root.showValue
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: root.showLabel ? parent.width * 0.48 : parent.width
            text: root._formatted + (root.unit ? " " + root.unit : "")
            horizontalAlignment: Text.AlignRight
            color: root.alertLevel > 0 ? root._alertColor : root.valueColor
            opacity: root._alertOpacity
            font.family: App.Style.fontFamily
            font.bold: true
            font.pixelSize: Math.max(1, Math.min(App.Spacing.overallText, parent.height * 0.7))
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 1
        }
    }

    Item {
        anchors.top: header.bottom
        anchors.topMargin: root._gap
        anchors.bottom: parent.bottom
        width: parent.width

        Row {
            id: segments
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: Math.max(0, Math.min(parent.height, App.Spacing.dp(30)))
            spacing: Math.min(App.Spacing.dp(3), width / (root._count * 4))

            Repeater {
                model: root._count
                delegate: Rectangle {
                    required property int index
                    readonly property bool lit: index < root._litSegments
                    // The part of the scale this segment covers. It shows a
                    // limit's colour when it lies wholly past it, like a
                    // shift-light strip: at or above a high limit from its
                    // lower edge, at or below a low limit up to its upper
                    // edge. (Judged by the upper edge alone, the segment
                    // below a high limit lit amber up to a segment early,
                    // and low limits coloured nothing.)
                    readonly property real _step: (root.max - root.min) / root._count
                    readonly property real _lo: root.min + index * _step
                    readonly property real _hi: root.min + (index + 1) * _step
                    readonly property real _eps: Math.abs(_step) * 1e-6
                    readonly property int bandLevel: root._validRange
                        ? Math.max(Alerts.level(_lo + _eps, root.warnAbove, NaN, root.critAbove, NaN),
                                   Alerts.level(_hi - _eps, NaN, root.warnBelow, NaN, root.critBelow)) : 0
                    readonly property bool warningBand: bandLevel > 0
                    readonly property color bandColor: bandLevel === 2 ? root.critColor : root.warnColor
                    width: Math.max(0, (segments.width - (root._count - 1) * segments.spacing) / root._count)
                    height: segments.height
                    radius: Math.min(App.Spacing.dp(2), width / 4, height / 4)
                    color: lit ? (warningBand ? bandColor
                                  : root.alertLevel > 0 ? root._alertColor : root.fillColor)
                               : root.trackColor
                    border.width: warningBand ? 1 : 0
                    border.color: Qt.rgba(bandColor.r, bandColor.g, bandColor.b, 0.45)
                }
            }
        }
    }
}
