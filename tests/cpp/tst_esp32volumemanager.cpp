// ESP32 knob port selection: with no port saved, only an ESP32-S3 is
// auto-selected (a VM's COM1 / ttyS0 once got the knob's LED commands), and a
// hand-picked port that isn't an ESP32-S3 gets no writes until the knob sends
// a command. A pseudo-terminal stands in for the serial port.

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <pty.h>
#include <unistd.h>

#include "managers/esp32volumemanager.h"
#include "managers/settingsmanager.h"

class TstEsp32VolumeManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        qputenv("HOME", m_home.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_home.path() + "/config").toUtf8());
    }

    void noPortSavedAutoSelectsOnlyAnEsp32S3()
    {
        SettingsManager settings;
        settings.save_esp32_volume_port(QString());
        ESP32VolumeManager mgr(&settings);
        mgr.m_startupTimer.stop();

        mgr.initialConnect();
        if (!mgr.m_esp32S3Port.isEmpty())
            QSKIP("an ESP32-S3 is plugged into this machine");

        QCOMPARE(settings.property("esp32VolumePort").toString(), QString());
        QVERIFY(!mgr.m_serialPort);
        QVERIFY(!mgr.is_connected());
    }

    void otherPortGetsNoWritesUntilTheKnobSpeaks()
    {
        int master = -1, slave = -1;
        char name[128] = {};
        QVERIFY(::openpty(&master, &slave, name, nullptr, nullptr) == 0);
        ::fcntl(master, F_SETFL, ::fcntl(master, F_GETFL) | O_NONBLOCK);

        SettingsManager settings;
        settings.save_esp32_volume_port(QString::fromLatin1(name));
        ESP32VolumeManager mgr(&settings);
        mgr.m_startupTimer.stop();
        QSignalSpy volume(&mgr, &ESP32VolumeManager::volumeChangeRequested);

        mgr.connect_device();
        QVERIFY2(mgr.m_serialPort && mgr.m_serialPort->isOpen(), name);
        QCOMPARE(mgr.get_connection_status(), QStringLiteral("Waiting"));
        QVERIFY(!mgr.is_connected());

        // What main.cpp sends on connect: nothing may reach the port yet
        mgr.send_volume_update(40);
        mgr.send_mute_state(false);
        mgr.send_theme_color(0, 255, 255);
        QTest::qWait(150);
        QCOMPARE(drain(master), QByteArray());

        // A line that isn't a knob command proves nothing
        QVERIFY(::write(master, "hello\n", 6) == 6);
        QTest::qWait(150);   // long enough for readyRead to deliver the line
        QVERIFY(!mgr.is_connected());
        QCOMPARE(mgr.get_connection_status(), QStringLiteral("Waiting"));
        QCOMPARE(volume.count(), 0);

        // The first knob command verifies the link, and counts as a tick
        QVERIFY(::write(master, "+\n", 2) == 2);
        QTRY_VERIFY(mgr.is_connected());
        QCOMPARE(mgr.get_connection_status(), QStringLiteral("Connected"));
        QCOMPARE(volume.count(), 1);

        mgr.send_theme_color(1, 2, 3);
        QByteArray written;
        QTRY_VERIFY2((written += drain(master)).contains("C1,2,3\n"), written.toHex(' ').constData());

        mgr.disconnect_device();
        ::close(slave);
        ::close(master);
    }

private:
    static QByteArray drain(int fd)
    {
        QByteArray out;
        char buf[256];
        qint64 n;
        while ((n = ::read(fd, buf, sizeof buf)) > 0)
            out.append(buf, n);
        return out;
    }

    QTemporaryDir m_home;
};

QTEST_GUILESS_MAIN(TstEsp32VolumeManager)
#include "tst_esp32volumemanager.moc"
