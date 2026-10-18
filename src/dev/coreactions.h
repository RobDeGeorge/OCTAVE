#ifndef OCTAVE_DEV_COREACTIONS_H
#define OCTAVE_DEV_COREACTIONS_H

#include <QObject>

namespace OctaveDev {

class CommandServer;
class PerfMonitor;
class UiDriver;

struct CoreTargets {
    UiDriver *ui = nullptr;
    PerfMonitor *perf = nullptr;     // null: get_metrics reports "perf monitor not active"
    QObject *settings = nullptr;     // settingsManager
    QObject *media = nullptr;        // mediaManager
    QObject *obd = nullptr;          // obdManager
};

// The actions of backend/command_server.py: perf metrics, navigation,
// screenshots / recording, UI driving, settings, media and OBD controls.
// Names, arguments and result shapes match the Python server.
void registerCoreActions(CommandServer &server, const CoreTargets &targets);

} // namespace OctaveDev

#endif // OCTAVE_DEV_COREACTIONS_H
