// EnvironmentTheme.qml
pragma Singleton
import QtQuick 2.15

// Structural style tokens (radii, borders, spacing flags) consumed across the
// settings component library via `EnvironmentTheme.active.*`.
//
// OCTAVE once shipped multiple selectable "environments" (Spacecraft, Deep Sea)
// with their own canvas effect layers. Those presets were never finished and
// the picker was removed, so only the single "Standard" look remains. This is
// kept as a singleton exposing one fixed `active` object so the ~40 existing
// `EnvironmentTheme.active.X` bindings keep working unchanged. If multiple
// environments are ever revived, restore the map + a notifyable current-env
// property here; the old presets live in git history.
QtObject {
    readonly property var active: ({
        // Hub Card
        hubCardRadius: 12,

        // Card
        cardRadius: 12,
        cornerBrackets: false,
        accentBorder: false,
        accentBorderOpacity: 0,

        // Divider
        dividerAnimated: false,
        dividerGradient: false,
        dividerHeight: 1,

        // Button
        buttonRadius: 6,
        buttonGlowBorder: false,
        buttonSolidFill: true,

        // Slider
        sliderTickMarks: false,
        sliderHandleGlow: false,
        sliderHandleRadius: -1,

        // Switch
        switchRadius: 8,
        switchKnobRadius: 6,

        // Toggle
        toggleTrackRadius: 10,
        toggleHandleRadius: 8,
        toggleRectShadow: false,

        // Labels & Text
        labelUppercase: false,
        labelLetterSpacing: 0,

        // Value Display
        valueDisplayBrackets: false,
        valueAccentColor: false,

        // Sidebar / Nav
        sidebarGrid: false,
        navItemRadius: 8,

        // Chips
        chipRadius: 8,
        chipAccentBorder: false,

        // Section Header
        sectionHeaderLines: false,

        // TextField
        textFieldRadius: 4,
        textFieldCornerMarks: false,

        // Dropdown
        dropdownRadius: 4,

        // Checkbox
        checkboxRadius: 4,

        // Segmented Control
        segmentGlowBar: false,

        // Radio
        radioSquare: false,

        // Terminal
        terminalRadius: 4,
        terminalAccentBorder: false,
        terminalCornerBrackets: false,
        terminalScanlines: false,
        terminalHeaderAccent: false,
        terminalAccentScroll: false,

        // Enhanced effects
        scanlineOverlay: false,
        pulsingElements: false,
        contentHudLines: false,
        dividerDiamondRotate: false,

        // Deep Sea effects
        sidebarBubbles: false,
        contentSonar: false,
        dividerSonarPing: false,
        cardGlassEffect: false
    })
}
