#include "logger.h"
#include "../managers/settingsmanager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <cstdio>
#include <cstring>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>     // _O_WRONLY / _O_APPEND / _O_CREAT for the crash-log fd
#elif defined(Q_OS_UNIX)
#  include <csignal>
#  include <fcntl.h>
#  include <unistd.h>
#  if defined(__GLIBC__) || defined(Q_OS_MACOS)
#    include <execinfo.h>
#    define OCTAVE_HAVE_BACKTRACE 1
#  endif
#endif

namespace {

// The crash handler's own descriptor follows the error log through a
// rotation (defined with the handlers below)
void reopenCrashFd(const QString &rotatedPath);
// One line to that descriptor: no lock, no Qt, no buffering
void writeToCrashFd(const char *text, size_t length);

// One log file, rotated at maxBytes into `backups` numbered copies
struct RotatingFile {
    QString path;
    qint64 maxBytes = 0;
    int backups = 0;
    QFile file;

    void open()
    {
        file.setFileName(path);
        (void)file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }

    void write(const QByteArray &line)
    {
        if (!file.isOpen())
            return;
        if (file.size() + line.size() > maxBytes)
            rotate();
        file.write(line);
        file.flush();
    }

    void rotate()
    {
        file.close();
        // octave.log.3 -> gone, .2 -> .3, .1 -> .2, octave.log -> .1
        QFile::remove(path + QStringLiteral(".%1").arg(backups));
        for (int i = backups - 1; i >= 1; --i)
            QFile::rename(path + QStringLiteral(".%1").arg(i), path + QStringLiteral(".%1").arg(i + 1));
        if (backups > 0)
            QFile::rename(path, path + QStringLiteral(".1"));
        else
            QFile::remove(path);
        open();
        reopenCrashFd(path);
    }
};

// Recursive on purpose: anything Qt warns about while the lock is held
// (a QFile call on a closed file, for instance) re-enters handler() on the
// same thread, and a plain QMutex would deadlock the app there.
QRecursiveMutex g_mutex;
RotatingFile g_main;    // info and above          5 MB x 3
RotatingFile g_error;   // warning and above        2 MB x 5
RotatingFile g_debug;   // everything, --debug only 10 MB x 2
bool g_debugEnabled = false;
bool g_installed = false;
QString g_logDir;
constexpr int kLockWaitMs = 2000;
int g_crashFd = -1;     // raw descriptor of the error log for the signal path
[[maybe_unused]] QtMessageHandler g_previous = nullptr;   // used where stderr goes nowhere (phones)

const char *levelName(QtMsgType t)
{
    switch (t) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO";
    case QtWarningMsg:  return "WARNING";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "?";
}

void handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    // Line format:
    // 2026-09-10 12:00:00 | INFO    | octave.media | file.cpp:123 | message
    const QString category = ctx.category ? QString::fromLatin1(ctx.category) : QStringLiteral("default");
    QString where;
    if (ctx.file) {
        QString f = QString::fromUtf8(ctx.file);
        const int slash = f.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0)
            f = f.mid(slash + 1);
        where = QStringLiteral("%1:%2").arg(f).arg(ctx.line);
    }
    const QByteArray line = QStringLiteral("%1 | %2 | %3 | %4 | %5\n")
                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                     QString::fromLatin1(levelName(type)).leftJustified(7), category,
                                     where.isEmpty() ? QStringLiteral("-") : where, msg)
                                .toUtf8();

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // stderr goes nowhere on a phone: hand the message to the handler we
    // replaced (Qt's default) so it still reaches logcat / os_log.
    if (g_previous)
        g_previous(type, ctx, msg);
#else
    // Terminal, as before the handler existed. Deliberately NOT chained to
    // g_previous here: Qt's default handler also prints to stderr and every
    // line would show up twice — the file-format line above replaces it.
    std::fputs(line.constData(), stderr);
    std::fflush(stderr);
