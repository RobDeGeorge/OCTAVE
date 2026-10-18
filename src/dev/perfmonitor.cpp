#include "perfmonitor.h"

#include "../util/perfscope.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QSysInfo>
#include <QTextStream>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>

#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
#include <unistd.h>
#elif defined(Q_OS_UNIX)
#include <sys/resource.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX   // windows.h's min/max macros break std::max
#endif
#include <windows.h>
#include <psapi.h>
#endif

// ---------------------------------------------------------------------------
// OCTAVE_PERF_SCOPE registry (src/util/perfscope.h)
// ---------------------------------------------------------------------------

namespace {
QMutex s_slotLock;
QHash<QString, OctaveDev::RingBuffer> &slotBuffers()
{
    static QHash<QString, OctaveDev::RingBuffer> buffers;
    return buffers;
}

QMutex s_jitterLock;
OctaveDev::RingBuffer s_jitter(300);

QMutex s_metricsLock;

qint64 monotonicNs()
{
    static QElapsedTimer clock = [] { QElapsedTimer t; t.start(); return t; }();
    return clock.nsecsElapsed();
}

double roundTo(double v, int places)
{
    const double f = std::pow(10.0, places);
    return std::round(v * f) / f;
}
} // namespace

namespace OctavePerf {
std::atomic<bool> g_enabled{false};

void record(const char *label, double ms)
{
    QMutexLocker l(&s_slotLock);
    auto it = slotBuffers().find(QString::fromLatin1(label));
    if (it == slotBuffers().end())
        it = slotBuffers().insert(QString::fromLatin1(label), OctaveDev::RingBuffer(200));
    it->push(ms);
}
} // namespace OctavePerf

namespace OctaveDev {

// ---------------------------------------------------------------------------
// RingBuffer
// ---------------------------------------------------------------------------

void RingBuffer::push(double v)
{
    if (static_cast<int>(m_samples.size()) < m_capacity) {
        m_samples.push_back(v);
    } else {
        m_samples[m_next] = v;
        m_next = (m_next + 1) % m_samples.size();
    }
}

double RingBuffer::percentile(double p) const
{
    if (m_samples.empty())
        return 0.0;
    std::vector<double> s = m_samples;
    std::sort(s.begin(), s.end());
    const double k = (s.size() - 1) * (p / 100.0);
    const size_t f = static_cast<size_t>(k);
    if (f + 1 >= s.size())
        return s[f];
    return s[f] + (k - f) * (s[f + 1] - s[f]);
}

double RingBuffer::mean() const
{
    if (m_samples.empty())
        return 0.0;
    return std::accumulate(m_samples.begin(), m_samples.end(), 0.0) / m_samples.size();
}

double RingBuffer::max() const
{
    return m_samples.empty() ? 0.0 : *std::max_element(m_samples.begin(), m_samples.end());
}

// ---------------------------------------------------------------------------
// PerfMonitor
// ---------------------------------------------------------------------------

PerfMonitor::PerfMonitor(const QString &csvPath, const QString &logPath, QObject *parent)
    : QObject(parent)
{
    // A 5 ms precise timer; how late each tick fires is the loop latency.
    // (A 0 ms timer timing the gap between firings would keep the loop
    // spinning: a whole core, and it skews the CPU numbers.)
    m_probeTimer.setInterval(kProbeIntervalMs);
    m_probeTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_probeTimer, &QTimer::timeout, this, &PerfMonitor::onProbe);

    m_reportTimer.setInterval(1000);
    connect(&m_reportTimer, &QTimer::timeout, this, &PerfMonitor::onReport);

