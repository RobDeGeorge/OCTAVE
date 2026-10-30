// A compact segmented scale for RPM, load, fuel and other numeric PIDs.
import QtQuick 2.15
import ".." as App

Item {
    id: root

    readonly property var octaveSupportedKinds: ["*"]
    property string paramId: ""
    readonly property var _pInfo: {
        var all = App.OBDParameterModel.displayParameters
        for (var i = 0; i < all.length; ++i)
            if (all[i].id === paramId) return all[i]
        return null
    }
    property string title: _pInfo ? _pInfo.title : ""
    property string unit: _pInfo ? _pInfo.unit : ""
    property real min: _pInfo ? _pInfo.min : 0
    property real max: _pInfo ? _pInfo.max : 100
    property real value: App.OBDParameterModel.sample(paramId)
    property int decimals: 0
    property int segmentCount: 20
    property real warnAbove: NaN
    property bool showLabel: true
    property bool showValue: true
    property color fillColor: App.Style.obdBarColor
    property color trackColor: Qt.darker(App.Style.obdBoxBackground, 1.2)
    property color warnColor: App.Style.statusDanger
    property color labelColor: App.Style.obdLabelColor
    property color valueColor: App.Style.obdValueColor

    readonly property bool hasValue: isFinite(value)
    readonly property bool _validRange: isFinite(min) && isFinite(max) && max > min
    readonly property int _count: Math.max(2, Math.min(40, segmentCount))
    readonly property real _normalized: hasValue && _validRange
        ? Math.max(0, Math.min(1, (value - min) / (max - min))) : 0
    readonly property int _litSegments: Math.ceil(_normalized * _count)
    readonly property bool _warning: hasValue && isFinite(warnAbove) && value >= warnAbove
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
            color: root._warning ? root.warnColor : root.valueColor
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
                    readonly property bool warningBand: isFinite(root.warnAbove) && root._validRange
                        && root.min + (index + 1) * (root.max - root.min) / root._count > root.warnAbove
                    width: Math.max(0, (segments.width - (root._count - 1) * segments.spacing) / root._count)
                    height: segments.height
                    radius: Math.min(App.Spacing.dp(2), width / 4, height / 4)
                    color: lit ? ((root._warning || warningBand) ? root.warnColor : root.fillColor)
                               : root.trackColor
                    border.width: warningBand ? 1 : 0
                    border.color: Qt.rgba(root.warnColor.r, root.warnColor.g, root.warnColor.b, 0.45)
                }
            }
        }
    }
}
