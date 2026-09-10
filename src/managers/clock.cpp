#include "clock.h"
#include "settingsmanager.h"

#include <QDateTime>
#include <QTime>

Clock::Clock(SettingsManager *settingsManager, QObject *parent)
    : QObject(parent)
    , m_settingsManager(settingsManager)
{
    connect(&m_timer, &QTimer::timeout, this, &Clock::update_time);
    m_timer.start(1000); // Update every second
}

void Clock::update_time()
{
    QString text;
    if (m_settingsManager->showClock()) {
        const QTime currentTime = QTime::currentTime();
        const bool showSeconds = m_settingsManager->clockShowSeconds();
        if (m_settingsManager->clockFormat24Hour()) {
            text = currentTime.toString(showSeconds ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"));
        } else {
            int hour12 = currentTime.hour() % 12;
            if (hour12 == 0)
                hour12 = 12;
            text = QStringLiteral("%1:%2")
                       .arg(hour12, 2, 10, QLatin1Char('0'))
                       .arg(currentTime.minute(), 2, 10, QLatin1Char('0'));
            if (showSeconds)
                text += QStringLiteral(":%1").arg(currentTime.second(), 2, 10, QLatin1Char('0'));
            text += (currentTime.hour() >= 12) ? QStringLiteral(" PM") : QStringLiteral(" AM");
        }
    }
    // Without seconds the text changes once a minute; re-evaluating every
    // binding that shows the clock 60 times for the same string is waste.
    if (text == m_lastText)
        return;
    m_lastText = text;
    emit timeChanged(text);
}
