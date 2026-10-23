// Clock (src/managers/clock.{h,cpp}): `time` is a readable property that
// notifies on timeChanged with the same text, holds a value from
// construction, follows the format settings, and is "" with the clock off.

#include <QMetaProperty>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "managers/clock.h"
#include "managers/settingsmanager.h"

class TstClock : public QObject
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

    void timeIsAPropertyNotifiedByTimeChanged()
    {
        SettingsManager settings;
        Clock clock(&settings);
        const QMetaProperty p = clock.metaObject()->property(clock.metaObject()->indexOfProperty("time"));
        QVERIFY(p.isValid() && p.isReadable() && !p.isWritable());
        QCOMPARE(p.notifySignal().name(), QByteArray("timeChanged"));
        QCOMPARE(p.notifySignal().parameterCount(), 1);   // the argument stays
        QVERIFY(!clock.time().isEmpty());                // from construction
        QCOMPARE(p.read(&clock).toString(), clock.time());
    }

    void followsTheFormatAndTheSwitch()
    {
        SettingsManager settings;
        Clock clock(&settings);
        QSignalSpy changed(&clock, &Clock::timeChanged);
        settings.save_clock_format(true);
        settings.save_clock_show_seconds(true);
        clock.update_time();
        QVERIFY2(QRegularExpression("^\\d\\d:\\d\\d:\\d\\d$").match(clock.time()).hasMatch(), qPrintable(clock.time()));
        QVERIFY(!changed.isEmpty());
        QCOMPARE(changed.last().at(0).toString(), clock.time());

        settings.save_clock_format(false);
        settings.save_clock_show_seconds(false);
        clock.update_time();
        QVERIFY2(QRegularExpression("^\\d\\d:\\d\\d [AP]M$").match(clock.time()).hasMatch(), qPrintable(clock.time()));
        // Same text again: no notify
        const qsizetype before = changed.size();
        clock.update_time();
        QCOMPARE(changed.size(), before);

        settings.save_show_clock(false);
        clock.update_time();
        QCOMPARE(clock.time(), QString());
        QCOMPARE(changed.last().at(0).toString(), QString());
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_GUILESS_MAIN(TstClock)
#include "tst_clock.moc"
