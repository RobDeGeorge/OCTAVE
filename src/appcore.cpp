#include "appcore.h"

#include "util/units.h"

#include <QColor>
#include <QQmlContext>
#include <QTimer>

#include <cmath>

#ifdef Q_OS_ANDROID
#include <QtCore/private/qandroidextras_p.h>
#include <QFuture>
#include <QtConcurrent>
#include "platform/androidmediabridge.h"
#endif

AppCore::AppCore(const QDir &frontendDir, Units *units)
    : clock(&settingsManager)
    , volumeController(&settingsManager)
    , obdManager(&settingsManager)
    , shiftLightManager(&settingsManager)
    , obdAdapterFinder(&obdManager, &settingsManager)
    , tripRecorder(&settingsManager)
    , nowPlaying(&mediaManager, &phoneMirrorManager, &settingsManager)
    , fontCatalog(frontendDir.absoluteFilePath(QStringLiteral("assets/fonts")))
    , themeCatalog(frontendDir.absoluteFilePath(QStringLiteral("themes.json")), &settingsManager)
    , servicePairing(&settingsManager)
{
    // Settings Manager — central settings store, everything depends on this
    expose("settingsManager", &settingsManager);
    // Diagnostics — read / export the logs from the head unit (Settings > About)
    expose("diagnosticsManager", &diagnosticsManager);
    // Clock — time display, depends on settings for format
    expose("clock", &clock);
    // Network Manager — WiFi status, update checking
    expose("networkManager", &networkManager);
    // Volume Controller — quadratic curve + dispatch to all audio outputs
    // (wired to the managers in wireVolume())
    expose("volumeController", &volumeController);

    // Media Manager — local music playback, library scanning, playlists
    mediaManager.setSettingsManager(&settingsManager);
    expose("mediaManager", &mediaManager);

#ifdef Q_OS_ANDROID
#ifdef OCTAVE_ENABLE_DOWNLOADS
    // Initialize the youtubedl-android bridge off the GUI thread — on first
    // launch it extracts Python 3.9 + yt-dlp + ffmpeg to the app's private data
    // dir (~10–20s). Subsequent launches are instant.
    QtConcurrent::run([] {
        if (OctaveAndroid::initDownloadBridge()) {
            qInfo() << "[OctaveDownloadBridge] yt-dlp + ffmpeg ready";
        } else {
            qWarning() << "[OctaveDownloadBridge] init failed — downloads disabled";
        }
    });
#endif

    // Request runtime permissions needed for the media library and (eventual)
    // OBD Bluetooth transport. Declared in the manifest isn't enough on
    // Android 12+/13+; user must grant at runtime. Re-scan once granted.
    const QStringList androidPermissions = {
        QStringLiteral("android.permission.READ_MEDIA_AUDIO"),     // Android 13+
        QStringLiteral("android.permission.READ_EXTERNAL_STORAGE"),// pre-13 fallback
        QStringLiteral("android.permission.BLUETOOTH_CONNECT"),    // OBD Phase 3
        QStringLiteral("android.permission.BLUETOOTH_SCAN"),       // OBD Phase 3
    };
    for (const QString &perm : androidPermissions) {
        auto future = QtAndroidPrivate::requestPermission(perm);
        future.then([this, perm](QtAndroidPrivate::PermissionResult result) {
            if (result == QtAndroidPrivate::Authorized
                && perm == QStringLiteral("android.permission.READ_MEDIA_AUDIO")) {
                // Got audio permission — kick off a library scan
                QMetaObject::invokeMethod(&mediaManager, "scan_library",
                                          Qt::QueuedConnection, Q_ARG(bool, true));
            }
        });
    }
#endif

    // Audio Analyzer — waveform visualization via FFT
    expose("audioAnalyzer", &audioAnalyzer);

    // Parameter catalog: title / unit / range / kind of every OBD PID and IMU
    // reading, for every frontend (OBDParameterModel reads it)
    expose("parameterCatalog", &parameterCatalog);
    // OBD Manager — vehicle diagnostics via ELM327
    expose("obdManager", &obdManager);
    // Shift light — which RPM flag is active and its flash phase, fed by OBD
    QObject::connect(&obdManager, &OBDManager::rpmChanged,
                     &shiftLightManager, &ShiftLightManager::setRpm);
    expose("shiftLightManager", &shiftLightManager);
    // Bluetooth OBD adapter discovery + pairing (the "Connect" sheet)
    expose("obdAdapterFinder", &obdAdapterFinder);

    // Trip Recorder — the black box: every OBD reading of every drive into
    // <config>/history/blackbox.sqlite, one CSV + JSON per trip
    tripRecorder.attach(&obdManager);
    tripRecorder.start();
    expose("tripRecorder", &tripRecorder);

    // Hardware managers (USB-serial / I2C on desktop, stubbed on mobile via header).
    esp32VolumeManager.connect_settings_manager(&settingsManager);
    expose("esp32VolumeManager", &esp32VolumeManager);
    berryIMU.connect_settings_manager(&settingsManager);
    expose("berryIMU", &berryIMU);

    wireGauges();
    expose("paramStore", &paramStore);

    gestureSensor.connect_settings_manager(&settingsManager);
    expose("gestureSensor", &gestureSensor);

    // Phone mirror and Android Auto (stubbed out on mobile via Q_OS_MOBILE
    // guards in the headers). The dhuframe image provider is main.cpp's.
    expose("androidAutoManager", &androidAutoManager);
    expose("phoneMirrorManager", &phoneMirrorManager);
    wirePhoneMirror();

    // What is playing and its controls, whichever source has them (the phone
    // while mirroring, else OCTAVE's player); also the handoff between the two.
    // Outside the desktop block: on mobile the phone stub never takes them, so
    // the source is always local
    expose("nowPlaying", &nowPlaying);
    wireAnalyzer();

    // Download Manager — music search & download via yt-dlp
    downloadManager.connect_settings_manager(&settingsManager);
    expose("downloadManager", &downloadManager);
    // Auto-rescan library when a download completes
    QObject::connect(&downloadManager, &DownloadManager::downloadComplete,
                     &mediaManager, [this](const QString &, const QString &) {
        mediaManager.scan_library(false);
    });

    wireVolume();

    // Time formatting, compass points and unit conversions (src/util/units)
    if (units)
        expose("units", units);
    // The bundled UI and icon fonts, registered before any QML loads
    expose("fontCatalog", &fontCatalog);
    // The colour themes as data, and the palette in force (Style.qml reads it)
    expose("themeCatalog", &themeCatalog);

    // Dashboards (data-driven dashboards + user-authored presets), see
    // TODO/dashboards-roadmap.md
    dashboardManager.setPresetsDir(frontendDir.absoluteFilePath(QStringLiteral("dashboards/presets")));
    dashboardManager.setWidgetCatalogFile(frontendDir.absoluteFilePath(QStringLiteral("dashboards/widget-catalog.json")));
    dashboardManager.setUserDir(SettingsManager::getAppDataDir() + QStringLiteral("/dashboards"));
    expose("dashboardManager", &dashboardManager);

    // The simulator: `active` false and every control a no-op unless started
    expose("sim", &sim);

    // Other screens: installed web / exec frontends, chosen from the next start
    frontendManager.setRoot(SettingsManager::getAppDataDir() + QStringLiteral("/frontends"));
    expose("frontendManager", &frontendManager);

    wireInputs();
    wireKnobLed();
}