    if (!csvPath.isEmpty()) {
        m_csv.setFileName(csvPath);
        if (m_csv.open(QIODevice::WriteOnly | QIODevice::Text))
            m_csv.write("timestamp,loop_avg_ms,loop_p50_ms,loop_p95_ms,loop_p99_ms,loop_max_ms,"
                        "gil_avg_ms,gil_p99_ms,cpu_pct,mem_mb,threads\n");
    }
    if (!logPath.isEmpty()) {
        m_log.setFileName(logPath);
        if (m_log.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&m_log);
            out << "OCTAVE Performance Log — " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
            out << "Platform: " << QSysInfo::prettyProductName() << " (" << QSysInfo::kernelType() << " "
                << QSysInfo::kernelVersion() << ", " << QSysInfo::currentCpuArchitecture() << ")\n";
            out << "Backend: C++ (Qt " << qVersion() << ")\n";
            out << "Process stats: native\n";
            out << QString(90, QLatin1Char('=')) << "\n\n";
        }
    }
}

PerfMonitor::~PerfMonitor()
{
    stop();
}

void PerfMonitor::start()
{
    OctavePerf::g_enabled.store(true);
    m_jitterStop = false;
    m_jitterThread = std::thread([this] {
        // Sleep 10 ms, record how late the wake-up was
        constexpr auto interval = std::chrono::milliseconds(10);
        while (!m_jitterStop.load()) {
            const qint64 t0 = monotonicNs();
            std::this_thread::sleep_for(interval);
            const double lateMs = std::max(0.0, (monotonicNs() - t0) / 1.0e6 - 10.0);
            QMutexLocker l(&s_jitterLock);
            s_jitter.push(lateMs);
        }
    });
    m_lastProbeNs = monotonicNs();
    m_probeTimer.start();
    m_reportTimer.start();
    sampleProcess();   // prime the CPU delta
    std::fprintf(stdout, "\n\033[1;36m[PERF]\033[0m  Performance monitor active. Metrics every 1 s.\n");
    std::fflush(stdout);
}

void PerfMonitor::stop()
{
    if (!m_reportTimer.isActive() && !m_jitterThread.joinable())
        return;
    OctavePerf::g_enabled.store(false);
    m_probeTimer.stop();
    m_reportTimer.stop();
    m_jitterStop = true;
    if (m_jitterThread.joinable())
        m_jitterThread.join();
    if (m_log.isOpen()) {
        QTextStream out(&m_log);
        out << "\n" << QString(90, QLatin1Char('=')) << "\n" << getDetailedReport() << "\n";
        m_log.close();
    }
    if (m_csv.isOpen())
        m_csv.close();
}

void PerfMonitor::onProbe()
{
    const qint64 now = monotonicNs();
    const double lateMs = std::max(0.0, (now - m_lastProbeNs) / 1.0e6 - kProbeIntervalMs);
    m_lastProbeNs = now;
    m_loopBuf.push(lateMs);
}

void PerfMonitor::sampleProcess()
{
    double cpuS = -1;
    double memMb = 0;
    int threads = 0;
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    QFile stat(QStringLiteral("/proc/self/stat"));
    if (stat.open(QIODevice::ReadOnly)) {
        const QByteArray line = stat.readAll();
        // Fields after the ")" of the command name: state is field 3, utime 14, stime 15
        const QList<QByteArray> f = line.mid(line.lastIndexOf(')') + 2).split(' ');
        if (f.size() > 12) {
            const double ticks = static_cast<double>(sysconf(_SC_CLK_TCK));
            cpuS = (f[11].toDouble() + f[12].toDouble()) / ticks;
        }
    }
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (statm.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> f = statm.readAll().split(' ');
        if (f.size() > 1)
            memMb = f[1].toDouble() * sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0);
    }
    QFile status(QStringLiteral("/proc/self/status"));
    if (status.open(QIODevice::ReadOnly)) {
        for (const QByteArray &l : status.readAll().split('\n')) {
            if (l.startsWith("Threads:")) {
                threads = l.mid(8).trimmed().toInt();
                break;
            }
        }
    }
