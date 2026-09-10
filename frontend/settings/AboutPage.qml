import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import ".." as App

Flickable {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    contentWidth: width
    contentHeight: settingsContent.implicitHeight
    flickableDirection: Flickable.VerticalFlick
    clip: true
    boundsBehavior: Flickable.DragAndOvershootBounds
    flickDeceleration: 1200
    maximumFlickVelocity: 4000
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }

    ColumnLayout {
        id: settingsContent
        width: parent.width
        spacing: 0

        GridLayout {
            Layout.fillWidth: true
            Layout.margins: App.Spacing.overallMargin * 2
            columns: 1
            rowSpacing: App.Spacing.overallSpacing * 2

            Item {
                Layout.fillWidth: true
                width: parent.width
                height: dp(100)

                Text {
                    id: glowText
                    text: "OCTAVE"
                    font.pixelSize: App.Spacing.overallText * 5
                    font.family: App.Style.fontFamily
                    font.bold: true
                    color: App.Style.primaryTextColor
                    opacity: 0.5
                    anchors.centerIn: parent
                    scale: 1.05
                    z: 0

                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.3; to: 0.7; duration: 600; easing.type: Easing.InOutQuad }
                        NumberAnimation { from: 0.7; to: 0.3; duration: 600; easing.type: Easing.InOutQuad }
                    }
                }

                Text {
                    id: titleText
                    text: "OCTAVE"
                    font.pixelSize: App.Spacing.overallText * 5
                    font.family: App.Style.fontFamily
                    font.bold: true
                    color: App.Style.accent
                    anchors.centerIn: parent
                    z: 1
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: commitInfoColumn.implicitHeight + App.Spacing.overallSpacing * 2
                color: Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3)
                radius: App.Spacing.overallRadius

                ColumnLayout {
                    id: commitInfoColumn
                    anchors.fill: parent
                    anchors.margins: App.Spacing.overallSpacing
                    spacing: App.Spacing.overallSpacing * 0.5

                    Text {
                        text: "Latest Build"
                        color: App.Style.accent
                        font.pixelSize: App.Spacing.overallText * 0.9
                        font.family: App.Style.fontFamily
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing

                        Text {
                            text: "Commit:"
                            color: App.Style.secondaryTextColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: App.Style.fontFamily
                        }
                        Text {
                            text: settingsManager ? settingsManager.get_git_commit_hash() : "unknown"
                            color: App.Style.primaryTextColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: App.Style.fontFamily
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing

                        Text {
                            text: "Date:"
                            color: App.Style.secondaryTextColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: App.Style.fontFamily
                        }
                        Text {
                            text: settingsManager ? settingsManager.get_git_commit_date() : "unknown"
                            color: App.Style.primaryTextColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: App.Style.fontFamily
                        }
                    }

                    Text {
                        text: settingsManager ? settingsManager.get_git_commit_message() : ""
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText * 0.8
                        font.family: App.Style.fontFamily
                        font.italic: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        visible: text !== ""
                    }
                }
            }

            // ─── Diagnostics: read and get the logs out without a laptop ───
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: diagColumn.implicitHeight + App.Spacing.overallSpacing * 2
                color: Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3)
                radius: App.Spacing.overallRadius
                visible: typeof diagnosticsManager !== "undefined"

                ColumnLayout {
                    id: diagColumn
                    anchors.fill: parent
                    anchors.margins: App.Spacing.overallSpacing
                    spacing: App.Spacing.overallSpacing * 0.5

                    property string status: ""

                    Text {
                        text: "Diagnostics"
                        color: App.Style.accent
                        font.pixelSize: App.Spacing.overallText * 0.9
                        font.family: App.Style.fontFamily
                        font.bold: true
                        Layout.fillWidth: true
                    }

                    // Buttons on their own row so they can never squeeze the title;
                    // maximumWidth keeps a long label from widening the card.
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.maximumWidth: diagColumn.width
                        spacing: App.Spacing.overallSpacing

                        SettingsButton {
                            text: "Refresh"
                            height: dp(30)
                            onClicked: logTail.text = diagnosticsManager.recentLogLines(200)
                        }
                        SettingsButton {
                            text: "Copy"
                            tooltipText: "Copy the recent log lines and device info to the clipboard"
                            height: dp(30)
                            onClicked: diagColumn.status = diagnosticsManager.copyLogsToClipboard(400)
                                                            ? "Copied to clipboard" : "Copy failed"
                        }
                        SettingsButton {
                            text: "Export logs"
                            tooltipText: "Copy every log file into your Downloads folder, ready to attach to an email"
                            height: dp(30)
                            onClicked: {
                                var p = diagnosticsManager.exportLogs()
                                diagColumn.status = p !== "" ? "Exported to " + p : "Export failed"
                            }
                        }
                        SettingsButton {
                            text: "Open folder"
                            height: dp(30)
                            visible: diagnosticsManager.canOpenFolder
                            onClicked: diagnosticsManager.openLogFolder()
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        text: "OCTAVE " + diagnosticsManager.appVersion + " (" + diagnosticsManager.backendName + " backend)  \u2022  "
                              + diagnosticsManager.deviceInfo + "\nLogs: " + diagnosticsManager.logDir
                        color: App.Style.secondaryTextColor
                        font.pixelSize: App.Spacing.overallText * 0.8
                        font.family: App.Style.fontFamily
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0   // wrap to the card, never widen it
                    }

                    Text {
                        text: diagColumn.status
                        visible: text !== ""
                        color: App.Style.primaryTextColor
                        font.pixelSize: App.Spacing.overallText * 0.8
                        font.family: App.Style.fontFamily
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                    }

                    // Last lines of the main log (plus the error log); newest at the bottom
                    Flickable {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        Layout.preferredHeight: dp(180)
                        contentWidth: width
                        contentHeight: logTail.implicitHeight
                        clip: true
                        flickableDirection: Flickable.VerticalFlick
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        Component.onCompleted: contentY = Math.max(0, contentHeight - height)

                        TextEdit {
                            id: logTail
                            width: parent.width
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            color: App.Style.primaryTextColor
                            font.family: "monospace"
                            font.pixelSize: App.Spacing.overallText * 0.7
                            text: diagnosticsManager.recentLogLines(200)
                        }
                    }
                }
            }

            // ─── Check for Updates ───
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: updateColumn.implicitHeight + App.Spacing.overallSpacing * 2
                color: Qt.rgba(App.Style.hoverColor.r, App.Style.hoverColor.g, App.Style.hoverColor.b, 0.3)
                radius: App.Spacing.overallRadius

                ColumnLayout {
                    id: updateColumn
                    anchors.fill: parent
                    anchors.margins: App.Spacing.overallSpacing
                    spacing: App.Spacing.overallSpacing * 0.5

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing

                        Text {
                            text: "Software Updates"
                            color: App.Style.accent
                            font.pixelSize: App.Spacing.overallText * 0.9
                            font.family: App.Style.fontFamily
                            font.bold: true
                            Layout.fillWidth: true
                        }

                        SettingsButton {
                            id: checkUpdateButton
                            text: networkManager && networkManager.updateStatus === "checking" ? "Checking..." : "Check for Updates"
                            height: dp(30)
                            enabled: !networkManager || networkManager.updateStatus !== "checking"
                            opacity: enabled ? 1.0 : 0.6
                            onClicked: {
                                if (networkManager)
                                    networkManager.checkForUpdates()
                            }

                            // Update notification dot
                            Rectangle {
                                width: dp(7)
                                height: dp(7)
                                radius: width / 2
                                color: "#FF9800"
                                z: 10
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.topMargin: dp(-2)
                                anchors.rightMargin: dp(-2)
                                visible: networkManager && networkManager.updateStatus === "update-available"

                                SequentialAnimation on opacity {
                                    running: networkManager && networkManager.updateStatus === "update-available"
                                    loops: Animation.Infinite
                                    NumberAnimation { from: 1.0; to: 0.3; duration: 1200 }
                                    NumberAnimation { from: 0.3; to: 1.0; duration: 1200 }
                                }
                            }
                        }
                    }

                    // Status indicator
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing * 0.5
                        visible: networkManager && networkManager.updateStatus !== ""

                        Rectangle {
                            width: dp(8)
                            height: dp(8)
                            radius: width / 2
                            color: {
                                if (!networkManager) return App.Style.secondaryTextColor
                                switch (networkManager.updateStatus) {
                                    case "checking":    return App.Style.accent
                                    case "up-to-date":  return "#4CAF50"
                                    case "update-available": return "#FF9800"
                                    // Running a source checkout that is ahead of / diverged from
                                    // origin/main is normal for development, not a fault.
                                    case "ahead":       return App.Style.accent
                                    case "diverged":    return "#FF9800"
                                    case "error":       return "#F44336"
                                    default:            return App.Style.secondaryTextColor
                                }
                            }

                            // Pulse animation when checking
                            SequentialAnimation on opacity {
                                running: networkManager && networkManager.updateStatus === "checking"
                                loops: Animation.Infinite
                                NumberAnimation { from: 1.0; to: 0.3; duration: 500 }
                                NumberAnimation { from: 0.3; to: 1.0; duration: 500 }
                            }
                        }

                        Text {
                            text: networkManager ? networkManager.updateMessage : ""
                            color: App.Style.primaryTextColor
                            font.pixelSize: App.Spacing.overallText * 0.85
                            font.family: App.Style.fontFamily
                            Layout.fillWidth: true
                        }
                    }

                    // Show remote commit info when update is available
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: App.Spacing.overallSpacing * 0.3
                        visible: networkManager && networkManager.updateStatus === "update-available"

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: App.Spacing.overallSpacing

                            Text {
                                text: "Latest:"
                                color: App.Style.secondaryTextColor
                                font.pixelSize: App.Spacing.overallText * 0.8
                                font.family: App.Style.fontFamily
                            }
                            Text {
                                text: networkManager ? networkManager.remoteCommit : ""
                                color: App.Style.primaryTextColor
                                font.pixelSize: App.Spacing.overallText * 0.8
                                font.family: App.Style.fontFamily
                            }
                        }

                        Text {
                            text: networkManager ? networkManager.remoteCommitMsg : ""
                            color: App.Style.secondaryTextColor
                            font.pixelSize: App.Spacing.overallText * 0.8
                            font.family: App.Style.fontFamily
                            font.italic: true
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            visible: text !== ""
                        }

                        // ─── Update Action Area ───
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: App.Spacing.overallSpacing * 0.3
                            spacing: App.Spacing.overallSpacing * 0.5

                            // Uncommitted work present — self-update would hard-reset over it,
                            // so explain the situation instead of offering the button below.
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: App.Spacing.overallSpacing * 0.5
                                visible: networkManager && networkManager.canSelfUpdate
                                         && networkManager.workingTreeDirty

                                Rectangle {
                                    width: dp(8); height: dp(8); radius: width / 2
                                    color: "#FF9800"
                                }

                                Text {
                                    text: "Uncommitted changes in the working tree — commit or stash them to enable updating"
                                    color: "#FF9800"
                                    font.pixelSize: App.Spacing.overallText * 0.75
                                    font.family: App.Style.fontFamily
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }

                            // "Update Now" button with confirmation (when git self-update is available)
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: App.Spacing.overallSpacing
                                visible: networkManager && networkManager.canSelfUpdate
                                         && !networkManager.workingTreeDirty
                                         && networkManager.selfUpdateStatus !== "fetching"
                                         && networkManager.selfUpdateStatus !== "restart-required"
                                         && networkManager.selfUpdateStatus !== "error"

                                property bool confirming: false

                                Timer {
                                    id: updateConfirmResetTimer
                                    interval: 5000
                                    onTriggered: parent.confirming = false
                                }

                                SettingsButton {
                                    text: parent.confirming ? "Confirm Update" : "Update Now"
                                    buttonColor: parent.confirming ? "#FF9800" : App.Style.accent
                                    height: dp(30)
                                    onClicked: {
                                        if (parent.confirming) {
                                            parent.confirming = false
                                            networkManager.applySelfUpdate()
                                        } else {
                                            parent.confirming = true
                                            updateConfirmResetTimer.restart()
                                        }
                                    }
                                }

                                Text {
                                    text: "This will overwrite local changes"
                                    color: "#FF9800"
                                    font.pixelSize: App.Spacing.overallText * 0.75
                                    font.family: App.Style.fontFamily
                                    visible: parent.confirming
                                    Layout.alignment: Qt.AlignVCenter

                                    SequentialAnimation on opacity {
                                        running: parent.parent.confirming
                                        loops: Animation.Infinite
                                        NumberAnimation { from: 1.0; to: 0.4; duration: 800 }
                                        NumberAnimation { from: 0.4; to: 1.0; duration: 800 }
                                    }
                                }
                            }

                            // Progress indicator during fetch/apply
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: App.Spacing.overallSpacing * 0.5
                                visible: networkManager && networkManager.selfUpdateStatus === "fetching"

                                Rectangle {
                                    width: dp(8)
                                    height: dp(8)
                                    radius: width / 2
                                    color: App.Style.accent

                                    SequentialAnimation on opacity {
                                        running: networkManager && networkManager.selfUpdateStatus === "fetching"
                                        loops: Animation.Infinite
                                        NumberAnimation { from: 1.0; to: 0.3; duration: 500 }
                                        NumberAnimation { from: 0.3; to: 1.0; duration: 500 }
                                    }
                                }

                                Text {
                                    text: networkManager ? networkManager.selfUpdateMessage : ""
                                    color: App.Style.primaryTextColor
                                    font.pixelSize: App.Spacing.overallText * 0.85
                                    font.family: App.Style.fontFamily
                                }
                            }

                            // Error state with retry
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: App.Spacing.overallSpacing * 0.5
                                visible: networkManager && networkManager.selfUpdateStatus === "error"

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: App.Spacing.overallSpacing * 0.5

                                    Rectangle {
                                        width: dp(8)
                                        height: dp(8)
                                        radius: width / 2
                                        color: "#F44336"
                                    }

                                    Text {
                                        text: networkManager ? networkManager.selfUpdateMessage : "Update failed"
                                        color: "#F44336"
                                        font.pixelSize: App.Spacing.overallText * 0.8
                                        font.family: App.Style.fontFamily
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                SettingsButton {
                                    text: "Retry Update"
                                    buttonColor: "#FF9800"
                                    height: dp(30)
                                    onClicked: {
                                        if (networkManager)
                                            networkManager.applySelfUpdate()
                                    }
                                }
                            }

                            // Success + Restart button
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: App.Spacing.overallSpacing * 0.5
                                visible: networkManager && networkManager.selfUpdateStatus === "restart-required"

                                RowLayout {
                                    spacing: App.Spacing.overallSpacing * 0.5

                                    Rectangle {
                                        width: dp(8)
                                        height: dp(8)
                                        radius: width / 2
                                        color: "#4CAF50"
                                    }

                                    Text {
                                        text: networkManager ? networkManager.selfUpdateMessage : "Update complete"
                                        color: "#4CAF50"
                                        font.pixelSize: App.Spacing.overallText * 0.85
                                        font.family: App.Style.fontFamily
                                    }
                                }

                                SettingsButton {
                                    text: "Restart Now"
                                    buttonColor: "#4CAF50"
                                    height: dp(30)
                                    onClicked: {
                                        if (networkManager)
                                            networkManager.restartApp()
                                    }
                                }
                            }

                            // Fallback for non-git environments (PyInstaller builds)
                            Text {
                                text: "Pull the latest changes from GitHub to update."
                                color: App.Style.secondaryTextColor
                                font.pixelSize: App.Spacing.overallText * 0.75
                                font.family: App.Style.fontFamily
                                Layout.fillWidth: true
                                visible: !networkManager || !networkManager.canSelfUpdate
                            }
                        }
                    }
                }
            }

            Text {
                id: descriptionText
                text: "Welcome to OCTAVE, an open-source, cross-platform telematics system for an augmented vehicle experience. Developed by Way Better Solutions, our mission is simple: we make things better.

This software is designed to provide a seamless interface for vehicle systems, media playback, navigation, and more."
                wrapMode: Text.WordWrap
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText
                font.family: App.Style.fontFamily
                Layout.fillWidth: true
                Layout.topMargin: App.Spacing.overallSpacing
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: App.Style.hoverColor
                Layout.topMargin: App.Spacing.overallSpacing
                Layout.bottomMargin: App.Spacing.overallSpacing
            }

            Text {
                text: "GitHub Repository"
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText * 1.2
                font.family: App.Style.fontFamily
                font.bold: true
                Layout.fillWidth: true
            }

            Text {
                text: "<a href='https://github.com/WayBetterSolutions/OCTAVE'>github.com/WayBetterSolutions/OCTAVE</a>"
                color: App.Style.accent
                linkColor: App.Style.accent
                font.pixelSize: App.Spacing.overallText
                font.family: App.Style.fontFamily
                Layout.fillWidth: true
                onLinkActivated: Qt.openUrlExternally(link)
            }

            Text {
                text: "2026 Way Better Solutions"
                color: App.Style.primaryTextColor
                font.pixelSize: App.Spacing.overallText
                font.family: App.Style.fontFamily
                Layout.fillWidth: true
                Layout.topMargin: App.Spacing.overallSpacing
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: App.Spacing.overallSpacing * 4
            }
        }
    }
}
