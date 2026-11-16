#ifndef OCTAVE_LOGGER_H
#define OCTAVE_LOGGER_H

// Persistent logging. A Qt message handler routes every qDebug/qInfo/qWarning/qCritical/qFatal
// and every QML console.* call into rotating files under
// <app data dir>/logs/ (octave-cpp.log, octave-cpp-error.log and, with
// --debug, octave-cpp-debug.log), while still echoing to stderr. Crash
// handlers write the signal / exception and a backtrace to the error log so
// a segfault in the vehicle leaves a trace (on the GUI thread a stack
// overflow too: the handlers have a stack of their own there).

#include <QString>

namespace OctaveLog {

// Install the message handler and crash handlers. Call before the
// QGuiApplication is constructed. debug=true also writes QtDebugMsg to the
// debug file (and keeps QLoggingCategory debug output enabled).
void install(bool debug);

// Directory the log files live in (created on install).
QString logDir();

// Flush all files. Called from main.cpp's aboutToQuit handler (normal exit)
// and by the message handler after a QtFatalMsg, right before Qt aborts.
// Never called from the crash (signal) handlers: those must stay Qt-free,
// and every line is already flushed as it is written.
void flush();

// One finished line (end it with '\n') straight to the error log and to
// stderr: no lock, no Qt, no formatting. For a caller that cannot afford to
// wait on the log lock: the UI watchdog's last line before it aborts, when
// the thread it watches may hold that lock. Everything else logs through Qt.
void writeDirect(const char *line);

}  // namespace OctaveLog

#endif  // OCTAVE_LOGGER_H
