// Style.qml
pragma Singleton
import QtQuick 2.15

QtObject {
    // Base URL for frontend/assets/. Resolved here, in frontend/, so callers in
    // any subdirectory can build an icon path without guessing how many "../"
    // segments they need — settings/ pages and frontend/ layouts both consume
    // the tile icons. Deliberately a property rather than a function: singleton
    // *functions* are unreliable on Android (same Qt bug the local dp() wrappers
    // scattered through the QML work around).
    readonly property url assetBase: Qt.resolvedUrl("assets/")

    property string currentTheme: "SolarizedLight"
    property string currentFont: "System Default"

    // Available fonts (populated from Main.qml after scanning fonts folder)
    property var availableFonts: ["System Default"]

    // Map of font display names to their loaded font family names
    property var fontFamilyMap: ({})

    // System default font family (set by Main.qml at startup)
    property string systemDefaultFont: ""

    // The actual font family to use (resolved from currentFont)
    // Always returns a valid font family name - systemDefaultFont for System Default, custom font family otherwise
    property string fontFamily: currentFont === "System Default" ? systemDefaultFont : (fontFamilyMap[currentFont] || systemDefaultFont)

    // Signal when fonts list is updated
    signal fontsUpdated()

    // Signal when font changes (for real-time updates)
    signal fontChanged()

    // Function to set the current font
    function setFont(fontName) {
        if (availableFonts.indexOf(fontName) !== -1 || fontName === "System Default") {
            currentFont = fontName
            fontChanged()
        }
    }

    // Function to register available fonts (called from Main.qml)
    function registerFonts(fontList, familyMap) {
        availableFonts = ["System Default"].concat(fontList)
        fontFamilyMap = familyMap
        fontsUpdated()
    }

    // This will store our custom themes that are loaded from settings
    property var customThemes: ({})

    // Dynamic album art theme - updated in real-time based on current song
    property var albumArtTheme: null
    property bool isAlbumArtCaptureActive: currentTheme === "Album Art Capture"

    // Define theme palettes (built-in themes)
    readonly property var themes: ({
        "SolarizedLight": {
            "base": "#FDF6E3",
            "baseAlt": "#EEE8D5",
            "accent": "#268BD2",
            "text": {
                "primary": "#073642",
                "secondary": "#586E75"
            },
            "states": {
                "hover": "#E0DAC3",
                "paused": "#D6D0B9",
                "playing": "#CCC6AF"
            },
            "sliders": {
                "volume": "#268BD2",
                "media": "#586E75",
                "settings": "#268BD2"
            },
            "bottombar": {
                "previous": "#268BD2",
                "play": "#268BD2",
                "pause": "#268BD2",
                "next": "#268BD2",
                "volume": "#268BD2",
                "shuffle": "#268BD2",
                "toggleShade": "#E0DAC3",
                "homeButton": "#268BD2",
                "obdButton": "#268BD2",
                "mediaButton": "#268BD2",
                "sensorButton": "#268BD2",
                "settingsButton": "#268BD2",
                "androidAutoButton": "#268BD2",
                "phoneMirrorButton": "#268BD2"
            },
            "mediaroom": {
                "previous": "#2AA198",
                "play": "#2AA198",
                "pause": "#2AA198",
                "next": "#2AA198",
                "left": "#2AA198",
                "right": "#2AA198",
                "shuffle": "#2AA198",
                "toggleShade": "#D6D0B9"
            },
            "mainmenu": {
                "mediaContainer": "#9d2aa1"
            },
            "obd": {
                "boxBackground": "#EEE8D5",
                "barColor": "#2AA198"
            }
        },

        "NeonMatrix": {
            "base": "#0C0C0C",
            "baseAlt": "#161616",
            "accent": "#00FF41",
            "text": {
                "primary": "#CCFFCC",
                "secondary": "#88CC88"
            },
            "states": {
                "hover": "#202020",
                "paused": "#1C1C1C",
                "playing": "#282828"
            },
            "sliders": {
                "volume": "#00FF41",
                "media": "#88CC88",
                "settings": "#00FF41"
            },
            "bottombar": {
                "previous": "#00FF41",
                "play": "#00FF41",
                "pause": "#00FF41",
                "next": "#00FF41",
                "volume": "#00FF41",
                "shuffle": "#00FF41",
                "toggleShade": "#202020",
                "homeButton": "#00FF41",
                "obdButton": "#00FF41",
                "mediaButton": "#00FF41",
                "sensorButton": "#00FF41",
                "settingsButton": "#00FF41",
                "androidAutoButton": "#00FF41",
                "phoneMirrorButton": "#00FF41"
            },
            "mediaroom": {
                "previous": "#33FF66",
                "play": "#33FF66",
                "pause": "#33FF66",
                "next": "#33FF66",
                "left": "#33FF66",
                "right": "#33FF66",
                "shuffle": "#33FF66",
                "toggleShade": "#1C1C1C"
            },
            "mainmenu": {
                "mediaContainer": "#005500"
            },
            "obd": {
                "boxBackground": "#161616",
                "barColor": "#33FF66"
            }
        },

        "CosmicVoyager": {
        "base": "#0A0E17",
        "baseAlt": "#151C29",
        "accent": "#00BFFF",
        "text": {
            "primary": "#E0F2FF",
            "secondary": "#A0C2E0"
        },
        "states": {
            "hover": "#1E2A3D",
            "paused": "#192433",
            "playing": "#243143"
        },
        "sliders": {
            "volume": "#00BFFF", // Bright blue for primary volume
            "media": "#FF5E94",  // Contrast with pink for media
            "settings": "#7CB9E8" // Softer blue for settings
        },
        "bottombar": {
            "previous": "#7D9EC0", // Lighter blue
            "play": "#00CCFF",    // Bright cyan for emphasis
            "pause": "#00CCFF",   // Match play
            "next": "#7D9EC0",    // Match previous
            "volume": "#00BFFF",  // Match accent
            "shuffle": "#FF5E94", // Pink for distinction
            "toggleShade": "#1E2A3D",
            "homeButton": "#00BFFF", // Match accent
            "obdButton": "#FF8C42",  // Orange for OBD
            "mediaButton": "#00E673", // Green for media
            "sensorButton": "#FFD24D", // Amber for sensors
            "settingsButton": "#7CB9E8", // Match settings slider
            "androidAutoButton": "#7CB9E8",
            "phoneMirrorButton": "#7CB9E8"
        },
        "mediaroom": {
            "previous": "#7D9EC0",
            "play": "#00E0FF", // Brighter than bottombar
            "pause": "#00E0FF",
            "next": "#7D9EC0",
            "left": "#4DA6FF",
            "right": "#4DA6FF",
            "shuffle": "#FF5E94",
            "toggleShade": "#192433"
        },
        "mainmenu": {
            "mediaContainer": "#004080" // Darker blue for container
        },
        "obd": {
            "boxBackground": "#151C29",
            "barColor": "#FF8C42" // Orange matches OBD button
        }
    },

    "TechnoRetro": {
        "base": "#1E1014",
        "baseAlt": "#2A1B22",
        "accent": "#FF00AA",
        "text": {
            "primary": "#F5E0FF",
            "secondary": "#CAA0D4"
        },
        "states": {
            "hover": "#3D2A33",
            "paused": "#33222B",
            "playing": "#482F3A"
        },
        "sliders": {
            "volume": "#FF00AA", // Hot pink for volume
            "media": "#00FFCC",  // Teal for media (contrasting)
            "settings": "#D436FF" // Purple for settings
        },
        "bottombar": {
            "previous": "#B347B9", // Muted purple
            "play": "#FF00AA",    // Hot pink for emphasis
            "pause": "#FF00AA",   // Match play
            "next": "#B347B9",    // Match previous
            "volume": "#D436FF",  // Purple accent
            "shuffle": "#00FFCC", // Teal for distinction
            "toggleShade": "#3D2A33",
            "homeButton": "#FF47B9", // Lighter pink
            "obdButton": "#FFDD00",  // Yellow for OBD
            "mediaButton": "#00FFCC", // Teal for media
            "sensorButton": "#FF66E0", // Bright pink for sensors
            "settingsButton": "#D436FF", // Match settings slider
            "androidAutoButton": "#D436FF",
            "phoneMirrorButton": "#D436FF"
        },
        "mediaroom": {
            "previous": "#B347B9",
            "play": "#FF47B9", // Lighter play button in media room
            "pause": "#FF47B9",
            "next": "#B347B9",
            "left": "#E566FF", // Light purple
            "right": "#E566FF",
            "shuffle": "#00FFCC",
            "toggleShade": "#33222B"
        },
        "mainmenu": {
            "mediaContainer": "#990066" // Darker pink for container
        },
        "obd": {
            "boxBackground": "#2A1B22",
            "barColor": "#FFDD00" // Yellow matches OBD button
        }
    },

    "AutumnCascade": {
        "base": "#2D1E12",
        "baseAlt": "#3E2918",
        "accent": "#E67E22",
        "text": {
            "primary": "#FDF2E9",
            "secondary": "#D5BBA8"
        },
        "states": {
            "hover": "#4E3923",
            "paused": "#44321E",
            "playing": "#5A4329"
        },
        "sliders": {
            "volume": "#E67E22", // Orange for volume
            "media": "#8D6E63",  // Woody brown for media
            "settings": "#F5B041" // Golden for settings
        },
        "bottombar": {
            "previous": "#BA6B40", // Rusty orange
            "play": "#FF9F45",    // Brighter orange for play
            "pause": "#FF9F45",   // Match play
            "next": "#BA6B40",    // Match previous
            "volume": "#E67E22",  // Match accent
            "shuffle": "#5D4037", // Dark brown for shuffle
            "toggleShade": "#4E3923",
            "homeButton": "#F5B041", // Golden
            "obdButton": "#A04000",  // Deep rust for OBD
            "mediaButton": "#D35400", // Burnt orange for media
            "sensorButton": "#E67E22", // Pumpkin for sensors
            "settingsButton": "#F39C12", // Match settings slider
            "androidAutoButton": "#F39C12",
            "phoneMirrorButton": "#F39C12"
        },
        "mediaroom": {
            "previous": "#BA6B40", 
            "play": "#FFA44F", // Even brighter in media room
            "pause": "#FFA44F",
            "next": "#BA6B40",
            "left": "#CD853F", // Lighter brown
            "right": "#CD853F",
            "shuffle": "#5D4037",
            "toggleShade": "#44321E"
        },
        "mainmenu": {
            "mediaContainer": "#7E5109" // Deep amber for container
        },
        "obd": {
            "boxBackground": "#3E2918",
            "barColor": "#A04000" // Match OBD button
        }
    },

    "QuantumNebula": {
        "base": "#0F0F1A",
        "baseAlt": "#17172A",
        "accent": "#9C27B0", // Purple accent
        "text": {
            "primary": "#F0E6FF",
            "secondary": "#B5A4D4"
        },
        "states": {
            "hover": "#24243F",
            "paused": "#1F1F37",
            "playing": "#2D2D4A"
        },
        "sliders": {
            "volume": "#9C27B0", // Purple for volume
            "media": "#00BCD4",  // Cyan for media
            "settings": "#673AB7" // Deeper purple for settings
        },
        "bottombar": {
            "previous": "#4A148C", // Dark purple for previous
            "play": "#9C27B0",    // Purple for play
            "pause": "#9C27B0",   // Match play
            "next": "#4A148C",    // Match previous
            "volume": "#673AB7",  // Violet for volume
            "shuffle": "#00BCD4", // Cyan for shuffle
            "toggleShade": "#24243F",
            "homeButton": "#7B1FA2", // Medium purple for home
            "obdButton": "#00E5FF",  // Bright cyan for OBD
            "mediaButton": "#00BCD4", // Regular cyan for media
            "sensorButton": "#E040FB", // Magenta for sensors
            "settingsButton": "#673AB7", // Match settings slider
            "androidAutoButton": "#673AB7",
            "phoneMirrorButton": "#673AB7"
        },
        "mediaroom": {
            "previous": "#4A148C",
            "play": "#BA68C8", // Lighter purple 
            "pause": "#BA68C8",
            "next": "#4A148C",
            "left": "#7C4DFF", // Indigo variant
            "right": "#7C4DFF",
            "shuffle": "#00BCD4", // Cyan
            "toggleShade": "#1F1F37"
        },
        "mainmenu": {
            "mediaContainer": "#4A0072" // Deep purple container
        },
        "obd": {
            "boxBackground": "#17172A",
            "barColor": "#00E5FF" // Match OBD button
        }
    },

    "CrimsonEmber": {
        "base": "#1A0808",
        "baseAlt": "#241010",
        "accent": "#DC2626",
        "text": {
            "primary": "#FFE4E4",
            "secondary": "#C49595"
        },
        "states": {
            "hover": "#361414",
            "paused": "#2D0F0F",
            "playing": "#421A1A"
        },
        "sliders": {
            "volume": "#DC2626",
            "media": "#F59E0B",
            "settings": "#B91C1C"
        },
        "bottombar": {
            "previous": "#B91C1C",
            "play": "#DC2626",
            "pause": "#DC2626",
            "next": "#B91C1C",
            "volume": "#DC2626",
            "shuffle": "#F59E0B",
            "toggleShade": "#361414",
            "homeButton": "#DC2626",
            "obdButton": "#F59E0B",
            "mediaButton": "#FB923C",
            "sensorButton": "#FBBF24",
            "settingsButton": "#B91C1C",
            "androidAutoButton": "#B91C1C",
            "phoneMirrorButton": "#B91C1C"
        },
        "mediaroom": {
            "previous": "#B91C1C",
            "play": "#EF4444",
            "pause": "#EF4444",
            "next": "#B91C1C",
            "left": "#F87171",
            "right": "#F87171",
            "shuffle": "#F59E0B",
            "toggleShade": "#2D0F0F"
        },
        "mainmenu": {
            "mediaContainer": "#7F1D1D"
        },
        "obd": {
            "boxBackground": "#241010",
            "barColor": "#F59E0B"
        }
    },

    "ForestEmerald": {
        "base": "#0A1A14",
        "baseAlt": "#122620",
        "accent": "#10B981",
        "text": {
            "primary": "#D1FAE5",
            "secondary": "#87B5A1"
        },
        "states": {
            "hover": "#1E3A2C",
            "paused": "#1A3025",
            "playing": "#244032"
        },
        "sliders": {
            "volume": "#10B981",
            "media": "#FBBF24",
            "settings": "#059669"
        },
        "bottombar": {
            "previous": "#059669",
            "play": "#10B981",
            "pause": "#10B981",
            "next": "#059669",
            "volume": "#10B981",
            "shuffle": "#FBBF24",
            "toggleShade": "#1E3A2C",
            "homeButton": "#10B981",
            "obdButton": "#FBBF24",
            "mediaButton": "#34D399",
            "sensorButton": "#FCD34D",
            "settingsButton": "#059669",
            "androidAutoButton": "#059669",
            "phoneMirrorButton": "#059669"
        },
        "mediaroom": {
            "previous": "#059669",
            "play": "#34D399",
            "pause": "#34D399",
            "next": "#059669",
            "left": "#6EE7B7",
            "right": "#6EE7B7",
            "shuffle": "#FBBF24",
            "toggleShade": "#1A3025"
        },
        "mainmenu": {
            "mediaContainer": "#064E3B"
        },
        "obd": {
            "boxBackground": "#122620",
            "barColor": "#FBBF24"
        }
    },

    "ArcticChill": {
        "base": "#F0F4F8",
        "baseAlt": "#DCE5EE",
        "accent": "#0EA5E9",
        "text": {
            "primary": "#0F2942",
            "secondary": "#5B7A95"
        },
        "states": {
            "hover": "#C4D4E2",
            "paused": "#CDDCE8",
            "playing": "#B7CADC"
        },
        "sliders": {
            "volume": "#0EA5E9",
            "media": "#7C3AED",
            "settings": "#0284C7"
        },
        "bottombar": {
            "previous": "#0284C7",
            "play": "#0EA5E9",
            "pause": "#0EA5E9",
            "next": "#0284C7",
            "volume": "#0EA5E9",
            "shuffle": "#7C3AED",
            "toggleShade": "#C4D4E2",
            "homeButton": "#0EA5E9",
            "obdButton": "#7C3AED",
            "mediaButton": "#06B6D4",
            "sensorButton": "#A78BFA",
            "settingsButton": "#0284C7",
            "androidAutoButton": "#0284C7",
            "phoneMirrorButton": "#0284C7"
        },
        "mediaroom": {
            "previous": "#0284C7",
            "play": "#38BDF8",
            "pause": "#38BDF8",
            "next": "#0284C7",
            "left": "#7DD3FC",
            "right": "#7DD3FC",
            "shuffle": "#7C3AED",
            "toggleShade": "#CDDCE8"
        },
        "mainmenu": {
            "mediaContainer": "#BFDBFE"
        },
        "obd": {
            "boxBackground": "#DCE5EE",
            "barColor": "#7C3AED"
        }
    },

    "AmberConsole": {
        "base": "#000000",
        "baseAlt": "#0A0A0A",
        "accent": "#FFB000",
        "text": {
            "primary": "#FFE89C",
            "secondary": "#C49B5C"
        },
        "states": {
            "hover": "#1A1500",
            "paused": "#150F00",
            "playing": "#261F00"
        },
        "sliders": {
            "volume": "#FFB000",
            "media": "#FF8C00",
            "settings": "#CC8C00"
        },
        "bottombar": {
            "previous": "#CC8C00",
            "play": "#FFB000",
            "pause": "#FFB000",
            "next": "#CC8C00",
            "volume": "#FFB000",
            "shuffle": "#FF8C00",
            "toggleShade": "#1A1500",
            "homeButton": "#FFB000",
            "obdButton": "#FF8C00",
            "mediaButton": "#FFD24D",
            "sensorButton": "#FFCC33",
            "settingsButton": "#CC8C00",
            "androidAutoButton": "#CC8C00",
            "phoneMirrorButton": "#CC8C00"
        },
        "mediaroom": {
            "previous": "#CC8C00",
            "play": "#FFC933",
            "pause": "#FFC933",
            "next": "#CC8C00",
            "left": "#FFD966",
            "right": "#FFD966",
            "shuffle": "#FF8C00",
            "toggleShade": "#150F00"
        },
        "mainmenu": {
            "mediaContainer": "#332300"
        },
        "obd": {
            "boxBackground": "#0A0A0A",
            "barColor": "#FF8C00"
        }
    },

    "Parchment": {
        "base": "#F5EBD8",
        "baseAlt": "#E8DBC1",
        "accent": "#8B4513",
        "text": {
            "primary": "#2A1A0A",
            "secondary": "#6B4F2E"
        },
        "states": {
            "hover": "#D6C5A4",
            "paused": "#DDCDAE",
            "playing": "#CCB994"
        },
        "sliders": {
            "volume": "#8B4513",
            "media": "#B45309",
            "settings": "#78350F"
        },
        "bottombar": {
            "previous": "#78350F",
            "play": "#8B4513",
            "pause": "#8B4513",
            "next": "#78350F",
            "volume": "#8B4513",
            "shuffle": "#B45309",
            "toggleShade": "#D6C5A4",
            "homeButton": "#8B4513",
            "obdButton": "#B45309",
            "mediaButton": "#A0522D",
            "sensorButton": "#C2410C",
            "settingsButton": "#78350F",
            "androidAutoButton": "#78350F",
            "phoneMirrorButton": "#78350F"
        },
        "mediaroom": {
            "previous": "#78350F",
            "play": "#A0522D",
            "pause": "#A0522D",
            "next": "#78350F",
            "left": "#C19A6B",
            "right": "#C19A6B",
            "shuffle": "#B45309",
            "toggleShade": "#DDCDAE"
        },
        "mainmenu": {
            "mediaContainer": "#C2A878"
        },
        "obd": {
            "boxBackground": "#E8DBC1",
            "barColor": "#B45309"
        }
    },

    "MintGarden": {
        "base": "#F0F9F4",
        "baseAlt": "#DDF0E2",
        "accent": "#16A34A",
        "text": {
            "primary": "#0F2A1A",
            "secondary": "#4D7B5E"
        },
        "states": {
            "hover": "#C5E2CD",
            "paused": "#CFE7D5",
            "playing": "#B5D6BF"
        },
        "sliders": {
            "volume": "#16A34A",
            "media": "#0891B2",
            "settings": "#15803D"
        },
        "bottombar": {
            "previous": "#15803D",
            "play": "#16A34A",
            "pause": "#16A34A",
            "next": "#15803D",
            "volume": "#16A34A",
            "shuffle": "#0891B2",
            "toggleShade": "#C5E2CD",
            "homeButton": "#16A34A",
            "obdButton": "#0891B2",
            "mediaButton": "#22C55E",
            "sensorButton": "#06B6D4",
            "settingsButton": "#15803D",
            "androidAutoButton": "#15803D",
            "phoneMirrorButton": "#15803D"
        },
        "mediaroom": {
            "previous": "#15803D",
            "play": "#22C55E",
            "pause": "#22C55E",
            "next": "#15803D",
            "left": "#4ADE80",
            "right": "#4ADE80",
            "shuffle": "#0891B2",
            "toggleShade": "#CFE7D5"
        },
        "mainmenu": {
            "mediaContainer": "#A7E3B7"
        },
        "obd": {
            "boxBackground": "#DDF0E2",
            "barColor": "#0891B2"
        }
    },

    "RoseQuartz": {
        "base": "#FBF2F4",
        "baseAlt": "#F5E0E5",
        "accent": "#BE185D",
        "text": {
            "primary": "#3A0E1F",
            "secondary": "#8E5B6C"
        },
        "states": {
            "hover": "#EAC8D1",
            "paused": "#EFD0D8",
            "playing": "#E0B8C3"
        },
        "sliders": {
            "volume": "#BE185D",
            "media": "#9333EA",
            "settings": "#9D174D"
        },
        "bottombar": {
            "previous": "#9D174D",
            "play": "#BE185D",
            "pause": "#BE185D",
            "next": "#9D174D",
            "volume": "#BE185D",
            "shuffle": "#9333EA",
            "toggleShade": "#EAC8D1",
            "homeButton": "#BE185D",
            "obdButton": "#9333EA",
            "mediaButton": "#DB2777",
            "sensorButton": "#A855F7",
            "settingsButton": "#9D174D",
            "androidAutoButton": "#9D174D",
            "phoneMirrorButton": "#9D174D"
        },
        "mediaroom": {
            "previous": "#9D174D",
            "play": "#DB2777",
            "pause": "#DB2777",
            "next": "#9D174D",
            "left": "#F472B6",
            "right": "#F472B6",
            "shuffle": "#9333EA",
            "toggleShade": "#EFD0D8"
        },
        "mainmenu": {
            "mediaContainer": "#E2A5B6"
        },
        "obd": {
            "boxBackground": "#F5E0E5",
            "barColor": "#9333EA"
        }
    },

    "LavenderMist": {
        "base": "#F5F3FA",
        "baseAlt": "#E9E4F2",
        "accent": "#6D28D9",
        "text": {
            "primary": "#1F0E3D",
            "secondary": "#605685"
        },
        "states": {
            "hover": "#D5CCE5",
            "paused": "#DDD4EA",
            "playing": "#C7BCD9"
        },
        "sliders": {
            "volume": "#6D28D9",
            "media": "#DB2777",
            "settings": "#5B21B6"
        },
        "bottombar": {
            "previous": "#5B21B6",
            "play": "#6D28D9",
            "pause": "#6D28D9",
            "next": "#5B21B6",
            "volume": "#6D28D9",
            "shuffle": "#DB2777",
            "toggleShade": "#D5CCE5",
            "homeButton": "#6D28D9",
            "obdButton": "#DB2777",
            "mediaButton": "#8B5CF6",
            "sensorButton": "#EC4899",
            "settingsButton": "#5B21B6",
            "androidAutoButton": "#5B21B6",
            "phoneMirrorButton": "#5B21B6"
        },
        "mediaroom": {
            "previous": "#5B21B6",
            "play": "#8B5CF6",
            "pause": "#8B5CF6",
            "next": "#5B21B6",
            "left": "#A78BFA",
            "right": "#A78BFA",
            "shuffle": "#DB2777",
            "toggleShade": "#DDD4EA"
        },
        "mainmenu": {
            "mediaContainer": "#C2B4DC"
        },
        "obd": {
            "boxBackground": "#E9E4F2",
            "barColor": "#DB2777"
        }
    },

    "Sunshine": {
        "base": "#FFFCF2",
        "baseAlt": "#FFF6D8",
        "accent": "#F97316",
        "text": {
            "primary": "#3A1E03",
            "secondary": "#7B5B2E"
        },
        "states": {
            "hover": "#FFE9A8",
            "paused": "#FFEEB7",
            "playing": "#FFDF8E"
        },
        "sliders": {
            "volume": "#F97316",
            "media": "#EAB308",
            "settings": "#C2410C"
        },
        "bottombar": {
            "previous": "#C2410C",
            "play": "#F97316",
            "pause": "#F97316",
            "next": "#C2410C",
            "volume": "#F97316",
            "shuffle": "#EAB308",
            "toggleShade": "#FFE9A8",
            "homeButton": "#F97316",
            "obdButton": "#EAB308",
            "mediaButton": "#FB923C",
            "sensorButton": "#FACC15",
            "settingsButton": "#C2410C",
            "androidAutoButton": "#C2410C",
            "phoneMirrorButton": "#C2410C"
        },
        "mediaroom": {
            "previous": "#C2410C",
            "play": "#FB923C",
            "pause": "#FB923C",
            "next": "#C2410C",
            "left": "#FDBA74",
            "right": "#FDBA74",
            "shuffle": "#EAB308",
            "toggleShade": "#FFEEB7"
        },
        "mainmenu": {
            "mediaContainer": "#FDD08A"
        },
        "obd": {
            "boxBackground": "#FFF6D8",
            "barColor": "#EAB308"
        }
    },

    "SlateCloud": {
        "base": "#F1F5F9",
        "baseAlt": "#E2E8F0",
        "accent": "#475569",
        "text": {
            "primary": "#0F172A",
            "secondary": "#64748B"
        },
        "states": {
            "hover": "#CBD5E1",
            "paused": "#D5DCE6",
            "playing": "#BAC4D2"
        },
        "sliders": {
            "volume": "#475569",
            "media": "#0F766E",
            "settings": "#334155"
        },
        "bottombar": {
            "previous": "#334155",
            "play": "#475569",
            "pause": "#475569",
            "next": "#334155",
            "volume": "#475569",
            "shuffle": "#0F766E",
            "toggleShade": "#CBD5E1",
            "homeButton": "#475569",
            "obdButton": "#0F766E",
            "mediaButton": "#64748B",
            "sensorButton": "#0E7490",
            "settingsButton": "#334155",
            "androidAutoButton": "#334155",
            "phoneMirrorButton": "#334155"
        },
        "mediaroom": {
            "previous": "#334155",
            "play": "#64748B",
            "pause": "#64748B",
            "next": "#334155",
            "left": "#94A3B8",
            "right": "#94A3B8",
            "shuffle": "#0F766E",
            "toggleShade": "#D5DCE6"
        },
        "mainmenu": {
            "mediaContainer": "#9CABBF"
        },
        "obd": {
            "boxBackground": "#E2E8F0",
            "barColor": "#0F766E"
        }
    },

    // Album Art Capture - Default placeholder (dynamically updated)
    "Album Art Capture": {
        "base": "#1a1a2e",
        "baseAlt": "#16213e",
        "accent": "#e94560",
        "text": {
            "primary": "#f0f0f0",
            "secondary": "#b4b4b4"
        },
        "states": {
            "hover": "#2a2a4e",
            "paused": "#252545",
            "playing": "#303060"
        },
        "sliders": {
            "volume": "#e94560",
            "media": "#0f3460",
            "settings": "#e94560"
        },
        "bottombar": {
            "previous": "#e94560",
            "play": "#e94560",
            "pause": "#e94560",
            "next": "#e94560",
            "volume": "#e94560",
            "shuffle": "#e94560",
            "toggleShade": "#2a2a4e",
            "homeButton": "#e94560",
            "obdButton": "#e94560",
            "mediaButton": "#e94560",
            "sensorButton": "#e94560",
            "settingsButton": "#e94560",
            "androidAutoButton": "#e94560",
            "phoneMirrorButton": "#e94560"
        },
        "mediaroom": {
            "previous": "#d43850",
            "play": "#e94560",
            "pause": "#e94560",
            "next": "#d43850",
            "left": "#c93048",
            "right": "#c93048",
            "shuffle": "#0f3460",
            "toggleShade": "#252545"
        },
        "mainmenu": {
            "mediaContainer": "#8c2a3a"
        },
        "obd": {
            "boxBackground": "#16213e",
            "barColor": "#e94560"
        }
    }
    })

    // Reactive current theme object - this is what all color bindings should use
    // It updates whenever currentTheme changes OR when album art colors are extracted
    property var activeTheme: {
        // If Album Art Capture is active and we have dynamic colors, use them
        if (currentTheme === "Album Art Capture" && albumArtTheme) {
            return albumArtTheme
        }
        // Check if theme is a custom theme first
        if (customThemes[currentTheme]) {
            return customThemes[currentTheme]
        }
        // Fall back to built-in themes
        return themes[currentTheme] || themes["SolarizedLight"]
    }

    // Helper function for backward compatibility
    function getCurrentTheme() {
        return activeTheme
    }

    // Function to update the Album Art Capture theme with new colors
    function updateAlbumArtTheme(themeJson) {
        console.log("[AlbumArtCapture QML] updateAlbumArtTheme called")
        try {
            let newTheme = JSON.parse(themeJson)
            console.log("[AlbumArtCapture QML] Parsed theme, base color:", newTheme.base, "accent:", newTheme.accent)

            // Store the new theme - this should trigger activeTheme to re-evaluate
            albumArtTheme = newTheme

            // If Album Art Capture is currently active, trigger a visual refresh
            if (currentTheme === "Album Art Capture") {
                console.log("[AlbumArtCapture QML] Theme is active, forcing refresh...")

                // First paint after entering Album Art mode is instant — only
                // subsequent song changes fade with the configured speed.
                var instantFirst = _firstAlbumArtUpdate
                var savedDuration = colorTransitionMs
                if (instantFirst) colorTransitionMs = 0

                // Force activeTheme binding to re-evaluate by toggling currentTheme
                let savedTheme = currentTheme
                currentTheme = ""
                currentTheme = savedTheme

                if (instantFirst) {
                    _firstAlbumArtUpdate = false
                    Qt.callLater(function() { colorTransitionMs = savedDuration })
                }

                // Emit signal for any listeners
                albumArtThemeUpdated()

                console.log("[AlbumArtCapture QML] UI refresh complete")
            }
        } catch (e) {
            console.log("[AlbumArtCapture QML] Error parsing album art theme:", e)
        }
    }

    // Signal emitted when album art theme colors are updated
    signal albumArtThemeUpdated()
    
    // Function to add a custom theme
    function addCustomTheme(name, themeObject) {
        customThemes[name] = themeObject
        // Emit a signal that themes have changed
        customThemesUpdated()
    }
    
    // Last non–Album-Art theme the user had selected — used by the Album Art
    // Capture toggle so that turning it off restores the prior choice.
    property string lastStaticTheme: ""

    // Function to get all theme names (built-in + custom).
    // "Album Art Capture" is intentionally excluded — it's surfaced as a
    // toggle under the chips, not as a selectable theme.
    function getAllThemeNames() {
        let allNames = Object.keys(themes).filter(function(n) { return n !== "Album Art Capture" })
        allNames = allNames.concat(Object.keys(customThemes))
        return allNames
    }

    // Curated ordering for the theme picker — built-in themes split into
    // light (light base) and dark (dark base) groups. The ordering within
    // each group is hand-tuned so the swatch palette flows nicely.
    readonly property var lightThemeNames: [
        "SolarizedLight", "ArcticChill", "Parchment", "Sunshine",
        "MintGarden", "RoseQuartz", "LavenderMist", "SlateCloud"
    ]
    readonly property var darkThemeNames: [
        "AmberConsole", "NeonMatrix", "ForestEmerald", "CosmicVoyager",
        "TechnoRetro", "QuantumNebula", "CrimsonEmber", "AutumnCascade"
    ]

    function getLightThemeNames() { return lightThemeNames }
    function getDarkThemeNames() { return darkThemeNames }

    // Transition duration for smooth color shifts (e.g. album art theme changes)
    property int colorTransitionMs: 1000

    property color bottomBarGradientStart: activeTheme.base
    Behavior on bottomBarGradientStart { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarGradientEnd: activeTheme.baseAlt
    Behavior on bottomBarGradientEnd { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarToggleShade: activeTheme.bottombar.toggleShade
    Behavior on bottomBarToggleShade { ColorAnimation { duration: colorTransitionMs } }
    property color hoverColor: activeTheme.states.hover
    Behavior on hoverColor { ColorAnimation { duration: colorTransitionMs } }
    property color hoverPausedColor: activeTheme.states.paused
    Behavior on hoverPausedColor { ColorAnimation { duration: colorTransitionMs } }
    property color hoverPlayingColor: activeTheme.states.paused
    Behavior on hoverPlayingColor { ColorAnimation { duration: colorTransitionMs } }
    property color pausedHighlightColor: activeTheme.states.paused
    Behavior on pausedHighlightColor { ColorAnimation { duration: colorTransitionMs } }
    property color playingHighlightColor: activeTheme.states.playing
    Behavior on playingHighlightColor { ColorAnimation { duration: colorTransitionMs } }
    property color rowBackgroundColor: activeTheme.base
    Behavior on rowBackgroundColor { ColorAnimation { duration: colorTransitionMs } }
    property color backgroundColor: activeTheme.base
    Behavior on backgroundColor { ColorAnimation { duration: colorTransitionMs } }
    property color headerBackgroundColor: activeTheme.baseAlt
    Behavior on headerBackgroundColor { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomToggleShade: activeTheme.mediaroom.toggleShade
    Behavior on mediaRoomToggleShade { ColorAnimation { duration: colorTransitionMs } }
    property color mediaContentArea: activeTheme.mainmenu.mediaContainer
    Behavior on mediaContentArea { ColorAnimation { duration: colorTransitionMs } }
    property color obdBoxBackground: activeTheme.obd.boxBackground
    Behavior on obdBoxBackground { ColorAnimation { duration: colorTransitionMs } }
    property color sidebarColor: activeTheme.base
    Behavior on sidebarColor { ColorAnimation { duration: colorTransitionMs } }
    property color contentColor: activeTheme.baseAlt
    Behavior on contentColor { ColorAnimation { duration: colorTransitionMs } }

    property color primaryTextColor: activeTheme.text.primary
    Behavior on primaryTextColor { ColorAnimation { duration: colorTransitionMs } }
    property color secondaryTextColor: activeTheme.text.secondary
    Behavior on secondaryTextColor { ColorAnimation { duration: colorTransitionMs } }
    property color headerTextColor: activeTheme.text.primary
    Behavior on headerTextColor { ColorAnimation { duration: colorTransitionMs } }
    property color clockTextColor: activeTheme.accent
    Behavior on clockTextColor { ColorAnimation { duration: colorTransitionMs } }
    property color metadataColor: activeTheme.accent
    Behavior on metadataColor { ColorAnimation { duration: colorTransitionMs } }
    property color obdLabelColor: activeTheme.obd.labelColor ? activeTheme.obd.labelColor : secondaryTextColor
    Behavior on obdLabelColor { ColorAnimation { duration: colorTransitionMs } }
    property color obdValueColor: activeTheme.obd.valueColor ? activeTheme.obd.valueColor : primaryTextColor
    Behavior on obdValueColor { ColorAnimation { duration: colorTransitionMs } }

    property color accent: activeTheme.accent
    Behavior on accent { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarActiveToggleButton: activeTheme.accent
    Behavior on bottomBarActiveToggleButton { ColorAnimation { duration: colorTransitionMs } }

    // Bottom bar button icon colors
    property color bottomBarPreviousButton: activeTheme.bottombar.previous
    Behavior on bottomBarPreviousButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarPlayButton: activeTheme.bottombar.play
    Behavior on bottomBarPlayButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarPauseButton: activeTheme.bottombar.pause
    Behavior on bottomBarPauseButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarNextButton: activeTheme.bottombar.next
    Behavior on bottomBarNextButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarVolumeButton: activeTheme.bottombar.volume
    Behavior on bottomBarVolumeButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarShuffleButton: activeTheme.bottombar.shuffle
    Behavior on bottomBarShuffleButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarHomeButton: activeTheme.bottombar.homeButton
    Behavior on bottomBarHomeButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarOBDButton: activeTheme.bottombar.obdButton
    Behavior on bottomBarOBDButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarMediaButton: activeTheme.bottombar.mediaButton
    Behavior on bottomBarMediaButton { ColorAnimation { duration: colorTransitionMs } }
    // Fall back to homeButton when an extracted (or older persisted) album-art
    // theme JSON doesn't carry a sensorButton key — keeps the icon reactive
    // even with partial palettes.
    property color bottomBarSensorButton: activeTheme.bottombar.sensorButton
        || activeTheme.bottombar.homeButton
    Behavior on bottomBarSensorButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarSettingsButton: activeTheme.bottombar.settingsButton
    Behavior on bottomBarSettingsButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarAndroidAutoButton: activeTheme.bottombar.androidAutoButton
    Behavior on bottomBarAndroidAutoButton { ColorAnimation { duration: colorTransitionMs } }
    property color bottomBarPhoneMirrorButton: activeTheme.bottombar.phoneMirrorButton
    Behavior on bottomBarPhoneMirrorButton { ColorAnimation { duration: colorTransitionMs } }

    // MediaRoom button icon colors
    property color mediaRoomPreviousButton: activeTheme.mediaroom.previous
    Behavior on mediaRoomPreviousButton { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomPlayButton: activeTheme.mediaroom.play
    Behavior on mediaRoomPlayButton { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomPauseButton: activeTheme.mediaroom.pause
    Behavior on mediaRoomPauseButton { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomNextButton: activeTheme.mediaroom.next
    Behavior on mediaRoomNextButton { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomLeftButton: activeTheme.mediaroom.left
    Behavior on mediaRoomLeftButton { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomRightButton: activeTheme.mediaroom.right
    Behavior on mediaRoomRightButton { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomToggleButton: activeTheme.mediaroom.shuffle
    Behavior on mediaRoomToggleButton { ColorAnimation { duration: colorTransitionMs } }

    // Slider colors
    property color volumeSliderColor: activeTheme.sliders.volume
    Behavior on volumeSliderColor { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomSlider: activeTheme.sliders.media
    Behavior on mediaRoomSlider { ColorAnimation { duration: colorTransitionMs } }
    property color mediaRoomSeekColor: activeTheme.sliders.volume
    Behavior on mediaRoomSeekColor { ColorAnimation { duration: colorTransitionMs } }
    property color settingsSliderColor: activeTheme.sliders.settings
    Behavior on settingsSliderColor { ColorAnimation { duration: colorTransitionMs } }
    property color obdBarColor: activeTheme.obd.barColor
    Behavior on obdBarColor { ColorAnimation { duration: colorTransitionMs } }

    // Status colors (with safe fallbacks for themes that don't define them)
    property color statusConnected: activeTheme.status ? activeTheme.status.connected : "#44AA44"
    Behavior on statusConnected { ColorAnimation { duration: colorTransitionMs } }
    property color statusDisconnected: activeTheme.status ? activeTheme.status.disconnected : "#AA4444"
    Behavior on statusDisconnected { ColorAnimation { duration: colorTransitionMs } }
    property color statusError: activeTheme.status ? activeTheme.status.error : "#F44336"
    Behavior on statusError { ColorAnimation { duration: colorTransitionMs } }
    property color statusWarning: activeTheme.status ? activeTheme.status.warning : "#FF9800"
    Behavior on statusWarning { ColorAnimation { duration: colorTransitionMs } }
    property color statusInfo: activeTheme.status ? activeTheme.status.info : "#2196F3"
    Behavior on statusInfo { ColorAnimation { duration: colorTransitionMs } }
    property color statusSuccess: activeTheme.status ? activeTheme.status.success : "#4CAF50"
    Behavior on statusSuccess { ColorAnimation { duration: colorTransitionMs } }
    property color statusDanger: activeTheme.status ? activeTheme.status.danger : "#e74c3c"
    Behavior on statusDanger { ColorAnimation { duration: colorTransitionMs } }

    // Function to update theme
    function setTheme(theme) {
        if (themes[theme] || customThemes[theme]) {
            if (theme !== "Album Art Capture") lastStaticTheme = theme
            else _firstAlbumArtUpdate = true
            currentTheme = theme
        }
    }

    // Apply a theme without animating the transition. Used for startup loads
    // and manual theme switches — the configured `colorTransitionMs` only
    // applies to album-art-driven color shifts on song change.
    function setThemeInstant(theme) {
        if (!(themes[theme] || customThemes[theme])) return
        if (theme !== "Album Art Capture") lastStaticTheme = theme
        else _firstAlbumArtUpdate = true
        var saved = colorTransitionMs
        colorTransitionMs = 0
        currentTheme = theme
        Qt.callLater(function() { colorTransitionMs = saved })
    }

    // The first album-art paint after entering Album Art mode (startup with
    // it already on, or a fresh toggle-on) snaps instantly to the current
    // song's colors — only real song changes should honor the transition
    // speed. Reset to true whenever the theme switches to "Album Art Capture".
    property bool _firstAlbumArtUpdate: true
    
    // Changed signal name to avoid conflict
    signal customThemesUpdated()
}