void AppCore::expose(const char *name, QObject *object)
{
    registry.add(QString::fromLatin1(name), object);
}

void AppCore::exposeTo(QQmlContext *ctx) const
{
    for (const QString &name : registry.names())
        ctx->setContextProperty(name, registry.object(name));
}

void AppCore::wireGauges()
{
    // Gauges show the units the settings ask for (the Sensors page keeps
    // its own temperature unit)
    auto applyGaugeUnits = [this] {
        parameterCatalog.setDisplayUnits(settingsManager.gaugeTempUnit(), settingsManager.gaugeSpeedUnit(),
                                         settingsManager.sensorAltitudeUnit());
    };
    applyGaugeUnits();
    QObject::connect(&settingsManager, &SettingsManager::gaugeTempUnitChanged, &parameterCatalog, applyGaugeUnits);
    QObject::connect(&settingsManager, &SettingsManager::gaugeSpeedUnitChanged, &parameterCatalog, applyGaugeUnits);
    QObject::connect(&settingsManager, &SettingsManager::sensorAltitudeUnitChanged, &parameterCatalog, applyGaugeUnits);

    // Value store: the latest value of every PID and IMU reading, one
    // notifying key per id (OBDParameterModel and the gauges bind to it)
    QObject::connect(&obdManager, &OBDManager::parameterSampled, &paramStore, &ParameterStore::ingest);
    auto feed = [this](const char *id) {
        return [this, key = QString::fromLatin1(id)](float v) { paramStore.ingest(key, v); };
    };
    QObject::connect(&berryIMU, &BerryIMUManager::pitchChanged, &paramStore, feed("PITCH"));
    QObject::connect(&berryIMU, &BerryIMUManager::rollChanged, &paramStore, feed("ROLL"));
    QObject::connect(&berryIMU, &BerryIMUManager::headingChanged, &paramStore, feed("HEADING"));
    QObject::connect(&berryIMU, &BerryIMUManager::altitudeChanged, &paramStore, feed("ALTITUDE"));
    QObject::connect(&berryIMU, &BerryIMUManager::accelMagnitudeChanged, &paramStore, feed("ACCEL_MAG"));
    QObject::connect(&berryIMU, &BerryIMUManager::lateralGChanged, &paramStore, feed("LATERAL_G"));
    QObject::connect(&berryIMU, &BerryIMUManager::longitudinalGChanged, &paramStore, feed("LONGITUDINAL_G"));
    QObject::connect(&berryIMU, &BerryIMUManager::baroTempChanged, &paramStore, feed("BARO_TEMP"));
}

