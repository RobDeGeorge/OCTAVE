#ifndef OCTAVE_UIWATCHDOG_H
#define OCTAVE_UIWATCHDOG_H

// Detects a hung Qt event loop and says so in the log. A worker thread
// posts a ping to the GUI thread every pingMs; if the GUI thread has not
// answered within stallMs a WARNING is logged (once per stall, with the
// duration when it recovers), so a frozen head unit leaves a trace.
// Mirrors backend/ui_watchdog.py.

#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <atomic>

class UiWatchdog : public QObject
{
    Q_OBJECT
public:
    explicit UiWatchdog(QObject *parent = nullptr, int pingMs = 2000, int stallMs = 5000);
    ~UiWatchdog() override;

    void start();
    void stop();

private:
    void loop();

    const int m_pingMs;
    const int m_stallMs;
    std::atomic<bool> m_running{false};
    // Monotonic (a wall-clock jump, e.g. NTP setting the RTC-less Pi's clock,
    // must not read as a stall); m_lastPong is ms on m_clock, written on the GUI thread
    QElapsedTimer m_clock;
    std::atomic<qint64> m_lastPong{0};
    QThread m_thread;
};

#endif  // OCTAVE_UIWATCHDOG_H
