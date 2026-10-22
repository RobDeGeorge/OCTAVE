import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../.." as App
import ".."

Item {
    id: widgetRoot
    property int cardSpan: 1

    signal navigateToSubSection(string subSection)

    GridLayout {
        anchors.fill: parent
        columns: widgetRoot.cardSpan >= 2 ? 2 : 1
        columnSpacing: App.Spacing.overallSpacing * 0.4
        rowSpacing: App.Spacing.overallSpacing * 0.25

        WidgetPill {
            label: "Theme"
            value: settingsManager ? settingsManager.themeSetting : ""
        }

        WidgetPill {
            label: "Scale"
            value: settingsManager ? Math.round(settingsManager.uiScale * 100) + "%" : "100%"
        }

        WidgetPill {
            label: "Font"
            value: {
                var f = settingsManager ? (settingsManager.fontSetting || "System Default") : "System Default"
                return f.length > 14 ? f.substring(0, 14) + "\u2026" : f
            }
        }

        SettingsToggle {
            compact: true
            text: "Show Clock"
            font.pixelSize: App.Spacing.overallText * 0.85
            Layout.columnSpan: widgetRoot.cardSpan >= 2 ? 2 : 1
            checked: settingsManager ? settingsManager.showClock : true
            onToggled: {
                if (settingsManager)
                    settingsManager.save_show_clock(checked)
            }
        }

        Item { Layout.fillHeight: true; Layout.columnSpan: widgetRoot.cardSpan >= 2 ? 2 : 1 }

        // Sub-section chips
        Flow {
            Layout.fillWidth: true
            Layout.columnSpan: widgetRoot.cardSpan >= 2 ? 2 : 1
            spacing: App.Spacing.overallSpacing * 0.3

            WidgetChip {
                text: "Layout"
                onClicked: widgetRoot.navigateToSubSection("Layout")
            }

            WidgetChip {
                text: "Appearance"
                onClicked: widgetRoot.navigateToSubSection("Appearance")
            }

            WidgetChip {
                text: "Clock"
                onClicked: widgetRoot.navigateToSubSection("Clock")
            }
        }
    }
}
