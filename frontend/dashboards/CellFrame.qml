// CellFrame.qml
//
// The card behind a dashboard cell whose props say `frame: true`: the box the
// reading cards of the classic Home had. Drawn by DashboardRenderer and the
// editor canvas behind the widget, which is inset by `inset()` so a ring or
// a number does not touch the border.
import QtQuick 2.15
import ".." as App

Rectangle {
    // The widget's margin inside a framed cell of this size
    function inset(w, h) {
        return Math.round(Math.min(App.Spacing.dp(6), Math.min(w, h) * 0.06))
    }

    radius: App.Spacing.dp(8)
    color: App.Style.obdBoxBackground
    border.color: Qt.darker(App.Style.obdBarColor, 1.4)
    border.width: 1
}
