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

// Flush all files. Called from main.cpp's aboutToQuit handler (normal exit)
// and by the message handler after a QtFatalMsg, right before Qt aborts.
// Never called from the crash (signal) handlers: those must stay Qt-free,
// and every line is already flushed as it is written.
void flush();

}  // namespace OctaveLog

#endif  // OCTAVE_LOGGER_H
