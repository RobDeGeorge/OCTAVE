// What Home can be, besides a JSON dashboard: the hand-built screens of
// frontend/home/ and the classic three-panel Home (MainMenu's own). For
// Settings > Display > Home Screen (a card each, with its `preview`
// picture) and MainMenu's loader. The `homeScreen` setting holds an id;
// "" means the dashboard dashboardManager.homeDashboardId names.
.pragma library

var list = [
    { "id": "cockpit",   "label": "Cockpit",   "source": "home/HomeCockpit.qml",   "preview": "assets/home/cockpit.jpg" },
    { "id": "glass",     "label": "Glass",     "source": "home/HomeGlass.qml",     "preview": "assets/home/glass.jpg" },
    { "id": "trail",     "label": "Trail",     "source": "home/HomeTrail.qml",     "preview": "assets/home/trail.jpg" },
    { "id": "retro",     "label": "Retro",     "source": "home/HomeRetro.qml",     "preview": "assets/home/retro.jpg" },
    { "id": "rings",     "label": "Rings",     "source": "home/HomeRings.qml",     "preview": "assets/home/rings.jpg" },
    { "id": "telemetry", "label": "Telemetry", "source": "home/HomeTelemetry.qml", "preview": "assets/home/telemetry.jpg" },
    // (no file: MainMenu draws it)
    { "id": "classic",   "label": "Classic",   "source": "",                       "preview": "assets/home/classic.jpg" }
]

// The pictures of the built-in dashboards, by dashboard id
var dashboardPreviews = { "home": "assets/home/dashboard-home.jpg" }

function sourceFor(id) {
    for (var i = 0; i < list.length; i++)
        if (list[i].id === id) return list[i].source
    return ""
}
