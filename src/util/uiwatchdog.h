#ifndef OCTAVE_UIWATCHDOG_H
#define OCTAVE_UIWATCHDOG_H

// Detects a hung Qt event loop and says so in the log. A worker thread
// posts a ping to the GUI thread every pingMs; if the GUI thread has not
// answered within stallMs a WARNING is logged (once per stall, with the
// duration when it recovers), so a frozen head unit leaves a trace.
// Mirrors backend/ui_watchdog.py.
//
// On Linux it also acts on a long stall: after dumpMs it attaches gdb and
// writes every thread's stack to <log dir>/hang-<time>.txt, and after abortMs
// it aborts so the launcher (octave-start.sh) restarts the app instead of
// leaving a frozen screen that only a power pull clears. The abort is held
// off during the first startupGraceMs, when a cold library scan can
// legitimately block the GUI thread for a while. (The Python mirror only logs.)

#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <atomic>

class UiWatchdog : public QObject
{
    Q_OBJECT
public:
    explicit UiWatchdog(QObject *parent = nullptr, int pingMs = 2000, int stallMs = 5000,
                        int dumpMs = 15000, int abortMs = 30000, int startupGraceMs = 120000);
    ~UiWatchdog() override;

    void start();
    void stop();

private:
    void loop();
    // Writes all thread stacks via gdb; returns the file path, or empty on failure
    QString dumpStacks();

    const int m_pingMs;
    const int m_stallMs;
    const int m_dumpMs;
    const int m_abortMs;
    const int m_startupGraceMs;
    std::atomic<bool> m_running{false};
    // Monotonic (a wall-clock jump, e.g. NTP setting the RTC-less Pi's clock,
    // must not read as a stall); m_lastPong is ms on m_clock, written on the GUI thread
    QElapsedTimer m_clock;
    std::atomic<qint64> m_lastPong{0};
    QThread m_thread;
};

#endif  // OCTAVE_UIWATCHDOG_H
