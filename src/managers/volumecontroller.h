#ifndef VOLUMECONTROLLER_H
#define VOLUMECONTROLLER_H

#include <QElapsedTimer>
#include <QObject>

#include <algorithm>
#include <cmath>

class SettingsManager;

/**
 * @brief Single entry point for applying a UI volume percent to every output.
 *
 * OCTAVE maps the UI volume percent (0-100) to a linear audio volume (0.0-1.0)
 * through a quadratic curve (y = x^2). This gives a more natural perceptual
 * response than linear at low volumes. Every audio output in the app must use
 * this same curve or they will drift out of sync when the user moves the slider.
 *
 * Registered in main.cpp as QML context property "volumeController" so both
 * C++ hardware handlers (gesture sensor, ESP32 knob) and QML slider widgets
 * can funnel volume changes through one method.
 *
 * It also owns mute (in memory, never saved). The rules every input shares:
 * raising the volume while muted unmutes; the mute toggle at 0 % restores the
 * startup volume instead. MediaManager and PhoneMirrorManager follow
 * mutedChanged the way they follow volumeApplied. UIs bind to `percent`,
 * `muted` and `effectivelyMuted` and never do curve math themselves.
 *
 * The volumeApplied signal is emitted after every applyVolume() call with both
 * the raw percent and the computed linear value.  Other managers (MediaManager,
 * PhoneMirrorManager, ESP32VolumeManager) should connect to
 * this signal to update their own audio outputs.  This avoids tight coupling —
 * VolumeController doesn't need pointers to managers that may not exist yet.
 */
class VolumeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int percent READ percent NOTIFY percentChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    // muted, or the volume is 0 %: what the mute icons show
    Q_PROPERTY(bool effectivelyMuted READ effectivelyMuted NOTIFY effectivelyMutedChanged)

public:
    /**
     * @brief Construct a VolumeController.
     * @param settingsManager  Pointer to the app-wide SettingsManager (required).
     * @param parent           Optional QObject parent for ownership.
     */
    explicit VolumeController(SettingsManager *settingsManager,
                              QObject *parent = nullptr);

    // -- Free-function equivalent for use without a VolumeController instance --

    /**
     * @brief Convert a 0-100 UI volume percent to a 0.0-1.0 linear volume.
     *
     * Clamps out-of-range inputs.  Static so it can be used from anywhere
     * without a VolumeController instance (e.g. unit tests, one-off math).
     */
    static float toLinearStatic(float percent);

    /**
     * @brief Inverse of the curve: a 0.0-1.0 linear volume to a 0-100 percent.
     *
     * Inline so SettingsManager can use it (startup volume) without linking
     * this class. Clamps out-of-range inputs.
     */
    static int toPercentStatic(float linear)
    {
        const double l = std::clamp(static_cast<double>(linear), 0.0, 1.0);
        return static_cast<int>(std::lround(std::sqrt(l) * 100.0));
    }

    int percent() const { return m_percent; }
    bool muted() const { return m_muted; }
    bool effectivelyMuted() const { return m_muted || m_percent == 0; }

signals:
    /**
     * @brief Emitted after every applyVolume() call.
     * @param percent  Clamped 0-100 integer volume.
     * @param linear   Computed linear value (0.0-1.0) from the quadratic curve.
     *
     * Connect other managers to this signal in main.cpp:
     * @code
     *   connect(volumeController, &VolumeController::volumeApplied,
     *           mediaManager,     &MediaManager::setVolume);
     * @endcode
     */
    void volumeApplied(int percent, float linear);

    void percentChanged(int percent);
    void mutedChanged(bool muted);
    void effectivelyMutedChanged();

public slots:
    /**
     * @brief Apply a volume percent (0-100) to every connected audio output.
     *
     * - Stores the clamped percent as SettingsManager::currentVolume (in
     *   memory only; a restart starts at the separate start-up volume).
     * - Computes the quadratic-curve linear value.
     * - Emits volumeApplied(percent, linear) so connected managers can react.
     * - Unmutes if the percent went up.
     */
    void applyVolume(int percent);

    /// Mute or unmute without touching the level.
    void setMuted(bool muted);

    /**
     * @brief The mute button (screen, ESP32 knob, gesture).
     *
     * At 0 % it restores the startup volume (and unmutes) instead of
     * toggling. Presses within 150 ms of the last are ignored: the knob's
     * button bounces.
     */
    void toggleMute();

    /**
     * @brief Pure math helper for QML — exposes the curve without dispatching.
     * @param percent  Input value (0-100).  Clamped internally.
     * @return Linear volume (0.0-1.0).
     */
    float toLinear(float percent);

    /// Pure math helper for QML: linear 0.0-1.0 to percent 0-100.
    int toPercent(float linear);

private:
    void setPercent(int percent);

    SettingsManager *m_settings = nullptr;
    int m_percent = 0;
    bool m_muted = false;
    int m_notifyingPercent = 0;   // depth inside percentChanged (see applyVolume)
    QElapsedTimer m_lastToggle;
};

#endif // VOLUMECONTROLLER_H
