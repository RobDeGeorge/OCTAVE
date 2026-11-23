// threadidle.h
//
// sleepWhenIdle(thread): call it on every QThread that runs an event loop
// (QThread::exec(), a worker object moved to it), before or after start().
//
// It does nothing outside the browser build. There it works around a Qt
// bug, in every release from 6.10 on (6.12 included): the WebAssembly event
// dispatcher of a thread other than the main one waits for the next event
// with
//
//     m_moreEvents.wait_for(lock, std::chrono::milliseconds::max(), ...)
//
// when the thread has no running timer (QEventDispatcherWasm::
// secondaryThreadWait(), qeventdispatcher_wasm.cpp). libc++ adds that to
// steady_clock::now(), which overflows, so the wait ends at once and the
// event loop spins: one whole CPU core per idle thread, for as long as the
// page is open. That was the black box's trip-store thread on every visit
// (found 2026-11-23, 99.7 % of a core on the home screen).
//
// With a timer running on the thread the dispatcher waits for a real
// number of milliseconds instead, so the thread gets one that fires once an
// hour and does nothing.

#ifndef OCTAVE_UTIL_THREADIDLE_H
#define OCTAVE_UTIL_THREADIDLE_H

#include <QThread>

#ifdef OCTAVE_WASM
#include <QMetaObject>
#include <QTimer>

#include <chrono>
#endif

inline void sleepWhenIdle(QThread *thread)
{
#ifdef OCTAVE_WASM
    if (!thread)
        return;
    auto *timer = new QTimer;   // no parent: it moves to the thread and ends with it
    timer->setInterval(std::chrono::hours(1));
    timer->moveToThread(thread);
    QObject::connect(thread, &QThread::finished, timer, &QObject::deleteLater);
    // Queued to the thread itself: a timer starts on the thread it lives on,
    // and this runs as soon as that thread's event loop does
    QMetaObject::invokeMethod(timer, [timer] { timer->start(); }, Qt::QueuedConnection);
#else
    Q_UNUSED(thread);
#endif
}

#endif // OCTAVE_UTIL_THREADIDLE_H
