#include "shiftlightmanager.h"
#include "settingsmanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>

#include <algorithm>

Q_LOGGING_CATEGORY(lcShiftLight, "octave.shiftlight")

namespace {

constexpr int kMinFlashIntervalMs = 20;

double num(const QVariantMap &m, const char *key, double fallback)
{
    const QVariant v = m.value(QLatin1String(key));
    bool ok = false;
    const double d = v.toDouble(&ok);
    return v.isValid() && ok ? d : fallback;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

ShiftLightManager::ShiftLightManager(SettingsManager *settingsManager, QObject *parent)
    : QObject(parent)
    , m_settings(settingsManager)
{
    m_flashTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_flashTimer, &QTimer::timeout, this, &ShiftLightManager::onFlashTick);

    if (m_settings) {
        connect(m_settings, &SettingsManager::genericSettingChanged, this,
                [this](const QString &key) {
                    if (!m_saving && key.startsWith(QLatin1String("rpm_")))
                        reload();
                });
    }
    reload();
}

// ============================================================================
// Pure rules (static, tested directly)
// ============================================================================

QVariantList ShiftLightManager::migrateFlags(const QVariantList &flags, int maxRpm, bool *changed)
{
    bool dirty = false;
    QVariantList out;
    out.reserve(flags.size());
    for (const QVariant &v : flags) {
        QVariantMap flag = v.toMap();
        if (!flag.contains(QStringLiteral("rpmLow"))) {
            if (!flag.contains(QStringLiteral("rpm"))) {
                dirty = true;   // no usable band: drop it
                continue;
            }
            // Legacy single-threshold flag: lit from rpm up to the redline
            flag.insert(QStringLiteral("rpmLow"), flag.value(QStringLiteral("rpm")));
            flag.insert(QStringLiteral("rpmHigh"), maxRpm);
            dirty = true;
        }
        if (flag.remove(QStringLiteral("rpm")) > 0)
            dirty = true;
        if (!flag.contains(QStringLiteral("rpmHigh"))) {
            flag.insert(QStringLiteral("rpmHigh"), maxRpm);
            dirty = true;
        }
        out.append(flag);
    }
    if (changed)
        *changed = dirty;
    return out;
}

int ShiftLightManager::matchFlag(const QVariantList &flags, double rpm)
{
    // Highest priority last: the first match walking backwards wins
    for (int i = int(flags.size()) - 1; i >= 0; --i) {
        const QVariantMap flag = flags.at(i).toMap();
        const double low = num(flag, "rpmLow", 0.0);
        const double high = num(flag, "rpmHigh", 0.0);
        if (rpm >= low && rpm <= high)
            return i;
    }
    return -1;
}

QVariantMap ShiftLightManager::resolveFlag(const QVariantMap &flag, double globalFullScreenOpacity)
{
    QVariantMap r = flag;
    auto fill = [&r](const char *key, const QVariant &value) {
        if (!r.contains(QLatin1String(key)))
            r.insert(QLatin1String(key), value);
    };
    fill("color", QStringLiteral("#FF0000"));
    fill("flash", false);
    fill("flashSpeed", 100);
    fill("fullScreenFlash", false);
    fill("fullScreenFlashOpacity", globalFullScreenOpacity);
    fill("auraFlash", false);
    fill("auraFlashOpacity", 0.6);
    fill("auraSpread", 0.18);
    return r;
}

// ============================================================================
// Settings
// ============================================================================

void ShiftLightManager::reload()
{
    if (m_settings) {
        auto get = [this](const char *key, const QVariant &def) {
            return m_settings->get_setting_with_default(QLatin1String(key), def);
        };
        m_enabled = get("rpm_shift_light_enabled", true).toBool();
        m_showOnHomeCard = get("rpm_show_on_home_card", true).toBool();
        m_maxRpm = get("rpm_max_rpm", 8000).toInt();
        m_fullScreenFlashOpacity = get("rpm_fullscreen_flash_opacity", 0.5).toDouble();
        m_lampSize = get("rpm_shift_light_size", 0.5).toDouble();
        m_glowSize = get("rpm_glow_size", 0.6).toDouble();
        m_glowIntensity = get("rpm_glow_intensity", 0.6).toDouble();
        m_colorTransitionSpeed = get("rpm_color_transition_speed", 100).toInt();
        m_pulseEnabled = get("rpm_pulse_enabled", false).toBool();
    }
    emit settingsChanged();
    loadFlags();
    // Re-resolve the active flag even if the band index is unchanged: its
    // fields (or the global opacity it falls back to) may have been edited
    m_activeIndex = -2;
    evaluate();
}

void ShiftLightManager::loadFlags()
{
    QVariantList raw;
    if (m_settings) {
        const QVariant stored = m_settings->get_setting_with_default(
            QStringLiteral("rpm_flags"), QStringLiteral("[]"));
        if (stored.typeId() == QMetaType::QVariantList) {
            raw = stored.toList();
        } else {
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(stored.toString().toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isArray())
                raw = doc.array().toVariantList();
            else
                qCWarning(lcShiftLight) << "rpm_flags is not a JSON array, ignoring:" << err.errorString();
        }
    }

    bool migrated = false;
    const QVariantList flags = migrateFlags(raw, m_maxRpm, &migrated);
    if (migrated && m_settings) {
        qCInfo(lcShiftLight) << "migrated" << raw.size() << "stored flags to the band format";
        m_saving = true;
        m_settings->save_setting(QStringLiteral("rpm_flags"),
                                 QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(flags))
                                                       .toJson(QJsonDocument::Compact)));
        m_saving = false;
    }

    m_bands.clear();
    m_bands.reserve(flags.size());
    for (const QVariant &v : flags) {
        const QVariantMap f = v.toMap();
        m_bands.append({num(f, "rpmLow", 0.0), num(f, "rpmHigh", 0.0)});
    }
    if (flags != m_flags) {
        m_flags = flags;
        emit flagsChanged();
    }
}

