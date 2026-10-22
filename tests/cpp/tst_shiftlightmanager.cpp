// ShiftLightManager owns the RPM shift light rules that used to live in three
// QML copies: legacy flag migration, the band match, and the flash phase.
// Pins the decision the copies disagreed on: a legacy {rpm} flag's high bound
// is the redline (rpm_max_rpm), not "no limit".

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "managers/settingsmanager.h"
#include "managers/shiftlightmanager.h"

namespace {

QVariantMap band(double low, double high, const QString &color, bool flash = false, int speed = 100)
{
    return {{"rpmLow", low}, {"rpmHigh", high}, {"color", color},
            {"flash", flash}, {"flashSpeed", speed}};
}

QString json(const QVariantList &flags)
{
    return QString::fromUtf8(QJsonDocument::fromVariant(flags).toJson(QJsonDocument::Compact));
}

} // namespace

class TstShiftLightManager : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        qputenv("HOME", m_tmp->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp->path() + "/config").toUtf8());
        qputenv("APPDATA", (m_tmp->path() + "/appdata").toUtf8());
    }

    // ── migration ──────────────────────────────────────────────────────

    void legacyFlagHighBoundIsRedline()
    {
        bool changed = false;
        const QVariantList out = ShiftLightManager::migrateFlags(
            {QVariantMap{{"rpm", 6000}, {"color", "#FF0000"}}}, 7000, &changed);
        QVERIFY(changed);
        QCOMPARE(out.size(), 1);
        const QVariantMap f = out.first().toMap();
        QCOMPARE(f.value("rpmLow").toInt(), 6000);
        QCOMPARE(f.value("rpmHigh").toInt(), 7000);
        QVERIFY(!f.contains("rpm"));
        QCOMPARE(f.value("color").toString(), QStringLiteral("#FF0000"));
    }

    void bandFlagsAreLeftAlone()
    {
        bool changed = true;
        const QVariantList in{band(3000, 5000, "#00FF00")};
        QCOMPARE(ShiftLightManager::migrateFlags(in, 8000, &changed), in);
        QVERIFY(!changed);
    }

    void missingHighAndUnusableEntriesAreFixed()
    {
        bool changed = false;
        const QVariantList out = ShiftLightManager::migrateFlags(
            {QVariantMap{{"rpmLow", 4000}}, QVariantMap{{"color", "#FFF"}}, QVariant(42)}, 8000, &changed);
        QVERIFY(changed);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.first().toMap().value("rpmHigh").toInt(), 8000);
    }

    void migrationIsWrittenBackOnce()
    {
        SettingsManager settings;
        settings.save_setting("rpm_max_rpm", 6500);
        settings.save_setting("rpm_flags", json({QVariantMap{{"rpm", 5000}, {"color", "#FF0000"}}}));

        ShiftLightManager light(&settings);
        QCOMPARE(light.flags().size(), 1);
        QCOMPARE(light.flags().first().toMap().value("rpmHigh").toInt(), 6500);

        // Stored in the band format now, still as a JSON string
        const QVariantList stored = QJsonDocument::fromJson(
            settings.get_setting("rpm_flags").toString().toUtf8()).toVariant().toList();
        QCOMPARE(stored, light.flags());
    }

    void corruptFlagsMeanNoFlags()
    {
        SettingsManager settings;
        settings.save_setting("rpm_flags", "{not json");
        ShiftLightManager light(&settings);
        QVERIFY(light.flags().isEmpty());
        light.setRpm(7000);
        QVERIFY(!light.active());
    }

    // ── band match ─────────────────────────────────────────────────────

    void matchFlag_data()
    {
        QTest::addColumn<double>("rpm");
        QTest::addColumn<int>("index");
        QTest::newRow("below every band") << 1000.0 << -1;
        QTest::newRow("low bound inclusive") << 3000.0 << 0;
        QTest::newRow("inside first") << 4000.0 << 0;
        QTest::newRow("overlap: last wins") << 5000.0 << 1;
        QTest::newRow("high bound inclusive") << 6500.0 << 1;
        QTest::newRow("gap") << 6600.0 << -1;
        QTest::newRow("top band") << 7000.0 << 2;
        QTest::newRow("above every band") << 9000.0 << -1;
    }

    void matchFlag()
    {
        QFETCH(double, rpm);
        QFETCH(int, index);
        const QVariantList flags{band(3000, 5000, "#0F0"), band(5000, 6500, "#FF0"),
                                 band(6800, 8000, "#F00")};
        QCOMPARE(ShiftLightManager::matchFlag(flags, rpm), index);

        // The live path agrees with the pure rule
        SettingsManager settings;
        settings.save_setting("rpm_flags", json(flags));
        ShiftLightManager light(&settings);
        light.setRpm(rpm);
        QCOMPARE(light.activeIndex(), index);
        QCOMPARE(light.active(), index >= 0);
        QCOMPARE(light.lit(), index >= 0);
    }

    void activeFlagIsResolved()
    {
        SettingsManager settings;
        settings.save_setting("rpm_fullscreen_flash_opacity", 0.3);
        settings.save_setting("rpm_flags", json({QVariantMap{{"rpmLow", 4000}, {"rpmHigh", 8000}}}));
        ShiftLightManager light(&settings);
        light.setRpm(5000);

        const QVariantMap f = light.activeFlag();
        QCOMPARE(f.value("color").toString(), QStringLiteral("#FF0000"));
        QCOMPARE(f.value("flash").toBool(), false);
        QCOMPARE(f.value("fullScreenFlashOpacity").toDouble(), 0.3);   // global fallback
        QCOMPARE(f.value("auraSpread").toDouble(), 0.18);

        // A per-flag opacity beats the global one
        light.setFlags({QVariantMap{{"rpmLow", 4000}, {"rpmHigh", 8000}, {"fullScreenFlashOpacity", 0.9}}});
        QCOMPARE(light.activeFlag().value("fullScreenFlashOpacity").toDouble(), 0.9);
    }

    void sameBandDoesNotRenotify()
    {
        SettingsManager settings;
        settings.save_setting("rpm_flags", json({band(4000, 8000, "#F00")}));
        ShiftLightManager light(&settings);
        QSignalSpy changed(&light, &ShiftLightManager::activeFlagChanged);

        light.setRpm(4500);
        light.setRpm(4600);
        light.setRpm(4700);
        QCOMPARE(changed.count(), 1);
        light.setRpm(1000);
        QCOMPARE(changed.count(), 2);
    }

    // ── gates ──────────────────────────────────────────────────────────

    void disabledOrSuppressedIsDark()
    {
        SettingsManager settings;
        settings.save_setting("rpm_flags", json({band(4000, 8000, "#F00")}));
        ShiftLightManager light(&settings);
        light.setRpm(5000);
        QVERIFY(light.active());

        settings.save_setting("rpm_shift_light_enabled", false);   // live, via genericSettingChanged
        QVERIFY(!light.enabled());
        QVERIFY(!light.active());
        QVERIFY(!light.lit());

        settings.save_setting("rpm_shift_light_enabled", true);
        QVERIFY(light.active());

        light.setSuppressed(true);
        QVERIFY(!light.active());
        QVERIFY(light.enabled());   // the saved setting is untouched
        QCOMPARE(settings.get_setting("rpm_shift_light_enabled").toBool(), true);
        light.setSuppressed(false);
        QVERIFY(light.active());
    }

    void editsApplyWithoutRestart()
    {
        SettingsManager settings;
        ShiftLightManager light(&settings);
        QSignalSpy flagsChanged(&light, &ShiftLightManager::flagsChanged);
        light.setRpm(5000);
        QVERIFY(!light.active());

        light.setFlags({band(4000, 6000, "#0F0")});
        QCOMPARE(flagsChanged.count(), 1);
        QVERIFY(light.active());
        QCOMPARE(light.activeFlag().value("color").toString(), QStringLiteral("#0F0"));

        settings.save_setting("rpm_glow_size", 0.25);
        QCOMPARE(light.glowSize(), 0.25);
    }

    // ── flash ──────────────────────────────────────────────────────────

    void flashingFlagTogglesAndRestartsLit()
    {
        SettingsManager settings;
        settings.save_setting("rpm_flags", json({band(4000, 6000, "#FF0", true, 30),
                                                 band(6000, 8000, "#F00", true, 30)}));
        ShiftLightManager light(&settings);
        QSignalSpy lit(&light, &ShiftLightManager::litChanged);

        light.setRpm(5000);
        QVERIFY(light.lit());
        QTRY_VERIFY_WITH_TIMEOUT(lit.count() >= 4, 2000);   // on, off, on, off...

        // Entering a new band always starts lit
        QTRY_VERIFY_WITH_TIMEOUT(!light.lit(), 1000);
        light.setRpm(7000);
        QVERIFY(light.lit());

        // Leaving every band stops the flash dark
        light.setRpm(1000);
        QVERIFY(!light.lit());
        const int count = lit.count();
        QTest::qWait(120);
        QCOMPARE(lit.count(), count);
    }

    void steadyFlagStaysLit()
    {
        SettingsManager settings;
        settings.save_setting("rpm_flags", json({band(4000, 8000, "#F00", false, 30)}));
        ShiftLightManager light(&settings);
        QSignalSpy lit(&light, &ShiftLightManager::litChanged);
        light.setRpm(5000);
        QTest::qWait(120);
        QCOMPARE(lit.count(), 1);
        QVERIFY(light.lit());
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_GUILESS_MAIN(TstShiftLightManager)
#include "tst_shiftlightmanager.moc"
