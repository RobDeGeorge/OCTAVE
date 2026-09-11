import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../.." as App
import ".."

Item {
    id: widgetRoot
    property int cardSpan: 1

    signal navigateToSubSection(string subSection)

    property bool isSpotify: settingsManager ? settingsManager.mediaSource === "spotify" : false
    property color sourceStatusColor: {
        if (!isSpotify) return "transparent"
        if (typeof spotifyManager !== "undefined" && spotifyManager && spotifyManager.is_connected())
            return App.Style.statusConnected
        return App.Style.statusDisconnected
    }

    GridLayout {
        anchors.fill: parent
        columns: widgetRoot.cardSpan >= 2 ? 2 : 1
        columnSpacing: App.Spacing.overallSpacing * 0.4
        rowSpacing: App.Spacing.overallSpacing * 0.25

        WidgetPill {
            label: "Source"
            value: widgetRoot.isSpotify ? "Spotify" : "Local"
            statusColor: widgetRoot.sourceStatusColor
        }

        SettingsToggle {
            compact: true
            text: "Autoplay"
            font.pixelSize: App.Spacing.overallText * 0.85
            checked: settingsManager ? settingsManager.autoPlayOnStartup : false
            onToggled: {
                if (settingsManager)
                    settingsManager.save_auto_play_on_startup(checked)
            }
        }

        SettingsToggle {
            compact: true
            text: "Visualizer"
            font.pixelSize: App.Spacing.overallText * 0.85
            checked: settingsManager ? settingsManager.showWaveformVisualizer : false
            onToggled: {
                if (settingsManager)
                    settingsManager.save_show_waveform_visualizer(checked)
            }
        }

        SettingsToggle {
            compact: true
            text: "3D Preview"
            font.pixelSize: App.Spacing.overallText * 0.85
            checked: settingsManager ? settingsManager.show3DAlbumPreview : false
            onToggled: {
                if (settingsManager)
                    settingsManager.save_show_3d_album_preview(checked)
            }
        }

        Item { Layout.fillHeight: true; Layout.columnSpan: widgetRoot.cardSpan >= 2 ? 2 : 1 }

        // Sub-section chips
        Flow {
            Layout.fillWidth: true
            Layout.columnSpan: widgetRoot.cardSpan >= 2 ? 2 : 1
            spacing: App.Spacing.overallSpacing * 0.3

            WidgetChip {
                text: "Library"
                onClicked: widgetRoot.navigateToSubSection("Library")
            }

            WidgetChip {
                text: "Playback"
                onClicked: widgetRoot.navigateToSubSection("Playback")
            }

            WidgetChip {
                text: "Now Playing"
                onClicked: widgetRoot.navigateToSubSection("Now Playing")
            }

            WidgetChip {
                text: "Spotify"
                onClicked: widgetRoot.navigateToSubSection("Spotify")
            }
        }
    }
}