#elif defined(Q_OS_UNIX)
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        cpuS = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
        memMb = ru.ru_maxrss / (1024.0 * 1024.0);   // peak, bytes on macOS
    }
#elif defined(Q_OS_WIN)
    FILETIME c, e, k, u;
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
        auto secs = [](const FILETIME &ft) {
            return ((static_cast<quint64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 1e7;
        };
        cpuS = secs(k) + secs(u);
    }
    PROCESS_MEMORY_COUNTERS pmc;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        memMb = pmc.WorkingSetSize / (1024.0 * 1024.0);
#endif
    const qint64 wall = monotonicNs();
    double cpu = m_cpu;
    if (cpuS >= 0 && m_lastCpuS >= 0 && wall > m_lastWallNs)
        cpu = 100.0 * (cpuS - m_lastCpuS) / ((wall - m_lastWallNs) / 1.0e9);   // psutil-style, may exceed 100
    m_lastCpuS = cpuS;
    m_lastWallNs = wall;

    QMutexLocker l(&s_metricsLock);
    m_cpu = cpu;
    m_mem = memMb;
    m_threads = threads;
}

void PerfMonitor::onReport()
{
    const double avg = m_loopBuf.mean();
    const double p50 = m_loopBuf.percentile(50);
    const double p95 = m_loopBuf.percentile(95);
    const double p99 = m_loopBuf.percentile(99);
    const double mx = m_loopBuf.max();
    double jAvg, jP99;
    {
        QMutexLocker l(&s_jitterLock);
        jAvg = s_jitter.mean();
        jP99 = s_jitter.percentile(99);
        s_jitter.clear();
    }
    sampleProcess();

    const QString summary = QString::asprintf("loop %.1fms avg  p99 %.1fms  jitter %.1fms  CPU %.0f%%  MEM %.0fMB  thr %d",
                                              avg, p99, jAvg, m_cpu, m_mem, m_threads);
    {
        QMutexLocker l(&s_metricsLock);
        m_loopAvg = avg;
        m_loopP99 = p99;
        m_jitterAvg = jAvg;
        m_summary = summary;
    }

    // Slots with p95 over 2 ms
    QStringList slow;
    {
        QMutexLocker l(&s_slotLock);
        for (auto it = slotBuffers().constBegin(); it != slotBuffers().constEnd(); ++it) {
            const double sp95 = it->percentile(95);
            if (sp95 > 2.0)
                slow << QString::asprintf("  %s: %.1fms avg  %.1fms p95", qPrintable(it.key()), it->mean(), sp95);
        }
    }

    const char *color = avg < 5 ? "\033[32m" : avg < 16 ? "\033[33m" : "\033[31m";
    std::fprintf(stdout,
                 "\r\033[1;36m[PERF]\033[0m  loop: %s%6.1f\033[0mms avg %5.1f p50  %5.1f p95  \033[1m%5.1f\033[0m p99  "
                 "%5.1f max  │  jitter: %5.1fms avg %5.1f p99  │  CPU: %4.0f%%  MEM: %5.0fMB  thr: %d",
                 color, avg, p50, p95, p99, mx, jAvg, jP99, m_cpu, m_mem, m_threads);
    if (!slow.isEmpty() && m_reportCount % 5 == 0) {
        std::fprintf(stdout, "\n\033[1;33m[PERF]\033[0m  Slow slots:\n");
        for (const QString &s : std::as_const(slow))
            std::fprintf(stdout, "\033[33m%s\033[0m\n", qPrintable(s));
    }
    std::fflush(stdout);
    ++m_reportCount;

    if (m_log.isOpen()) {
        const char *tag = avg < 5 ? "OK" : avg < 16 ? "WARN" : "STUTTER";
        QTextStream out(&m_log);
        out << QString::asprintf("[%s] [%-7s]  loop: %6.1fms avg  %5.1f p50  %5.1f p95  %5.1f p99  %5.1f max  |  "
                                 "GIL: %5.1fms avg  %5.1f p99  |  CPU: %4.0f%%  MEM: %5.0fMB  thr: %d\n",
                                 qPrintable(QTime::currentTime().toString("HH:mm:ss")), tag, avg, p50, p95, p99, mx,
                                 jAvg, jP99, m_cpu, m_mem, m_threads);
        for (const QString &s : std::as_const(slow))
            out << "  SLOW " << s.trimmed() << "\n";
        out.flush();
    }
    if (m_csv.isOpen()) {
        QTextStream out(&m_csv);
        out << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
            << QString::asprintf(",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.1f,%d\n", avg, p50, p95, p99, mx, jAvg,
                                 jP99, m_cpu, m_mem, m_threads);
        out.flush();
    }

    m_loopBuf.clear();
    emit metricsUpdated();
}

