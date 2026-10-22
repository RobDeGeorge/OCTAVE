#ifndef SHIFTLIGHTMANAGER_H
#define SHIFTLIGHTMANAGER_H

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class SettingsManager;

/**
 * @brief The RPM shift light: which flag the current RPM falls in, and whether
 *        the light is lit right now.
 *
 * A flag is an RPM band (rpmLow..rpmHigh, inclusive) with a colour and effects
 * (flash, full-screen flash, aura). Flags are stored in the "rpm_flags" setting
 * as a JSON string, ordered by rpmLow; when bands overlap the last match wins.
 * A legacy flag ({rpm} with no band) is migrated once on load to
 * rpmLow = rpm, rpmHigh = rpm_max_rpm, and written back.
 *
 * Fed by OBDManager::rpmChanged (wired in main.cpp). The flash timer runs here,
 * so every frontend sees the same on/off phase: a UI only binds to `active`,
 * `activeFlag` and `lit`. All "rpm_*" settings are re-read whenever one changes.
 *
 * Registered in main.cpp as QML context property "shiftLightManager".
 */
class ShiftLightManager : public QObject
{
    Q_OBJECT

    // Settings (read-only here; the editor saves them through SettingsManager)
    Q_PROPERTY(bool enabled READ enabled NOTIFY settingsChanged)
    Q_PROPERTY(bool showOnHomeCard READ showOnHomeCard NOTIFY settingsChanged)
    Q_PROPERTY(int maxRpm READ maxRpm NOTIFY settingsChanged)
    Q_PROPERTY(double fullScreenFlashOpacity READ fullScreenFlashOpacity NOTIFY settingsChanged)
    Q_PROPERTY(double lampSize READ lampSize NOTIFY settingsChanged)
    Q_PROPERTY(double glowSize READ glowSize NOTIFY settingsChanged)
    Q_PROPERTY(double glowIntensity READ glowIntensity NOTIFY settingsChanged)
    Q_PROPERTY(int colorTransitionSpeed READ colorTransitionSpeed NOTIFY settingsChanged)
    Q_PROPERTY(bool pulseEnabled READ pulseEnabled NOTIFY settingsChanged)

    // The flags as stored (migrated), for the editor
    Q_PROPERTY(QVariantList flags READ flags NOTIFY flagsChanged)

    // Live state
    Q_PROPERTY(double rpm READ rpm NOTIFY rpmChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeFlagChanged)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY activeFlagChanged)
    Q_PROPERTY(QVariantMap activeFlag READ activeFlag NOTIFY activeFlagChanged)
    Q_PROPERTY(bool lit READ lit NOTIFY litChanged)

    // In-memory override (never saved): forces the light dark, e.g. for the
    // dev screenshot tooling, without touching the user's setting
    Q_PROPERTY(bool suppressed READ suppressed WRITE setSuppressed NOTIFY suppressedChanged)

public:
    explicit ShiftLightManager(SettingsManager *settingsManager, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    bool showOnHomeCard() const { return m_showOnHomeCard; }
    int maxRpm() const { return m_maxRpm; }
    double fullScreenFlashOpacity() const { return m_fullScreenFlashOpacity; }
    double lampSize() const { return m_lampSize; }
    double glowSize() const { return m_glowSize; }
    double glowIntensity() const { return m_glowIntensity; }
    int colorTransitionSpeed() const { return m_colorTransitionSpeed; }
    bool pulseEnabled() const { return m_pulseEnabled; }

    QVariantList flags() const { return m_flags; }

    double rpm() const { return m_rpm; }
    bool active() const { return m_activeIndex >= 0; }
    int activeIndex() const { return m_activeIndex; }
    QVariantMap activeFlag() const { return m_activeFlag; }
    bool lit() const { return m_lit; }

    bool suppressed() const { return m_suppressed; }
    void setSuppressed(bool suppressed);

    /**
     * @brief Bring stored flags to the band format.
     *
     * Legacy {rpm} flags get rpmLow = rpm and rpmHigh = maxRpm; a band with no
     * rpmHigh gets maxRpm; entries with no usable low bound are dropped.
     * @param changed  Set to true if anything was rewritten.
     */
    static QVariantList migrateFlags(const QVariantList &flags, int maxRpm, bool *changed = nullptr);

    /// Index of the flag whose band contains rpm (last match wins), or -1.
    static int matchFlag(const QVariantList &flags, double rpm);

    /// A flag with every field filled in (defaults; fullScreenFlashOpacity
    /// falls back to the global opacity).
    static QVariantMap resolveFlag(const QVariantMap &flag, double globalFullScreenOpacity);

signals:
    void settingsChanged();
    void flagsChanged();
    void rpmChanged();
    void activeFlagChanged();
    void litChanged();
    void suppressedChanged();

public slots:
    void setRpm(double rpm);

    /// Replace the flags and save them (the editor's only write path)
    void setFlags(const QVariantList &flags);

private:
    struct Band { double low; double high; };

    void reload();
    void loadFlags();
    void evaluate();
    void setLit(bool lit);
    void onFlashTick();

    SettingsManager *m_settings = nullptr;

    bool m_enabled = true;
    bool m_showOnHomeCard = true;
    int m_maxRpm = 8000;
    double m_fullScreenFlashOpacity = 0.5;
    double m_lampSize = 0.5;
    double m_glowSize = 0.6;
    double m_glowIntensity = 0.6;
    int m_colorTransitionSpeed = 100;
    bool m_pulseEnabled = false;

    QVariantList m_flags;
    QVector<Band> m_bands;   // m_flags' bands, for the per-sample match

    double m_rpm = 0.0;
    int m_activeIndex = -1;
    QVariantMap m_activeFlag;
    bool m_lit = false;
    bool m_suppressed = false;
    bool m_saving = false;

    QTimer m_flashTimer;
};

#endif // SHIFTLIGHTMANAGER_H
