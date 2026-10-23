#ifndef OCTAVE_DEV_COREACTIONS_H
#define OCTAVE_DEV_COREACTIONS_H

#include <QObject>

namespace OctaveDev {

}
namespace OctaveService { class CommandServer; }
namespace OctaveDev {
using OctaveService::CommandServer;
class PerfMonitor;
class UiDriver;

struct CoreTargets {
    UiDriver *ui = nullptr;
    PerfMonitor *perf = nullptr;     // null: get_metrics reports "perf monitor not active"
    QObject *settings = nullptr;     // settingsManager
    QObject *media = nullptr;        // mediaManager
    QObject *volume = nullptr;       // volumeController
    QObject *nowPlaying = nullptr;   // nowPlaying
    QObject *obd = nullptr;          // obdManager
};

// The command server's core actions: perf metrics, navigation,
// screenshots / recording, UI driving, settings, media and OBD controls.
// Names, arguments and result shapes are what the dev/ clients (MCP
// servers, screenshot tools) expect; keep them stable.
void registerCoreActions(CommandServer &server, const CoreTargets &targets);

} // namespace OctaveDev

#endif // OCTAVE_DEV_COREACTIONS_H