QVariantMap PerfMonitor::metrics() const
{
    QMutexLocker l(&s_metricsLock);
    return {
        {QStringLiteral("loop_avg_ms"), roundTo(m_loopAvg, 2)},
        {QStringLiteral("loop_p99_ms"), roundTo(m_loopP99, 2)},
        {QStringLiteral("gil_avg_ms"), roundTo(m_jitterAvg, 2)},
        {QStringLiteral("cpu_percent"), roundTo(m_cpu, 1)},
        {QStringLiteral("mem_mb"), roundTo(m_mem, 1)},
        {QStringLiteral("thread_count"), m_threads},
        {QStringLiteral("summary"), m_summary},
        {QStringLiteral("backend"), QStringLiteral("cpp")},
    };
}

QVariantMap PerfMonitor::slotTimings()
{
    QMutexLocker l(&s_slotLock);
    QVariantMap out;
    for (auto it = slotBuffers().constBegin(); it != slotBuffers().constEnd(); ++it) {
        out.insert(it.key(), QVariantMap{
            {QStringLiteral("avg_ms"), roundTo(it->mean(), 2)},
            {QStringLiteral("p95_ms"), roundTo(it->percentile(95), 2)},
            {QStringLiteral("max_ms"), roundTo(it->max(), 2)},
            {QStringLiteral("count"), it->count()},
        });
    }
    return out;
}

QString PerfMonitor::getDetailedReport() const
{
    QStringList lines{QStringLiteral("=== OCTAVE Performance Report ==="), QString()};
    {
        QMutexLocker l(&s_metricsLock);
        lines << QString::asprintf("Event Loop:  avg=%.1fms  p99=%.1fms", m_loopAvg, m_loopP99)
              << QString::asprintf("Thread Jitter:  avg=%.1fms", m_jitterAvg)
              << QString::asprintf("CPU: %.0f%%   MEM: %.0fMB   Threads: %d", m_cpu, m_mem, m_threads) << QString();
    }
    QMutexLocker l(&s_slotLock);
    if (slotBuffers().isEmpty()) {
        lines << QStringLiteral("No slot timings recorded. Use OCTAVE_PERF_SCOPE (src/util/perfscope.h).");
        return lines.join(QLatin1Char('\n'));
    }
    QList<QPair<QString, const RingBuffer *>> sorted;
    for (auto it = slotBuffers().constBegin(); it != slotBuffers().constEnd(); ++it)
        sorted.append({it.key(), &it.value()});
    std::sort(sorted.begin(), sorted.end(),
              [](const auto &a, const auto &b) { return a.second->percentile(95) > b.second->percentile(95); });
    lines << QStringLiteral("Slot Timings (p95 > 1ms):");
    for (const auto &[name, buf] : std::as_const(sorted)) {
        const double p95 = buf->percentile(95);
        if (p95 > 1.0)
            lines << QString::asprintf("  %-40s  avg=%6.1fms  p95=%6.1fms  max=%6.1fms  n=%d", qPrintable(name),
                                       buf->mean(), p95, buf->max(), buf->count());
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace OctaveDev
