#ifndef OCTAVE_DEV_PERFMONITOR_H
#define OCTAVE_DEV_PERFMONITOR_H

// Performance monitor for `octave --profile`. Measures event-loop latency
// (how late a 5 ms timer fires), background-thread wake jitter, process CPU / RSS / thread
// count, and the per-label timings recorded by OCTAVE_PERF_SCOPE
// (src/util/perfscope.h).
// Prints a status line every second and writes dev/perf/logs/perf_<stamp>
// .csv and .log in the formats the MCP perf tools and saved baselines read.
//
// The wake-jitter probe (a thread that sleeps 10 ms and measures how late it
// wakes) reports under the "GIL" name and gil_* keys those tools expect; it
// measures scheduler / CPU contention.

#include <QFile>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <atomic>
#include <thread>

namespace OctaveDev {

// Fixed-size sample window with percentiles
class RingBuffer
{
public:
    explicit RingBuffer(int capacity = 300) : m_capacity(capacity) {}
    void push(double v);
    double percentile(double p) const;
    double mean() const;
    double max() const;
    int count() const { return static_cast<int>(m_samples.size()); }
    void clear() { m_samples.clear(); m_next = 0; }

private:
    int m_capacity;
    std::vector<double> m_samples;
    size_t m_next = 0;
};

class PerfMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double loopLatencyMs READ loopLatencyMs NOTIFY metricsUpdated)
    Q_PROPERTY(double loopP99Ms READ loopP99Ms NOTIFY metricsUpdated)
    Q_PROPERTY(double gilJitterMs READ gilJitterMs NOTIFY metricsUpdated)
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY metricsUpdated)
    Q_PROPERTY(double memMb READ memMb NOTIFY metricsUpdated)
    Q_PROPERTY(int threadCount READ threadCount NOTIFY metricsUpdated)
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY metricsUpdated)

public:
    PerfMonitor(const QString &csvPath, const QString &logPath, QObject *parent = nullptr);
    ~PerfMonitor() override;

    void start();
    void stop();

    double loopLatencyMs() const { return m_loopAvg; }
    double loopP99Ms() const { return m_loopP99; }
    double gilJitterMs() const { return m_jitterAvg; }
    double cpuPercent() const { return m_cpu; }
    double memMb() const { return m_mem; }
    int threadCount() const { return m_threads; }
    QString summaryText() const { return m_summary; }

    Q_INVOKABLE QString getDetailedReport() const;

    // get_metrics / get_slot_timings payloads (thread-safe snapshots)
    QVariantMap metrics() const;
    static QVariantMap slotTimings();

signals:
    void metricsUpdated();

private slots:
    void onProbe();
    void onReport();

private:
    static constexpr int kProbeIntervalMs = 5;

    void sampleProcess();

    QTimer m_probeTimer;
    QTimer m_reportTimer;
    qint64 m_lastProbeNs = 0;
    RingBuffer m_loopBuf{1000};   // 5 s of 5 ms ticks, cleared every report

    // Wake-jitter probe thread
    std::thread m_jitterThread;
    std::atomic<bool> m_jitterStop{false};

    QFile m_csv;
    QFile m_log;
    int m_reportCount = 0;

    // Previous CPU sample for the delta
    double m_lastCpuS = -1;
    qint64 m_lastWallNs = 0;

    // Published metrics (written on the GUI thread, read by the command
    // server under s_metricsLock)
    double m_loopAvg = 0, m_loopP99 = 0, m_jitterAvg = 0, m_cpu = 0, m_mem = 0;
    int m_threads = 0;
    QString m_summary;
};

} // namespace OctaveDev

#endif // OCTAVE_DEV_PERFMONITOR_H
