// ESP32 knob serial reader: lines split across reads are joined, and a
// partial line with no newline must not grow forever.

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include "managers/esp32volumemanager.h"

namespace {
// The volume steps the manager asked for, in order (step size is 1 without settings)
QList<float> steps(const QSignalSpy &spy)
{
    QList<float> out;
    for (const QList<QVariant> &args : spy)
        out.append(args.at(0).toFloat());
    return out;
}
} // namespace

class TstEsp32ReadBuffer : public QObject
{
    Q_OBJECT

private slots:
    void garbageWithoutNewlineIsDroppedAndWarnedOnce()
    {
        ESP32VolumeManager mgr;
        QSignalSpy volume(&mgr, &ESP32VolumeManager::volumeChangeRequested);
        const QByteArray garbage(100, 'x');

        // 2000 bytes with no newline: dropped once over the limit, with one warning.
        // The "+" ends the noisy line, so it goes with it; the reader resyncs on
        // that newline and "-" parses. A second overflow is dropped silently.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("no newline"));
        QTest::failOnWarning(QRegularExpression("no newline"));
        for (int i = 0; i < 20; ++i)
            mgr.feedSerialData(garbage);
        mgr.feedSerialData("+\n");
        mgr.feedSerialData("-\n");
        for (int i = 0; i < 20; ++i)
            mgr.feedSerialData(garbage);
        mgr.feedSerialData("\n+\n");

        QCOMPARE(steps(volume), (QList<float>{-1.0f, 1.0f}));
    }

    void shortPartialLinesAreJoined()
    {
        ESP32VolumeManager mgr;
        QSignalSpy volume(&mgr, &ESP32VolumeManager::volumeChangeRequested);
        QSignalSpy mute(&mgr, &ESP32VolumeManager::muteToggleRequested);
        mgr.feedSerialData("+");
        mgr.feedSerialData("3\n");
        mgr.feedSerialData("-\nM");
        mgr.feedSerialData("\n");
        QCOMPARE(steps(volume), (QList<float>{3.0f, -1.0f}));
        QCOMPARE(mute.count(), 1);
    }

    void partialLineAtLimitIsKept()
    {
        ESP32VolumeManager mgr;
        QSignalSpy volume(&mgr, &ESP32VolumeManager::volumeChangeRequested);
        // Exactly the limit, still waiting for its newline: kept, then completed
        const QByteArray body = "+" + QByteArray(ESP32VolumeManager::ESP32_MAX_LINE_BYTES - 2, '0') + "2";
        QCOMPARE(body.size(), ESP32VolumeManager::ESP32_MAX_LINE_BYTES);
        QTest::failOnWarning(QRegularExpression("no newline"));
        mgr.feedSerialData(body);
        mgr.feedSerialData("\n");
        QCOMPARE(steps(volume), (QList<float>{2.0f}));
    }
};

QTEST_MAIN(TstEsp32ReadBuffer)
#include "tst_esp32readbuffer.moc"
