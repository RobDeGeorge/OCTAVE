#ifndef OCTAVE_LOGGER_H
#define OCTAVE_LOGGER_H

// Persistent logging for the C++ build. Mirrors backend/logging_config.py:
// a Qt message handler routes every qDebug/qInfo/qWarning/qCritical/qFatal
// and every QML console.* call into rotating files under
// <app data dir>/logs/ (octave-cpp.log, octave-cpp-error.log and, with
// --debug, octave-cpp-debug.log), while still echoing to stderr. Crash
// handlers write the signal / exception and a backtrace to the error log so
// a segfault in the vehicle leaves a trace.

#include <QString>

namespace OctaveLog {

// Install the message handler and crash handlers. Call before the
// QGuiApplication is constructed. debug=true also writes QtDebugMsg to the
// debug file (and keeps QLoggingCategory debug output enabled).
void install(bool debug);

// Directory the log files live in (created on install).
QString logDir();

// Flush all files (called on normal exit and from the fatal path).
void flush();

}  // namespace OctaveLog

#endif  // OCTAVE_LOGGER_H
