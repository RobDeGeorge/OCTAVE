// Settings must survive a power cut: a truncated settings file is restored
// from the last known-good backup instead of being replaced with defaults,
// and a failed write never damages the file on disk.

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

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TstSettingsManager)
#include "tst_settingsmanager.moc"
