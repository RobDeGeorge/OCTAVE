// WebAssembly build: no Bluetooth in a browser, so OBDAdapterFinder is the
// unsupported shell it is on every non-BlueZ platform (supported == false, the
// QML sheet falls back to obdManager.availableAdapters), without linking Qt
// Bluetooth. Replaces src/managers/obdadapterfinder.cpp when OCTAVE_WASM.

#include "../../managers/obdadapterfinder.h"

OBDAdapterFinder::OBDAdapterFinder(OBDManager *obd, SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_obd(obd)
    , m_settings(settings)
{
}

OBDAdapterFinder::~OBDAdapterFinder() = default;

bool OBDAdapterFinder::supported() const { return false; }
bool OBDAdapterFinder::bluetoothOn() const { return false; }
bool OBDAdapterFinder::looksLikeObd(const QString &, const QStringList &) { return false; }

void OBDAdapterFinder::startScan() {}
void OBDAdapterFinder::stopScan() {}
void OBDAdapterFinder::connectAdapter(const QString &) {}
void OBDAdapterFinder::cancel() {}
void OBDAdapterFinder::powerOn() {}