void ShiftLightManager::setFlags(const QVariantList &flags)
{
    if (!m_settings)
        return;
    // save_setting -> genericSettingChanged("rpm_flags") -> reload()
    m_settings->save_setting(QStringLiteral("rpm_flags"),
                             QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(flags))
                                                   .toJson(QJsonDocument::Compact)));
}

// ============================================================================
// Live state
// ============================================================================

void ShiftLightManager::setRpm(double rpm)
{
    if (rpm == m_rpm)
        return;
    m_rpm = rpm;
    emit rpmChanged();
    evaluate();
}

void ShiftLightManager::setSuppressed(bool suppressed)
{
    if (suppressed == m_suppressed)
        return;
    m_suppressed = suppressed;
    emit suppressedChanged();
    evaluate();
}

void ShiftLightManager::evaluate()
{
    int index = -1;
    if (m_enabled && !m_suppressed) {
        for (int i = int(m_bands.size()) - 1; i >= 0; --i) {
            if (m_rpm >= m_bands[i].low && m_rpm <= m_bands[i].high) {
                index = i;
                break;
            }
        }
    }
    // RPM arrives many times a second: only a real change goes further
    if (index == m_activeIndex)
        return;

    m_activeIndex = index;
    m_activeFlag = index >= 0 ? resolveFlag(m_flags.at(index).toMap(), m_fullScreenFlashOpacity)
                              : QVariantMap();
    emit activeFlagChanged();

    // A new flag starts lit; a flashing one then toggles at its own speed
    if (index >= 0 && m_activeFlag.value(QStringLiteral("flash")).toBool()) {
        m_flashTimer.start(std::max(kMinFlashIntervalMs,
                                    m_activeFlag.value(QStringLiteral("flashSpeed")).toInt()));
    } else {
        m_flashTimer.stop();
    }
    setLit(index >= 0);
}

void ShiftLightManager::onFlashTick()
{
    setLit(!m_lit);
}

void ShiftLightManager::setLit(bool lit)
{
    if (lit == m_lit)
        return;
    m_lit = lit;
    emit litChanged();
}
