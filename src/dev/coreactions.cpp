#include "coreactions.h"

#include "commandserver.h"
#include "perfmonitor.h"
#include "uidriver.h"

#include <QPointer>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace OctaveDev {

namespace {

using Gui = CommandServer;

QString str(const QVariantMap &a, const char *key, const QString &def = QString())
{
    const QVariant v = a.value(QLatin1String(key));
    return v.isValid() && !v.isNull() ? v.toString() : def;
}

double num(const QVariantMap &a, const char *key)
{
    const QVariant v = a.value(QLatin1String(key));
    bool ok = false;
    const double d = v.toDouble(&ok);
    if (!v.isValid() || !ok)
        throw CommandError(QStringLiteral("missing or non-numeric args.%1").arg(QLatin1String(key)));
    return d;
}

double num(const QVariantMap &a, const char *key, double def)
{
    return a.contains(QLatin1String(key)) ? num(a, key) : def;
}

bool flag(const QVariantMap &a, const char *key, bool def)
{
    const QVariant v = a.value(QLatin1String(key));
    return v.isValid() && !v.isNull() ? v.toBool() : def;
}

QVariant list(const QStringList &l) { return QVariant(l); }

struct NumSetting {
    const char *key;
    const char *method;
    bool isFloat;
    double lo, hi;
};

// Same tables as backend/command_server.py
const QList<QPair<QString, QString>> kPages = {
    {"home", "MainMenu.qml"}, {"media_room", "MediaRoom.qml"}, {"media_player", "MediaPlayer.qml"},
    {"obd", "OBDMenu.qml"}, {"obd_home", "OBDHome.qml"}, {"obd_diagnostics", "OBDDiagnostics.qml"},
    {"sensor_home", "SensorHome.qml"}, {"settings", "SettingsMenu.qml"}, {"phone_mirror", "PhoneMirrorView.qml"},
    {"android_auto", "AndroidAutoView.qml"}, {"sensors", "SensorMenu.qml"}, {"car", "CarMenu.qml"},
    {"download", "DownloadPage.qml"}, {"clock", "ClockMenu.qml"},
};

const QStringList kSettingsSections = {
    "displaySettings", "mediaSettings", "obdSettings", "accessoriesSettings", "deviceSettings", "about",
};

// toggle key -> (save slot, property)
const QList<std::tuple<QString, const char *, const char *>> kBoolSettings = {
    {"show_clock", "save_show_clock", "showClock"},
    {"clock_24h", "save_clock_format", "clockFormat24Hour"},
    {"show_waveform", "save_show_waveform_visualizer", "showWaveformVisualizer"},
    {"show_3d_tilt", "save_show_3d_button_tilt", "show3DButtonTilt"},
    {"show_3d_album", "save_show_3d_album_preview", "show3DAlbumPreview"},
    {"rounded_album_art", "save_rounded_album_art", "roundedAlbumArt"},
    {"album_art_shadow", "save_show_album_art_shadow", "showAlbumArtShadow"},
    {"vinyl_mode", "save_vinyl_record_mode", "vinylRecordMode"},
    {"obd_fast_mode", "save_obd_fast_mode", "obdFastMode"},
    {"auto_play", "save_auto_play_on_startup", "autoPlayOnStartup"},
    {"show_bottom_bar_controls", "save_show_bottom_bar_media_controls", "showBottomBarMediaControls"},
    {"song_length_transition", "save_song_length_transition", "songLengthTransition"},
};

const QList<NumSetting> kNumSettings = {
    {"ui_scale", "save_ui_scale", true, 0.5, 2.0},
    {"blur_radius", "save_background_blur_radius", false, 0, 100},
    {"color_transition_ms", "save_color_transition_ms", false, 0, 5000},
    {"album_art_corner_radius", "save_album_art_corner_radius", false, 2, 32},
    {"bg_overlay_opacity", "save_background_overlay_opacity", false, 0, 100},
    {"side_card_opacity", "save_side_card_opacity", true, 0.1, 1.0},
    {"side_card_angle", "save_side_card_angle", false, 5, 60},
    {"button_tilt_duration", "save_button_tilt_duration", false, 50, 500},
    {"text_scroll_speed", "save_text_scroll_speed", false, 1000, 10000},
    {"clock_size", "save_clock_size", false, 8, 72},
};

const QList<std::tuple<QString, const char *, QStringList>> kStrSettings = {
    {"theme", "save_theme_setting", {}},
    {"font", "save_font_setting", {}},
    {"album_art_transition", "save_album_art_transition", {"Crossfade", "Slide", "Vinyl Lift", "Dissolve", "Flip"}},
    {"bottom_bar_orientation", "save_bottom_bar_orientation", {"bottom", "side"}},
    {"mediaFolder", "save_media_folder", {}},
};

// get_all_settings key -> property
const QList<QPair<QString, const char *>> kAllSettings = {
    {"volume", "currentVolume"}, {"theme", "themeSetting"}, {"font", "fontSetting"},
    {"show_clock", "showClock"}, {"clock_24h", "clockFormat24Hour"}, {"show_waveform", "showWaveformVisualizer"},
    {"ui_scale", "uiScale"}, {"blur_radius", "backgroundBlurRadius"}, {"color_transition_ms", "colorTransitionMs"},
    {"show_3d_tilt", "show3DButtonTilt"}, {"show_3d_album", "show3DAlbumPreview"},
    {"rounded_album_art", "roundedAlbumArt"}, {"album_art_shadow", "showAlbumArtShadow"},
    {"vinyl_mode", "vinylRecordMode"}, {"album_art_transition", "albumArtTransition"},
    {"bottom_bar_orientation", "bottomBarOrientation"}, {"show_bottom_bar_controls", "showBottomBarMediaControls"},
    {"song_length_transition", "songLengthTransition"}, {"obd_fast_mode", "obdFastMode"},
    {"auto_play", "autoPlayOnStartup"},
};

QStringList keysOf(const QList<std::tuple<QString, const char *, const char *>> &t)
{
    QStringList out;
    for (const auto &e : t)
        out << std::get<0>(e);
    return out;
}

} // namespace

