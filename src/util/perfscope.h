#ifndef PERFSCOPE_H
#define PERFSCOPE_H

// OCTAVE_PERF_SCOPE("media.next_track") at the top of a function records how
// long the rest of the scope took, under that label, for the --profile perf
// monitor (src/dev/perfmonitor.cpp; read back by the get_slot_timings
// command).
//
// Builds without OCTAVE_DEV_TOOLS (every Release build) compile it to
// nothing. With dev tools built but no --profile, it costs one relaxed
// atomic load. Safe from any thread.

#ifdef OCTAVE_DEV_TOOLS

#include <QElapsedTimer>
#include <atomic>

namespace OctavePerf {
extern std::atomic<bool> g_enabled;
void record(const char *label, double ms);

class Scope
{
public:
    explicit Scope(const char *label) : m_label(label)
    {
        if (g_enabled.load(std::memory_order_relaxed))
            m_timer.start();
    }
    ~Scope()
    {
        if (m_timer.isValid())
            record(m_label, m_timer.nsecsElapsed() / 1.0e6);
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    const char *m_label;
    QElapsedTimer m_timer;
};
} // namespace OctavePerf

#define OCTAVE_PERF_CONCAT_(a, b) a##b
#define OCTAVE_PERF_CONCAT(a, b) OCTAVE_PERF_CONCAT_(a, b)
#define OCTAVE_PERF_SCOPE(label) \
    const OctavePerf::Scope OCTAVE_PERF_CONCAT(octavePerfScope_, __LINE__)(label)

#else

#define OCTAVE_PERF_SCOPE(label) static_cast<void>(0)

#endif

#endif // PERFSCOPE_H