void AppCore::wirePhoneMirror()
{
#ifndef Q_OS_MOBILE
    // Phone audio drives the visualizer's live mode
    QObject::connect(&phoneMirrorManager, &PhoneMirrorManager::audioPlayed,
                     &audioAnalyzer, &AudioAnalyzer::feed_live_pcm);
    // Phone mirror settings from saved config (desktop-only accessors)
    phoneMirrorManager.setDisplaySize(settingsManager.get_scrcpy_display_size());
    // Picture quality: virtual display density and per-link video bit rate
    phoneMirrorManager.setDisplayDpi(settingsManager.get_scrcpy_display_dpi());
    phoneMirrorManager.setBitRateUsb(settingsManager.get_scrcpy_bit_rate_usb());
    phoneMirrorManager.setBitRateWifi(settingsManager.get_scrcpy_bit_rate_wifi());
    QObject::connect(&settingsManager, &SettingsManager::scrcpyDisplayDpiChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setDisplayDpi);
    QObject::connect(&settingsManager, &SettingsManager::scrcpyBitRateUsbChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setBitRateUsb);
    QObject::connect(&settingsManager, &SettingsManager::scrcpyBitRateWifiChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setBitRateWifi);
    // Adaptive quality: the per-link rates above become the ceiling
    phoneMirrorManager.setBitRateAuto(settingsManager.get_scrcpy_bit_rate_auto());
    QObject::connect(&settingsManager, &SettingsManager::scrcpyBitRateAutoChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setBitRateAuto);
    // A USB phone is switched to adb over TCP so it can be reached over Wi-Fi later
    phoneMirrorManager.setAutoWifi(settingsManager.get_scrcpy_auto_wifi());
    QObject::connect(&settingsManager, &SettingsManager::scrcpyAutoWifiChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setAutoWifi);
    phoneMirrorManager.setAudioGain(float(settingsManager.get_scrcpy_audio_gain()));
    QObject::connect(&settingsManager, &SettingsManager::scrcpyAudioGainChanged,
                     &phoneMirrorManager, [this](double g) { phoneMirrorManager.setAudioGain(float(g)); });
    // Duck local media while the phone produces sound (nav prompts, video, calls)
    phoneMirrorManager.setAudioDuckEnabled(settingsManager.get_scrcpy_audio_duck_enabled());
    phoneMirrorManager.setAudioDuckLevel(float(settingsManager.get_scrcpy_audio_duck_level()));
    QObject::connect(&settingsManager, &SettingsManager::scrcpyAudioDuckEnabledChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setAudioDuckEnabled);
    QObject::connect(&settingsManager, &SettingsManager::scrcpyAudioDuckLevelChanged,
                     &phoneMirrorManager, [this](double l) { phoneMirrorManager.setAudioDuckLevel(float(l)); });
    QObject::connect(&phoneMirrorManager, &PhoneMirrorManager::duckingChanged,
                     &mediaManager, &MediaManager::setDucking);
    // Keep the phone's own panel dark while mirroring; a locked phone is woken automatically
    phoneMirrorManager.setPhoneScreenOff(settingsManager.get_scrcpy_phone_screen_off());
    QObject::connect(&settingsManager, &SettingsManager::scrcpyPhoneScreenOffChanged,
                     &phoneMirrorManager, &PhoneMirrorManager::setPhoneScreenOff);
    // Phone last connected over Wi-Fi: reconnected at startup, and kept
    // current when it is paired, moves to a new port, or is forgotten
    phoneMirrorManager.setWirelessAddress(settingsManager.get_scrcpy_wireless_address());
    QObject::connect(&phoneMirrorManager, &PhoneMirrorManager::wirelessAddressChanged,
                     &settingsManager, &SettingsManager::save_scrcpy_wireless_address);
#endif
}

