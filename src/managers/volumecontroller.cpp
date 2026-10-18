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
    // Clamp to [0, 100]
    percent = std::clamp(percent, 0, 100);

    const float linear = toLinearStatic(static_cast<float>(percent));

    // The live percent other code reads back (in memory, not saved to disk)
    if (m_settings) {
        m_settings->setCurrentVolume(percent);
    }

    // Notify all connected managers (MediaManager, PhoneMirrorManager, ESP32)
    // The wiring is done in main.cpp via connect().
    emit volumeApplied(percent, linear);
}

float VolumeController::toLinear(float percent)
{
    return toLinearStatic(percent);
}
