#ifndef VOLUMECONTROLLER_H
#define VOLUMECONTROLLER_H

#include <QObject>

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
 * The volumeApplied signal is emitted after every applyVolume() call with both
 * the raw percent and the computed linear value.  Other managers (MediaManager,
 * PhoneMirrorManager, ESP32VolumeManager) should connect to
 * this signal to update their own audio outputs.  This avoids tight coupling —
 * VolumeController doesn't need pointers to managers that may not exist yet.
 */
class VolumeController : public QObject
{
    Q_OBJECT

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

public slots:
    /**
     * @brief Apply a volume percent (0-100) to every connected audio output.
     *
     * - Stores the clamped percent as SettingsManager::currentVolume (in
     *   memory only; a restart starts at the separate start-up volume).
     * - Computes the quadratic-curve linear value.
     * - Emits volumeApplied(percent, linear) so connected managers can react.
     */
    void applyVolume(int percent);

    /**
     * @brief Pure math helper for QML — exposes the curve without dispatching.
     * @param percent  Input value (0-100).  Clamped internally.
     * @return Linear volume (0.0-1.0).
     */
    float toLinear(float percent);

private:
    SettingsManager *m_settings = nullptr;
};

#endif // VOLUMECONTROLLER_H