void AppCore::wireAnalyzer()
{
    // The visualizer follows what is playing: phone audio live, a local
    // track pre-analysed and read at its position
    auto feedAnalyzer = [this] {
        audioAnalyzer.set_live(nowPlaying.isPhone());
        audioAnalyzer.setPlaying(nowPlaying.playing());
    };
    QObject::connect(&nowPlaying, &NowPlaying::sourceChanged, &audioAnalyzer, feedAnalyzer);
    QObject::connect(&nowPlaying, &NowPlaying::playingChanged, &audioAnalyzer, feedAnalyzer);
    QObject::connect(&mediaManager, &MediaManager::positionChanged, &audioAnalyzer, &AudioAnalyzer::setPosition);
    QObject::connect(&mediaManager, &MediaManager::currentMediaChanged, &audioAnalyzer,
                     [this](const QString &file) { audioAnalyzer.setLocalFile(mediaManager.get_full_file_path(file)); });
    feedAnalyzer();
    if (!mediaManager.get_current_file().isEmpty())
        audioAnalyzer.setLocalFile(mediaManager.get_full_file_path(mediaManager.get_current_file()));
}

void AppCore::wireVolume()
{
    // Wire volume controller -> all audio outputs. Media and phone mirror
    // take the curved linear value, the ESP32 LED ring the raw percent (the
    // ESP32 manager rate-limits itself).
    QObject::connect(&volumeController, &VolumeController::volumeApplied,
                     &mediaManager, [this](int percent, float linear) {
        mediaManager.setVolume(linear);
        phoneMirrorManager.setVolume(linear);
#ifndef Q_OS_MOBILE
        esp32VolumeManager.send_volume_update(percent);
#else
        Q_UNUSED(percent);
#endif
    });
    volumeController.applyVolume(settingsManager.currentVolume());
    // Mute is VolumeController's too (in memory): it silences local media,
    // phone audio and lights the knob's mute LED
    QObject::connect(&volumeController, &VolumeController::mutedChanged,
                     &mediaManager, [this](bool muted) {
        mediaManager.setMuted(muted);
        phoneMirrorManager.setMuted(muted);
#ifndef Q_OS_MOBILE
        esp32VolumeManager.send_mute_state(muted);
#endif
    });
    phoneMirrorManager.setMuted(volumeController.muted());
}

void AppCore::wireInputs()
{
    // Wire gesture sensor actions -> media control (stub never emits on mobile).
    QObject::connect(&gestureSensor, &GestureManager::actionTriggered,
                     &nowPlaying, [this](const QString &action) {
        // Track actions drive whichever source has the media controls
        if (action == "next_track") nowPlaying.next();
        else if (action == "previous_track") nowPlaying.previous();
        else if (action == "play_pause") nowPlaying.togglePlay();
        else if (action == "mute_toggle") volumeController.toggleMute();
        else if (action == "volume_up") {
            int step = settingsManager.gestureVolumeStep();
            volumeController.applyVolume(settingsManager.currentVolume() + step);
        } else if (action == "volume_down") {
            int step = settingsManager.gestureVolumeStep();
            volumeController.applyVolume(settingsManager.currentVolume() - step);
        }
    });

    // Wire ESP32 volume knob -> volume controller (stub never emits on mobile).
    QObject::connect(&esp32VolumeManager, &ESP32VolumeManager::volumeChangeRequested,
                     &volumeController, [this](float delta) {
        static float accumulator = 0.0f;
        accumulator += delta;
        if (std::abs(accumulator) >= 1.0f) {
            int change = static_cast<int>(accumulator);
            accumulator -= change;
            volumeController.applyVolume(settingsManager.currentVolume() + change);
        }
    });
    // The LED follows mutedChanged, but answer every press too: a press the
    // debounce drops changes nothing here, and the knob may have flipped its
    // LED locally
    QObject::connect(&esp32VolumeManager, &ESP32VolumeManager::muteToggleRequested,
                     &volumeController, [this]() {
        volumeController.toggleMute();
        esp32VolumeManager.send_mute_state(volumeController.muted());
    });
}

