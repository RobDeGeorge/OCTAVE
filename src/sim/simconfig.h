#ifndef OCTAVE_SIM_SIMCONFIG_H
#define OCTAVE_SIM_SIMCONFIG_H

// The config a `--sim` run starts on when it is not given `--config DIR`:
// a fresh temporary folder, so trying the simulator never reads or writes a
// real install. Seeded with settings that keep the head unit's own
// hardware alone (ESP32 knob, gesture sensor) and a music folder of a few
// generated test tracks, so a frontend has something to play.

#include <QString>

namespace OctaveSim {

// Writes settingsConfigure.json into `configDir` (the OCTAVE config folder)
// and the test tracks into `musicDir`. False if either could not be written.
bool seedThrowawayConfig(const QString &configDir, const QString &musicDir);

// Three short WAV tracks (tones, with title / artist / album in a RIFF
// INFO chunk) into `dir`; the file names they got
QStringList writeTestTracks(const QString &dir);

} // namespace OctaveSim

#endif // OCTAVE_SIM_SIMCONFIG_H
