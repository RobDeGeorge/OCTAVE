#ifndef OCTAVE_APPCORE_H
#define OCTAVE_APPCORE_H

// Every manager, constructed and wired to each other, with no QML engine:
// what a frontend drives. main.cpp builds one and hands it to the QML UI
// (exposeTo() a context) and to the service (registry); `octave --headless`
// (TODO/open-backend.md, contract step 2) will run it with no engine at all.
//
// Members are declared in construction order: settings first, everything
// that reads them after. What needs the engine (the dhuframe image
// provider, QML types, import paths, Quick3D probes, the wiki URL) stays in
// main.cpp.

#include <QDir>
#include <QObject>

#include "service/serviceregistry.h"

#include "managers/settingsmanager.h"
#include "managers/diagnosticsmanager.h"
#include "managers/clock.h"
#include "managers/networkmanager.h"
#include "managers/volumecontroller.h"
#include "managers/mediamanager.h"
#include "managers/audioanalyzer.h"
#include "managers/parametercatalog.h"
#include "managers/obdmanager.h"
#include "managers/shiftlightmanager.h"
#include "managers/obdadapterfinder.h"
#include "managers/triprecorder.h"
#include "managers/esp32volumemanager.h"  // desktop-only; empty on Q_OS_MOBILE
#include "managers/berryimumanager.h"     // desktop-only; empty on Q_OS_MOBILE
#include "managers/parameterstore.h"
#include "managers/gesturemanager.h"      // desktop-only; empty on Q_OS_MOBILE
#include "managers/androidautomanager.h"
#include "managers/airplaymanager.h"
#include "managers/phonemirrormanager.h"
#include "managers/nowplaying.h"
#include "managers/themecatalog.h"
#include "service/servicepairing.h"
#include "managers/dashboardmanager.h"
#include "util/fontcatalog.h"
#include "sim/simulator.h"
#include "sim/demodrive.h"
#include "managers/frontendmanager.h"

class QQmlContext;
class Units;

class AppCore
{
public:
    // `frontendDir`: the bundled fonts and dashboard presets live under it.
    // `units` is main's (it must outlive the QML engine, which the core
    // does not); it is registered with the managers.
    AppCore(const QDir &frontendDir, Units *units);

    // Every manager as a context property of `ctx`, by its registry name
    void exposeTo(QQmlContext *ctx) const;

    // From aboutToQuit: stop the hardware and the players, close the trip,
    // flush the settings
    void shutdown();

    // The objects a frontend can use, by the names QML knows them by
    OctaveService::ServiceRegistry registry;

    SettingsManager settingsManager;
    DiagnosticsManager diagnosticsManager;
    Clock clock;
    NetworkManager networkManager;
    VolumeController volumeController;
    MediaManager mediaManager;
    AudioAnalyzer audioAnalyzer;
    ParameterCatalog parameterCatalog;
    OBDManager obdManager;
    ShiftLightManager shiftLightManager;
    OBDAdapterFinder obdAdapterFinder;
    TripRecorder tripRecorder;
    ESP32VolumeManager esp32VolumeManager;
    BerryIMUManager berryIMU;
    ParameterStore paramStore;
    GestureManager gestureSensor;
    AndroidAutoManager androidAutoManager;
    PhoneMirrorManager phoneMirrorManager;
    AirPlayManager airPlayManager;
    NowPlaying nowPlaying;
    FontCatalog fontCatalog;
    ThemeCatalog themeCatalog;
    // Pairing and the service switch (not a service object: QML only)
    ServicePairing servicePairing;
    DashboardManager dashboardManager;
    // The simulated car, started by --sim / --dev or the adapter picker.
    Simulator sim;
    DemoDrive demoDrive;
    // Installed frontends (<config>/frontends) and which one is the screen
    FrontendManager frontendManager;

private:
    void expose(const char *name, QObject *object);
    void wireGauges();
    void wirePhoneMirror();
    void wireAnalyzer();
    void wireVolume();
    void wireInputs();
    void wireKnobLed();

    float m_phoneDuck = 1.0f;    // phone mirror's last ducking factor (wirePhoneMirror)
};

#endif // OCTAVE_APPCORE_H
