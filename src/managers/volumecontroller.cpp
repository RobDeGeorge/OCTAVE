#include "volumecontroller.h"
#include "settingsmanager.h"

#include <QtMath>
#include <algorithm>

// ============================================================================
// Construction
// ============================================================================

VolumeController::VolumeController(SettingsManager *settingsManager,
                                   QObject *parent)
    : QObject(parent)
    , m_settings(settingsManager)
{
    if (m_settings)
        m_percent = m_settings->currentVolume();
}

// ============================================================================
// Static curve math
// ============================================================================

float VolumeController::toLinearStatic(float percent)
{
    // Clamp to [0, 100]
    const float pct = std::clamp(percent, 0.0f, 100.0f);
    // Quadratic curve: (pct / 100)^2.0
    const float normalized = pct / 100.0f;
    return normalized * normalized;
}

// ============================================================================
// Slots
// ============================================================================

void VolumeController::applyVolume(int percent)
{
    // A raise is judged on the request, so "up" at 100 % still counts
    const bool raised = percent > m_percent;

    // Clamp to [0, 100]
    percent = std::clamp(percent, 0, 100);

    // A UI echoing percentChanged back (the QML sliders set their value in
    // that handler) re-enters here with the same percent: the outer call
    // already tells the outputs
    if (m_notifyingPercent > 0 && percent == m_percent)
        return;

    setPercent(percent);
    // A handler of percentChanged may have moved it again: report where it is
    percent = m_percent;
    const float linear = toLinearStatic(static_cast<float>(percent));

    // Notify all connected managers (MediaManager, PhoneMirrorManager, ESP32)
    // The wiring is done in main.cpp via connect().
    emit volumeApplied(percent, linear);

    // Turning the volume up while muted brings the sound back, whichever
    // input did it
    if (raised && m_muted)
        setMuted(false);
}

void VolumeController::setPercent(int percent)
{
    // The live percent other code reads back (in memory, not saved to disk)
    if (m_settings)
        m_settings->setCurrentVolume(percent);
    if (percent == m_percent)
        return;
    const bool wasEffectivelyMuted = effectivelyMuted();
    m_percent = percent;
    ++m_notifyingPercent;
    emit percentChanged(percent);
    --m_notifyingPercent;
    if (effectivelyMuted() != wasEffectivelyMuted)
        emit effectivelyMutedChanged();
}

void VolumeController::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    const bool wasEffectivelyMuted = effectivelyMuted();
    m_muted = muted;
    emit mutedChanged(muted);
    if (effectivelyMuted() != wasEffectivelyMuted)
        emit effectivelyMutedChanged();
}

void VolumeController::toggleMute()
{
    if (m_lastToggle.isValid() && m_lastToggle.elapsed() < 150)
        return;
    m_lastToggle.start();

    if (m_percent == 0) {
        // Nothing to unmute to: come back at the startup volume
        const int restore = m_settings ? toPercentStatic(m_settings->startUpVolume()) : 0;
        if (restore > 0)
            applyVolume(restore);
        setMuted(false);
        return;
    }
    setMuted(!m_muted);
}

float VolumeController::toLinear(float percent)
{
    return toLinearStatic(percent);
}

int VolumeController::toPercent(float linear)
{
    return toPercentStatic(linear);
}