void AppCore::wireKnobLed()
{
#ifndef Q_OS_MOBILE
    // ESP32 LED ring colour sync.
    // "static" mode pushes the user's picked colour; "theme" mode follows the
    // album-art accent. Cyan (0,255,255) is the fallback when a colour is
    // missing or unparsable.
    auto sendEsp32Rgb = [this](const QString &hex, bool fallbackOnError) {
        const QColor c(hex.trimmed());
        if (c.isValid())
            esp32VolumeManager.send_theme_color(c.red(), c.green(), c.blue());
        else if (fallbackOnError)
            esp32VolumeManager.send_theme_color(0, 255, 255);
    };
    auto accentFromAlbumArt = [](const QVariantMap &colors) -> QString {
        if (colors.isEmpty())
            return QString();
        return colors.value(QStringLiteral("accent"), QStringLiteral("#00FFFF")).toString();
    };
    auto sendCurrentLedColor = [this, sendEsp32Rgb, accentFromAlbumArt]() {
        if (settingsManager.esp32LedColorMode() == QLatin1String("static")) {
            sendEsp32Rgb(settingsManager.esp32LedStaticColor(), true);
            return;
        }
        const QString accent = accentFromAlbumArt(settingsManager.albumArtColors());
        if (accent.isEmpty())
            esp32VolumeManager.send_theme_color(0, 255, 255);
        else
            sendEsp32Rgb(accent, true);
    };
    QObject::connect(&settingsManager, &SettingsManager::albumArtColorsChanged,
                     &esp32VolumeManager, [this, accentFromAlbumArt, sendEsp32Rgb]() {
        if (settingsManager.esp32LedColorMode() != QLatin1String("theme"))
            return;
        const QString accent = accentFromAlbumArt(settingsManager.albumArtColors());
        if (!accent.isEmpty())
            sendEsp32Rgb(accent, false);
    });
    QObject::connect(&settingsManager, &SettingsManager::esp32LedStaticColorChanged,
                     &esp32VolumeManager, [this, sendEsp32Rgb](const QString &color) {
        if (settingsManager.esp32LedColorMode() == QLatin1String("static"))
            sendEsp32Rgb(color, false);
    });
    QObject::connect(&settingsManager, &SettingsManager::esp32LedColorModeChanged,
                     &esp32VolumeManager, [sendCurrentLedColor](const QString &) {
        sendCurrentLedColor();
    });
    // Push the full state (volume / mute / colour) once the knob is up; the
    // 500 ms delay gives the firmware time to finish its own boot after the
    // serial port opens.
    QObject::connect(&esp32VolumeManager, &ESP32VolumeManager::connectionStatusChanged,
                     &esp32VolumeManager, [this, sendCurrentLedColor](const QString &status) {
        if (status != QLatin1String("Connected"))
            return;
        QTimer::singleShot(500, &esp32VolumeManager, [this, sendCurrentLedColor]() {
            esp32VolumeManager.send_volume_update(settingsManager.currentVolume());
            esp32VolumeManager.send_mute_state(volumeController.muted());
            sendCurrentLedColor();
        });
    });
#endif
}

void AppCore::shutdown()
{
    // Stubs make all calls no-op on mobile
    androidAutoManager.cleanup();
    phoneMirrorManager.cleanup();
    esp32VolumeManager.cleanup();
    berryIMU.cleanup();
    gestureSensor.cleanup();
    tripRecorder.shutdown();   // closes the trip and writes its files before the adapter goes
    obdManager.close();
    downloadManager.cleanup();
    networkManager.cleanup();
    mediaManager._save_playback_state();   // before the settings flush: saveSettings() is debounced now
    mediaManager.flush_metadata_store();
    settingsManager.flushPendingSave();   // coalesced settings writes land before exit
}
