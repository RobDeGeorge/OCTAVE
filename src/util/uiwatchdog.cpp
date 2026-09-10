#include "uiwatchdog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLoggingCategory>
#include <QMetaObject>

Q_LOGGING_CATEGORY(lcWatchdog, "octave.ui_watchdog")

UiWatchdog::UiWatchdog(QObject *parent, int pingMs, int stallMs)
    : QObject(parent), m_pingMs(pingMs), m_stallMs(stallMs)
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
    m_lastPong = QDateTime::currentMSecsSinceEpoch();
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
    qint64 stallStart = 0;
    while (m_running.load()) {
        QThread::msleep(static_cast<unsigned long>(m_pingMs));
        if (!m_running.load())
            break;
        // Ping: runs on the GUI thread when the event loop gets to it
        QMetaObject::invokeMethod(this, [this] { m_lastPong = QDateTime::currentMSecsSinceEpoch(); },
                                  Qt::QueuedConnection);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 silent = now - m_lastPong.load();
        if (!stalled && silent > m_stallMs) {
            stalled = true;
            stallStart = m_lastPong.load();
            qCWarning(lcWatchdog) << "UI thread blocked for" << silent / 1000.0 << "s and counting";
        } else if (stalled && silent <= m_pingMs) {
            stalled = false;
            qCWarning(lcWatchdog) << "UI thread responsive again after"
                                  << (m_lastPong.load() - stallStart) / 1000.0 << "s";
        }
    }
}