#endif

    // The lock is held across file writes. A thread stuck in one (a dying
    // SD card) must not take every other thread that logs with it, the UI
    // watchdog first of all: after kLockWaitMs the line goes straight to the
    // error log's own descriptor instead
    if (g_mutex.tryLock(kLockWaitMs)) {
        if (g_debugEnabled)
            g_debug.write(line);
        if (type != QtDebugMsg)
            g_main.write(line);
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
            g_error.write(line);
        g_mutex.unlock();
    } else if (type != QtDebugMsg) {
        writeToCrashFd(line.constData(), size_t(line.size()));
    }
    // Qt aborts after a fatal message returns (SIGABRT -> crashHandler below
    // appends the backtrace), so make sure everything is on disk first.
    if (type == QtFatalMsg)
        OctaveLog::flush();
}

#if defined(Q_OS_UNIX)
void writeRaw(int fd, const char *s)
{
    if (fd >= 0)
        (void)::write(fd, s, std::strlen(s));
}

void writeToCrashFd(const char *text, size_t length)
{
    if (g_crashFd >= 0)
        (void)::write(g_crashFd, text, length);
}

// Async-signal-safe as far as practical: raw write() of a fixed line plus
// backtrace_symbols_fd(); then restore the default action and re-raise so
// the OS still produces a core / tombstone. No Qt in here — that includes
// OctaveLog::flush() (QMutex + QFile); the log files need no flush anyway
// because RotatingFile::write() flushes every line as it lands.
void crashHandler(int sig)
{
    const char *name = "signal";
    switch (sig) {
    case SIGSEGV: name = "SIGSEGV"; break;
    case SIGABRT: name = "SIGABRT"; break;
    case SIGFPE:  name = "SIGFPE"; break;
    case SIGILL:  name = "SIGILL"; break;
    case SIGBUS:  name = "SIGBUS"; break;
    default: break;
    }
    writeRaw(g_crashFd, "\n==== FATAL ");
    writeRaw(g_crashFd, name);
    writeRaw(g_crashFd, " - backtrace (addresses; symbolise with addr2line against this build) ====\n");
    writeRaw(2, "\n==== FATAL ");
    writeRaw(2, name);
    writeRaw(2, " ====\n");
#ifdef OCTAVE_HAVE_BACKTRACE
    void *frames[64];
    const int n = backtrace(frames, 64);
    if (g_crashFd >= 0)
        backtrace_symbols_fd(frames, n, g_crashFd);
    backtrace_symbols_fd(frames, n, 2);
#else
    writeRaw(g_crashFd, "(no backtrace support on this platform)\n");
#endif
    writeRaw(g_crashFd, "==== end ====\n");
    // The default action again, then the same signal: it is delivered when
    // this handler returns (it is blocked meanwhile), so the OS still
    // writes its core / tombstone
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

// After the error log rotates, the descriptor opened at start-up points at
// octave-cpp-error.log.1 (later .2 ... then a deleted file): a crash's
// backtrace went where nobody looks. Open the new file, swap, close the old.
void reopenCrashFd(const QString &rotatedPath)
{
    if (g_crashFd < 0 || !rotatedPath.endsWith(QLatin1String("/octave-cpp-error.log")))
        return;
    const int fresh = ::open(rotatedPath.toLocal8Bit().constData(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fresh < 0)
        return;
    const int old = g_crashFd;
    g_crashFd = fresh;
    ::close(old);
}

void installCrashHandlers()
{
    // A separate descriptor so the signal path never touches Qt
    g_crashFd = ::open((g_logDir + QStringLiteral("/octave-cpp-error.log")).toLocal8Bit().constData(),
                       O_WRONLY | O_APPEND | O_CREAT, 0644);
#ifdef OCTAVE_HAVE_BACKTRACE
    // backtrace() loads its unwinder on the first call (dlopen, malloc):
    // have that happen now, not inside a signal handler
    void *warm[2];
    (void)backtrace(warm, 2);
#endif
#if defined(OCTAVE_WASM)
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS})
        ::signal(sig, crashHandler);
#else
    // A stack of the handler's own for this (the GUI) thread. A stack
    // overflow, runaway recursion in QML or C++, is a SIGSEGV with no room
    // left on the thread's stack: without this the handler could not run
    // and the process died with nothing in the log. Other threads have no
    // alternate stack, so there the handler runs on the thread's own, as
    // it always did (SA_ONSTACK means "if there is one")
    static char *altStack = nullptr;
    const size_t altSize = qMax<size_t>(size_t(SIGSTKSZ), 64 * 1024);
    if (!altStack) {
        altStack = new char[altSize];   // for the life of the process
        stack_t ss{};
        ss.ss_sp = altStack;
        ss.ss_size = altSize;
        ss.ss_flags = 0;
        (void)::sigaltstack(&ss, nullptr);
    }
    struct sigaction action{};
    action.sa_handler = crashHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_ONSTACK;
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS})
        (void)::sigaction(sig, &action, nullptr);
