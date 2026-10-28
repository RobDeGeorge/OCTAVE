#include "uiwatchdog.h"

#include "logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QProcess>
#include <QStandardPaths>

#include <cstdlib>

#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#include <unistd.h>
#endif

#ifdef OCTAVE_WASM
#include <atomic>
#include <emscripten.h>
#include <emscripten/bind.h>
#endif

Q_LOGGING_CATEGORY(lcWatchdog, "octave.ui_watchdog")

#ifdef OCTAVE_WASM
namespace {
// The browser all but stops a hidden page's main thread (phone locked, tab
// in the background): that is no hang. Set from the main thread's
// visibilitychange; read by the watchdog thread.
std::atomic<bool> g_pageHidden{false};
std::atomic<bool> g_hiddenSinceCheck{false};
} // namespace

void pageHidden(bool hidden)
{
    g_pageHidden = hidden;
    if (hidden)
        g_hiddenSinceCheck = true;
}

EMSCRIPTEN_BINDINGS(octave_watchdog)
{
    emscripten::function("octaveWatchdogPageHidden", &pageHidden);
}

EM_JS(void, octave_watchdog_watch_visibility, (), {
    const report = function () { Module.octaveWatchdogPageHidden(!!document.hidden); };
    document.addEventListener("visibilitychange", report);
    report();
});
#endif

UiWatchdog::UiWatchdog(QObject *parent, int pingMs, int stallMs, int dumpMs, int abortMs,
                       int startupGraceMs)
    : QObject(parent), m_pingMs(pingMs), m_stallMs(stallMs), m_dumpMs(dumpMs),
      m_abortMs(abortMs), m_startupGraceMs(startupGraceMs)
{
}

UiWatchdog::~UiWatchdog()
{
    stop();
}

void UiWatchdog::start()
{
    if (m_running.exchange(true))
        return;
    m_clock.start();
    m_lastPong = 0;
#ifdef OCTAVE_WASM
    octave_watchdog_watch_visibility();
#endif
    // The worker owns nothing Qt-side; it only posts queued calls to `this`,
    // which lives on the GUI thread.
    QObject::connect(&m_thread, &QThread::started, [this] { loop(); });
    m_thread.setObjectName(QStringLiteral("ui-watchdog"));
    m_thread.start();
}

void UiWatchdog::stop()
{
    if (!m_running.exchange(false))
        return;
    m_thread.quit();
    m_thread.wait(2000);
}

void UiWatchdog::loop()
{
    bool stalled = false;
    bool dumped = false;
    bool graceLogged = false;
    bool browserPaused = false;   // wasm: the stall is a hidden page
    qint64 stallStart = 0;
    while (m_running.load()) {
        QThread::msleep(static_cast<unsigned long>(m_pingMs));
        if (!m_running.load())
            break;
        // Ping: runs on the GUI thread when the event loop gets to it
        QMetaObject::invokeMethod(this, [this] { m_lastPong = m_clock.elapsed(); },
                                  Qt::QueuedConnection);
        const qint64 now = m_clock.elapsed();
        const qint64 silent = now - m_lastPong.load();
#ifdef OCTAVE_WASM
        // Hidden now, or at any point since the last pong: say so once, at
        // info level, rather than report the pause as a hang
        if (silent > m_stallMs && (g_pageHidden.load() || g_hiddenSinceCheck.load())) {
            if (!stalled)
                qCInfo(lcWatchdog) << "page hidden; the browser paused it";
            stalled = true;
            browserPaused = true;
            stallStart = m_lastPong.load();
            continue;
        }
        if (silent <= m_pingMs)
            g_hiddenSinceCheck = false;
#endif
        if (!stalled && silent > m_stallMs) {
            stalled = true;
            stallStart = m_lastPong.load();
            qCWarning(lcWatchdog) << "UI thread blocked for" << silent / 1000.0 << "s and counting";
        } else if (stalled && silent <= m_pingMs) {
            stalled = false;
            dumped = false;
            graceLogged = false;
            if (browserPaused)
                qCInfo(lcWatchdog) << "page running again after" << (m_lastPong.load() - stallStart) / 1000.0
                                   << "s";
            else
                qCWarning(lcWatchdog) << "UI thread responsive again after"
                                      << (m_lastPong.load() - stallStart) / 1000.0 << "s";
            browserPaused = false;
        }
        if (!stalled)
            continue;

#ifdef Q_OS_LINUX
        if (!dumped && silent > m_dumpMs) {
            dumped = true;
            const QString path = dumpStacks();
            if (!path.isEmpty())
                qCWarning(lcWatchdog) << "UI thread still blocked; thread stacks written to" << path;
        }
        if (m_abortMs > 0 && m_clock.elapsed() - m_lastPong.load() > m_abortMs) {
            if (now < m_startupGraceMs) {
                if (!graceLogged) {
                    graceLogged = true;
                    qCWarning(lcWatchdog) << "UI thread blocked during startup; holding off the restart until"
                                          << m_startupGraceMs / 1000 << "s after launch";
                }
                continue;
            }
            qCCritical(lcWatchdog) << "UI thread blocked for"
                                   << (m_clock.elapsed() - m_lastPong.load()) / 1000.0
                                   << "s; aborting so the launcher restarts OCTAVE";
            // No OctaveLog::flush(): it locks a mutex the hung GUI thread may
            // hold, and every log line is already flushed as it is written.
            std::abort();
        }
#endif
    }
}

QString UiWatchdog::dumpStacks()
{
#ifdef Q_OS_LINUX
    const QString gdb = QStandardPaths::findExecutable(QStringLiteral("gdb"));
    if (gdb.isEmpty()) {
        qCWarning(lcWatchdog) << "gdb not installed; no stack dump for this hang";
        return {};
    }
    const QString path = OctaveLog::logDir() + QStringLiteral("/hang-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".txt");

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.setStandardOutputFile(path);
    // Yama (ptrace_scope=1) stops a child attaching to its parent unless the
    // parent names it as its tracer. sh waits on stdin until we have done so,
    // then execs gdb in place, so the pid we named is gdb's.
    const QString script = QStringLiteral(
        "read _ && exec \"$0\" -p \"$1\" -batch -nx"
        " -ex 'set debuginfod enabled off' -ex 'set pagination off'"
        " -ex 'info threads' -ex 'thread apply all bt'");
    proc.start(QStringLiteral("/bin/sh"),
               {QStringLiteral("-c"), script, gdb, QString::number(::getpid())});
    if (!proc.waitForStarted(3000)) {
        qCWarning(lcWatchdog) << "could not start gdb for a stack dump";
        return {};
    }
    ::prctl(PR_SET_PTRACER, static_cast<unsigned long>(proc.processId()), 0, 0, 0);
    proc.write("\n");
    proc.waitForBytesWritten(1000);
    proc.closeWriteChannel();
    // A thread stuck in the kernel (D state) can stall the attach too
    if (!proc.waitForFinished(30000)) {
        proc.kill();
        proc.waitForFinished(2000);
        qCWarning(lcWatchdog) << "gdb stack dump timed out; partial output in" << path;
    }
    ::prctl(PR_SET_PTRACER, 0, 0, 0, 0);
    return path;
#else
    return {};
#endif
}