void registerCoreActions(CommandServer &server, const CoreTargets &t)
{
    QPointer<UiDriver> ui = t.ui;
    QPointer<PerfMonitor> perf = t.perf;
    QPointer<QObject> sm = t.settings;
    QPointer<QObject> mm = t.media;
    QPointer<QObject> obd = t.obd;

    auto needUi = [ui] {
        if (!ui)
            throw CommandError(QStringLiteral("navigation bridge not active"));
        return ui.data();
    };
    auto need = [](const QPointer<QObject> &o, const char *what) {
        if (!o)
            throw CommandError(QStringLiteral("%1 not available").arg(QLatin1String(what)));
        return o.data();
    };

    // ── Performance metrics ──────────────────────────────────────
    server.registerAction("get_metrics", [perf](const QVariantMap &) -> QVariant {
        if (!perf)
            return QVariantMap{{"error", "perf monitor not active"}};
        return perf->metrics();
    });
    server.registerAction("get_detailed_report", [perf](const QVariantMap &) -> QVariant {
        if (!perf)
            return QStringLiteral("perf monitor not active");
        return Gui::onGui([perf] { return QVariant(perf ? perf->getDetailedReport() : QString()); });
    });
    server.registerAction("get_slot_timings", [](const QVariantMap &) { return QVariant(PerfMonitor::slotTimings()); });

    // ── Navigation ───────────────────────────────────────────────
    server.registerAction("navigate", [ui](const QVariantMap &a) -> QVariant {
        const QString page = str(a, "page");
        QString file;
        QStringList valid;
        for (const auto &p : kPages) {
            valid << p.first;
            if (p.first == page)
                file = p.second;
        }
        if (file.isEmpty())
            throw CommandError(QStringLiteral("Unknown page: %1. Valid: %2").arg(page, valid.join(", ")));
        if (ui)
            Gui::postGui([ui, file] { if (ui) ui->navigateTo(file); });
        return QVariantMap{{"navigated_to", page}};
    });
    server.registerAction("go_back", [ui](const QVariantMap &) {
        Gui::postGui([ui] { if (ui) ui->navigateBack(); });
        return QVariant();
    });
    server.registerAction("go_home", [ui](const QVariantMap &) {
        Gui::postGui([ui] { if (ui) ui->navigateHome(); });
        return QVariant();
    });
    server.registerAction("get_current_page", [ui](const QVariantMap &) -> QVariant {
        if (!ui)
            return QVariantMap{{"page", "unknown"}};
        return QVariantMap{{"page", Gui::onGui([ui] { return QVariant(ui ? ui->currentPage() : QString()); })}};
    });
    server.registerAction("list_pages", [](const QVariantMap &) {
        QStringList pages;
        for (const auto &p : kPages)
            pages << p.first;
        return list(pages);
    });
    server.registerAction("list_settings_sections", [](const QVariantMap &) { return list(kSettingsSections); });
    server.registerAction("open_settings", [ui](const QVariantMap &a) -> QVariant {
        const QString section = str(a, "section", "displaySettings");
        if (!kSettingsSections.contains(section))
            throw CommandError(QStringLiteral("Unknown settings section: %1. Valid: %2")
                                   .arg(section, kSettingsSections.join(", ")));
        Gui::postGui([ui, section] { if (ui) ui->navigateToSettings(section); });
        return QVariantMap{{"section", section}};
    });
    server.registerAction("set_window_size", [ui](const QVariantMap &a) -> QVariant {
        const int w = int(num(a, "width", 1280));
        const int h = int(num(a, "height", 720));
        Gui::postGui([ui, w, h] { if (ui) ui->setWindowSize(w, h); });
        return QVariantMap{{"width", w}, {"height", h}};
    });
    server.registerAction("set_dev_chrome", [ui](const QVariantMap &a) -> QVariant {
        const bool visible = flag(a, "visible", true);
        Gui::postGui([ui, visible] { if (ui) ui->setDevChrome(visible); });
        return QVariantMap{{"dev_chrome", visible}};
    });

    // ── Screenshots / recording ──────────────────────────────────
    server.registerAction("screenshot", [ui](const QVariantMap &a) -> QVariant {
        const QString path = str(a, "path");
        if (path.isEmpty())
            throw CommandError(QStringLiteral("screenshot requires args.path (absolute file path)"));
        if (!ui)
            return QVariantMap{{"saved", false}, {"error", "navigation bridge not active"}};
        const bool saved = Gui::onGui([ui, path] { return QVariant(ui && ui->captureTo(path)); }).toBool();
        return QVariantMap{{"saved", saved}, {"path", path}};
    });
    server.registerAction("record_start", [needUi](const QVariantMap &a) {
        UiDriver *u = needUi();
        const QString path = str(a, "path");
        if (path.isEmpty())
            throw CommandError(QStringLiteral("record_start requires args.path (absolute .mp4 path)"));
        const int fps = int(num(a, "fps", 30));
        return Gui::onGui([u, path, fps] { u->recordStart(path, fps); return QVariant(u->recordStatus()); }, 60000);
    });
    server.registerAction("record_stop", [needUi](const QVariantMap &) {
        UiDriver *u = needUi();
        return Gui::onGui([u] { u->recordStop(); return QVariant(u->recordStatus()); }, 90000);
    });
    server.registerAction("record_status", [needUi](const QVariantMap &) {
        UiDriver *u = needUi();
        return Gui::onGui([u] { return QVariant(u->recordStatus()); });
    });

    // ── UI driving: locate items by objectName, poke properties, click/drag ──
    server.registerAction("find_item", [needUi](const QVariantMap &a) {
        UiDriver *u = needUi();
        const QString name = str(a, "name");
        return Gui::onGui([u, name] { return u->findItem(name); });
    });
    server.registerAction("find_text", [needUi](const QVariantMap &a) {
        UiDriver *u = needUi();
        const QString text = str(a, "text"), prop = str(a, "prop", "text");
        return Gui::onGui([u, text, prop] { return u->findText(text, prop); });
    });
    server.registerAction("list_items", [needUi](const QVariantMap &a) {
        UiDriver *u = needUi();
        const QString prefix = str(a, "prefix");
        return Gui::onGui([u, prefix] { return QVariant(u->listItems(prefix)); });
    });
    server.registerAction("get_prop", [needUi](const QVariantMap &a) {
        UiDriver *u = needUi();
        const QString name = str(a, "name"), prop = str(a, "prop");
        // Convert on the GUI thread: QJSValue / QObject values belong to it
        return Gui::onGui([u, name, prop] { return jsonSafe(u->getProperty(name, prop)); });
    });
    server.registerAction("set_prop", [needUi](const QVariantMap &a) {
        UiDriver *u = needUi();
        const QString name = str(a, "name"), prop = str(a, "prop");
        const QVariant value = a.value("value");
        return Gui::onGui([u, name, prop, value] { return QVariant(u->setProperty(name, prop, value)); });
    });
    server.registerAction("call", [needUi](const QVariantMap &a) {
        UiDriver *u = needUi();
        const QString name = str(a, "name"), method = str(a, "method");
        return Gui::onGui([u, name, method] { return QVariant(u->callMethod(name, method)); });
    });
    server.registerAction("click", [needUi](const QVariantMap &a) {
        UiDriver *u = needUi();
        const double x = num(a, "x"), y = num(a, "y");
        return Gui::onGui([u, x, y] { u->click(x, y); return QVariant(true); });
    });
    server.registerAction("long_press", [needUi](const QVariantMap &a) {
        UiDriver *u = needUi();
        const double x = num(a, "x"), y = num(a, "y");
        const int ms = int(num(a, "ms", 1000));
        return Gui::onGui([u, x, y, ms] { u->longPress(x, y, ms); return QVariant(true); }, 30000 + ms);
    });
    server.registerAction("drag", [needUi](const QVariantMap &a) {
        UiDriver *u = needUi();
        const double x1 = num(a, "x1"), y1 = num(a, "y1"), x2 = num(a, "x2"), y2 = num(a, "y2");
        const int steps = int(num(a, "steps", 12));
        return Gui::onGui([=] { u->drag(x1, y1, x2, y2, steps); return QVariant(true); });
    });
    server.registerAction("type_text", [needUi](const QVariantMap &a) {
        UiDriver *u = needUi();
        const QString text = str(a, "text"), field = str(a, "field");
        const bool enter = flag(a, "enter", false);
        return Gui::onGui([=] { return jsonSafe(u->typeText(text, enter, field)); });
    });

    // ── Settings ─────────────────────────────────────────────────
    server.registerAction("get_all_settings", [sm, need](const QVariantMap &) {
        QObject *s = need(sm, "settings manager");
        return Gui::onGui([s] {
            QVariantMap out;
            for (const auto &e : kAllSettings)
                out.insert(e.first, s->property(e.second));
            return QVariant(out);
        });
    });
    server.registerAction("get_settings", [sm, need](const QVariantMap &) {
        QObject *s = need(sm, "settings manager");
        return Gui::onGui([s] {
            return QVariant(QVariantMap{{"volume", s->property("currentVolume")}, {"theme", s->property("themeSetting")}});
        });
    });
    auto boolEntry = [](const QString &key) {
        for (const auto &e : kBoolSettings)
            if (std::get<0>(e) == key)
                return e;
        throw CommandError(QStringLiteral("Unknown toggle: %1. Valid: %2").arg(key, keysOf(kBoolSettings).join(", ")));
    };
    server.registerAction("toggle_setting", [sm, need, boolEntry](const QVariantMap &a) {
        QObject *s = need(sm, "settings manager");
        const QString key = str(a, "key");
        const auto e = boolEntry(key);
        return Gui::onGui([s, key, e] {
            const bool current = s->property(std::get<2>(e)).toBool();
            invokeByName(s, std::get<1>(e), {!current});
            return QVariant(QVariantMap{{"key", key}, {"old", current}, {"new", !current}});
        });
    });
    server.registerAction("set_bool_setting", [sm, need, boolEntry](const QVariantMap &a) {
        QObject *s = need(sm, "settings manager");
        const QString key = str(a, "key");
        const bool value = flag(a, "value", false);
        const auto e = boolEntry(key);
        return Gui::onGui([s, key, value, e] {
            invokeByName(s, std::get<1>(e), {value});
            return QVariant(QVariantMap{{"key", key}, {"value", value}});
        });
    });
    server.registerAction("list_toggle_settings", [](const QVariantMap &) { return list(keysOf(kBoolSettings)); });
    server.registerAction("set_numeric_setting", [sm, need](const QVariantMap &a) {
        QObject *s = need(sm, "settings manager");
        const QString key = str(a, "key");
        auto it = std::find_if(kNumSettings.cbegin(), kNumSettings.cend(),
                               [&key](const NumSetting &n) { return key == QLatin1String(n.key); });
        if (it == kNumSettings.cend()) {
            QStringList valid;
            for (const NumSetting &n : kNumSettings)
                valid << QLatin1String(n.key);
            throw CommandError(QStringLiteral("Unknown numeric setting: %1. Valid: %2").arg(key, valid.join(", ")));
        }
        const NumSetting n = *it;
        double v = num(a, "value");
        if (!n.isFloat)
            v = std::trunc(v);
        v = std::clamp(v, n.lo, n.hi);
        const QVariant value = n.isFloat ? QVariant(v) : QVariant(int(v));
        return Gui::onGui([s, key, n, value] {
            invokeByName(s, n.method, {value});
            return QVariant(QVariantMap{{"key", key}, {"value", value}});
        });
    });
    server.registerAction("list_numeric_settings", [](const QVariantMap &) {
        QVariantMap out;
        for (const NumSetting &n : kNumSettings)
            out.insert(QLatin1String(n.key), QVariantMap{{"type", n.isFloat ? "float" : "int"},
                                                         {"min", n.isFloat ? QVariant(n.lo) : QVariant(int(n.lo))},
                                                         {"max", n.isFloat ? QVariant(n.hi) : QVariant(int(n.hi))}});
        return QVariant(out);
    });
    server.registerAction("set_string_setting", [sm, need](const QVariantMap &a) {
        QObject *s = need(sm, "settings manager");
        const QString key = str(a, "key"), value = str(a, "value");
        for (const auto &e : kStrSettings) {
            if (std::get<0>(e) != key)
                continue;
            const QStringList &valid = std::get<2>(e);
            if (!valid.isEmpty() && !valid.contains(value))
                throw CommandError(QStringLiteral("Invalid value for %1: %2. Valid: %3").arg(key, value, valid.join(", ")));
            const char *method = std::get<1>(e);
            return Gui::onGui([s, key, value, method] {
                invokeByName(s, method, {value});
                return QVariant(QVariantMap{{"key", key}, {"value", value}});
            });
        }
        QStringList keys;
        for (const auto &e : kStrSettings)
            keys << std::get<0>(e);
        throw CommandError(QStringLiteral("Unknown string setting: %1. Valid: %2").arg(key, keys.join(", ")));
    });
    server.registerAction("list_string_settings", [](const QVariantMap &) {
        QVariantMap out;
        for (const auto &e : kStrSettings) {
            const QStringList &valid = std::get<2>(e);
            out.insert(std::get<0>(e), QVariantMap{{"valid_values", valid.isEmpty() ? QVariant() : QVariant(valid)}});
        }
        return QVariant(out);
    });

    // ── Media controls ───────────────────────────────────────────
    for (const char *action : {"next_track", "previous_track", "toggle_play", "pause", "toggle_mute", "toggle_shuffle"}) {
        server.registerAction(QLatin1String(action), [mm, need, action](const QVariantMap &) {
            QObject *m = need(mm, "media manager");
            Gui::onGui([m, action] { invokeByName(m, action); return QVariant(); });
            return QVariant();
        });
    }
    server.registerAction("set_volume", [mm, need](const QVariantMap &a) {
        QObject *m = need(mm, "media manager");
        const double v = num(a, "volume", 0.5);
        Gui::onGui([m, v] { invokeByName(m, "setVolume", {v}); return QVariant(); });
        return QVariant();
    });
    server.registerAction("get_media_status", [mm, need](const QVariantMap &) {
        QObject *m = need(mm, "media manager");
        return Gui::onGui([m] {
            return QVariant(QVariantMap{
                {"playing", invokeByName(m, "is_playing")},
                {"paused", invokeByName(m, "is_paused")},
                {"muted", invokeByName(m, "is_muted")},
                {"shuffled", invokeByName(m, "is_shuffled")},
                {"current_file", invokeByName(m, "get_current_file")},
                {"position_ms", invokeByName(m, "get_position")},
                {"duration_ms", invokeByName(m, "get_duration")},
            });
        });
    });
    server.registerAction("get_track_info", [mm, need](const QVariantMap &) {
        QObject *m = need(mm, "media manager");
        return Gui::onGui([m] {
            const QString f = invokeByName(m, "get_current_file").toString();
            if (f.isEmpty())
                return QVariant(QVariantMap{{"file", QVariant()}, {"display_name", QVariant()},
                                            {"album", QVariant()}, {"artist", QVariant()}});
            return QVariant(QVariantMap{
                {"file", f},
                {"display_name", invokeByName(m, "get_display_name", {f})},
                {"album", invokeByName(m, "get_album", {f})},
                {"artist", invokeByName(m, "get_band", {f})},
            });
        });
    });
    server.registerAction("skip_tracks", [mm, need](const QVariantMap &a) {
        QObject *m = need(mm, "media manager");
        const int count = int(num(a, "count", 1));
        Gui::onGui([m, count] {
            for (int i = 0; i < count; ++i)
                invokeByName(m, "next_track");
            return QVariant();
        });
        return QVariant(QVariantMap{{"skipped", count}});
    });

    // ── OBD controls ─────────────────────────────────────────────
    server.registerAction("obd_status", [obd, need](const QVariantMap &) {
        QObject *o = need(obd, "OBD manager");
        return Gui::onGui([o] {
            return QVariant(QVariantMap{{"connected", invokeByName(o, "is_connected")},
                                        {"status", invokeByName(o, "get_connection_status")}});
        });
    });
    server.registerAction("obd_reconnect", [obd, need](const QVariantMap &) {
        QObject *o = need(obd, "OBD manager");
        Gui::onGui([o] { invokeByName(o, "reconnect"); return QVariant(); });
        return QVariant();
    });
}

} // namespace OctaveDev
