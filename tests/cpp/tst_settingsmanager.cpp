// Settings must survive a power cut: a truncated settings file is restored
// from the last known-good backup instead of being replaced with defaults,
// and a failed write never damages the file on disk. Every value set through
// the setters survives a restart, and a file from an older build loads with
// its valid values kept and defaults filled in.

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "managers/settingsmanager.h"

class TstSettingsManager : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        // Every test gets its own config dir (SettingsManager::getAppDataDir()
        // reads these at construction)
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        qputenv("HOME", m_tmp->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp->path() + "/config").toUtf8());
        qputenv("APPDATA", (m_tmp->path() + "/appdata").toUtf8());
    }

    void corruptSettingsRestoredFromBackup()
    {
        const QString file = SettingsManager::getAppDataDir() + "/settingsConfigure.json";
        {
            SettingsManager first;
            first.save_setting("startUpVolume", 0.37);
            first.flushPendingSave();
        }
        QVERIFY(QFile::exists(file));

        // Second boot reads the good file and backs it up
        { SettingsManager second; }
        QVERIFY(QFile::exists(file + ".bak"));

        // Power cut mid-write: file truncated
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("{\"startUpVol");
        }

        SettingsManager third;
        QCOMPARE(third.get_setting_with_default("startUpVolume", 0).toDouble(), 0.37);
        QVERIFY(QFile::exists(file + ".corrupt"));
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(f.readAll()).object().value("startUpVolume").toDouble(), 0.37);
    }

    void failedWriteLeavesExistingFileIntact()
    {
#ifndef Q_OS_UNIX
        QSKIP("uses a read-only directory to make the write fail");
#else
        if (geteuid() == 0)
            QSKIP("root ignores directory permissions");
        const QString dir = SettingsManager::getAppDataDir();
        const QString file = dir + "/settingsConfigure.json";
        SettingsManager manager;
        QFile before(file);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray original = before.readAll();
        before.close();

        // The write fails partway, as on a full disk: QSaveFile can't create
        // its temp file next to the target
        QVERIFY(::chmod(QFile::encodeName(dir).constData(), 0555) == 0);
        manager.save_setting("startUpVolume", 0.81);
        manager.flushPendingSave();
        ::chmod(QFile::encodeName(dir).constData(), 0755);

        QFile after(file);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), original);
        const QStringList leftovers = QDir(dir).entryList({"*.json.*", "settingsConfigure.json?*"}, QDir::Files);
        QVERIFY2(leftovers.isEmpty() || leftovers == QStringList{"settingsConfigure.json.bak"},
                 qPrintable(leftovers.join(", ")));
#endif
    }

    void everySettingSurvivesARestart()
    {
        QJsonObject saved;
        {
            SettingsManager first;
            first.setCurrentVolume(63);
            first.save_start_volume(0.42f);
            first.save_theme_setting(QStringLiteral("Midnight"));
            first.save_ui_scale(1.25f);
            first.save_clock_format(false);
            first.save_clock_show_seconds(true);
            first.save_media_folder(m_tmp->path() + "/Music");
            first.save_obd_bluetooth_port(QStringLiteral("AA:BB:CC:DD:EE:FF"));
            first.save_obd_saved_adapters(QStringLiteral("[{\"mac\":\"AA:BB:CC:DD:EE:FF\",\"name\":\"NEXAS\"}]"));
            first.save_home_obd_parameters({QStringLiteral("RPM"), QStringLiteral("SPEED")});
            first.save_obd_parameter_enabled(QStringLiteral("COOLANT_TEMP"), false);
            first.save_custom_theme(QStringLiteral("Mine"), QStringLiteral("{\"accent\":\"#ff0000\"}"));
            first.save_gesture_action(QStringLiteral("up"), QStringLiteral("volume_up"));
            first.save_scrcpy_bit_rate_wifi(6);
            first.save_settings_section_visibility(QStringLiteral("obd"), false);
            first.save_playback_state(QStringLiteral("a.mp3"), 1234, QStringLiteral("Road"));
            first.flushPendingSave();
            saved = first.m_settings;

            // The setters really changed things, so the comparison below means something
            int changed = 0;
            for (auto it = saved.begin(); it != saved.end(); ++it)
                changed += first.m_defaultSettings.value(it.key()) != it.value();
            QVERIFY2(changed >= 12, qPrintable(QString::number(changed)));
        }

        SettingsManager second;
        QStringList differ;
        for (auto it = saved.begin(); it != saved.end(); ++it) {
            if (second.m_settings.value(it.key()) != it.value())
                differ << it.key();
        }
        QVERIFY2(differ.isEmpty(), qPrintable(differ.join(", ")));
        QCOMPARE(second.m_settings.size(), saved.size());
        // The live volume is not saved: a restart starts at the start-up
        // volume (linear 0.42 -> sqrt -> 65 %), not the last volume (63)
        QCOMPARE(second.currentVolume(), 65);
    }

    void olderFileLoadsWithDefaultsFilledIn()
    {
        const QString dir = SettingsManager::getAppDataDir();
        QVERIFY(QDir().mkpath(dir));
        // An older build: few keys, one with the wrong type, one this build doesn't know
        const QJsonObject old{{"startUpVolume", 0.3}, {"themeSetting", "Ocean"},
                              {"showClock", "yes"}, {"someFutureKey", "kept"}};
        {
            QFile f(dir + "/settingsConfigure.json");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(old).toJson());
        }

        SettingsManager mgr;
        QCOMPARE(mgr.m_settings.value("startUpVolume").toDouble(), 0.3);
        QCOMPARE(mgr.m_settings.value("themeSetting").toString(), QStringLiteral("Ocean"));
        QCOMPARE(mgr.m_settings.value("showClock"), mgr.m_defaultSettings.value("showClock"));   // wrong type: default
        QCOMPARE(mgr.m_settings.value("someFutureKey").toString(), QStringLiteral("kept"));
        QStringList missing;
        for (auto it = mgr.m_defaultSettings.begin(); it != mgr.m_defaultSettings.end(); ++it) {
            if (!mgr.m_settings.contains(it.key()))
                missing << it.key();
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(", ")));
        QVERIFY(!QFile::exists(dir + "/settingsConfigure.json.corrupt"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TstSettingsManager)
#include "tst_settingsmanager.moc"