#endif
}
#elif defined(Q_OS_WIN)
LONG WINAPI crashFilter(EXCEPTION_POINTERS *info)
{
    char buf[256];
    std::snprintf(buf, sizeof buf, "\n==== FATAL exception 0x%08lx at %p ====\n",
                  info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0ul,
                  info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr);
    if (g_crashFd >= 0) {
        _write(g_crashFd, buf, int(std::strlen(buf)));
        void *frames[62];
        const USHORT n = CaptureStackBackTrace(0, 62, frames, nullptr);
        for (USHORT i = 0; i < n; ++i) {
            std::snprintf(buf, sizeof buf, "  #%02u %p\n", unsigned(i), frames[i]);
            _write(g_crashFd, buf, int(std::strlen(buf)));
        }
        _write(g_crashFd, "==== end ====\n", 14);
    }
    std::fputs(buf, stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

void writeToCrashFd(const char *text, size_t length)
{
    if (g_crashFd >= 0)
        _write(g_crashFd, text, static_cast<unsigned int>(length));
}

void reopenCrashFd(const QString &rotatedPath)
{
    if (g_crashFd < 0 || !rotatedPath.endsWith(QLatin1String("/octave-cpp-error.log")))
        return;
    const int fresh = _open(rotatedPath.toLocal8Bit().constData(), _O_WRONLY | _O_APPEND | _O_CREAT, 0644);
    if (fresh < 0)
        return;
    const int old = g_crashFd;
    g_crashFd = fresh;
    _close(old);
}

void installCrashHandlers()
{
    g_crashFd = _open((g_logDir + QStringLiteral("/octave-cpp-error.log")).toLocal8Bit().constData(),
                      _O_WRONLY | _O_APPEND | _O_CREAT, 0644);
    SetUnhandledExceptionFilter(crashFilter);
}
#else
void reopenCrashFd(const QString &) {}
void writeToCrashFd(const char *, size_t) {}
void installCrashHandlers() {}
#endif

}  // namespace

namespace OctaveLog {

void install(bool debug)
{
    if (g_installed)
        return;
    g_installed = true;
    g_debugEnabled = debug;
    g_logDir = SettingsManager::getAppDataDir() + QStringLiteral("/logs");
    QDir().mkpath(g_logDir);

    g_main.path  = g_logDir + QStringLiteral("/octave-cpp.log");       g_main.maxBytes  = 5 * 1024 * 1024;  g_main.backups  = 3;
    g_error.path = g_logDir + QStringLiteral("/octave-cpp-error.log"); g_error.maxBytes = 2 * 1024 * 1024;  g_error.backups = 5;
    g_debug.path = g_logDir + QStringLiteral("/octave-cpp-debug.log"); g_debug.maxBytes = 10 * 1024 * 1024; g_debug.backups = 2;
    g_main.open();
    g_error.open();
    if (debug)
        g_debug.open();

    // Category debug output only costs something when it is written somewhere
    QLoggingCategory::setFilterRules(debug ? QStringLiteral("*.debug=true\nqt.*.debug=false")
                                           : QStringLiteral("*.debug=false"));

    g_previous = qInstallMessageHandler(handler);
    installCrashHandlers();

    qInfo("OCTAVE C++ logging to %s (debug %s)", qPrintable(g_logDir), debug ? "on" : "off");
}

QString logDir() { return g_logDir; }

void writeDirect(const char *line)
{
    const size_t length = std::strlen(line);
    writeToCrashFd(line, length);
#if defined(Q_OS_UNIX)
    (void)::write(2, line, length);
#else
    std::fputs(line, stderr);
#endif
}

void flush()
{
    QMutexLocker<QRecursiveMutex> lock(&g_mutex);
    // Only open files: QFile::flush() on a closed one (the debug log when
    // --debug is off) emits a qWarning, which used to self-deadlock here.
    for (RotatingFile *f : {&g_main, &g_error, &g_debug})
        if (f->file.isOpen())
            f->file.flush();
}

}  // namespace OctaveLog
