// The Bluetooth LE mouse (src/platform/bluezhidmouse) that drives an AirPlay-
// mirrored iPhone: the HID report descriptor and report bytes, start()
// failing cleanly without a system bus, and (opt-in, OCTAVE_TEST_BT_LIVE=1,
// on a machine with BlueZ and an adapter) a start / stop against the real
// bluetoothd that must leave the adapter exactly as it found it.

#include <QTest>
#include <QSignalSpy>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QProcess>

#include "platform/bluezhidmouse.h"

namespace {

// Walks the short items of a HID report descriptor, adding up the bits of
// every Input main item
struct DescriptorSummary {
    bool ok = true;
    int inputBits = 0;
    int depth = 0;
    int maxDepth = 0;
    QList<QPair<quint8, quint32>> items;  // (prefix with size bits cleared, data)
};

DescriptorSummary parse(const QByteArray &d)
{
    DescriptorSummary s;
    quint32 reportSize = 0, reportCount = 0;
    for (int i = 0; i < d.size();) {
        const quint8 prefix = quint8(d[i]);
        int size = prefix & 0x03;
        if (size == 3)
            size = 4;
        if (prefix == 0xFE || i + 1 + size > d.size()) {  // long items: not used here
            s.ok = false;
            return s;
        }
        quint32 data = 0;
        for (int b = 0; b < size; ++b)
            data |= quint32(quint8(d[i + 1 + b])) << (8 * b);
        const quint8 tag = prefix & 0xFC;
        s.items.append({tag, data});
        switch (tag) {
        case 0x74: reportSize = data; break;    // Report Size
        case 0x94: reportCount = data; break;   // Report Count
        case 0x80: s.inputBits += int(reportSize * reportCount); break;  // Input
        case 0xA0: s.maxDepth = qMax(s.maxDepth, ++s.depth); break;     // Collection
        case 0xC0: if (--s.depth < 0) s.ok = false; break;              // End Collection
        default: break;
        }
        i += 1 + size;
    }
    if (s.depth != 0)
        s.ok = false;
    return s;
}

bool bluezOnSystemBus()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    return bus.isConnected() && bus.interface()
           && bus.interface()->isServiceRegistered(QStringLiteral("org.bluez")).value();
}

// Pairable / Powered / ActiveInstances of the first adapter, as a string to
// compare before and after
QString adapterSnapshot()
{
    // Probe hci0..3 (keeps the test free of the nested a{oa{sa{sv}}}
    // demarshalling the class does)
    for (int i = 0; i < 4; ++i) {
        const QString path = QStringLiteral("/org/bluez/hci%1").arg(i);
        auto get = [&](const QString &iface, const QString &name) {
            QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), path,
                                                            QStringLiteral("org.freedesktop.DBus.Properties"),
                                                            QStringLiteral("Get"));
            m << iface << name;
            const QDBusMessage r = QDBusConnection::systemBus().call(m);
            return r.type() == QDBusMessage::ReplyMessage
                       ? qvariant_cast<QDBusVariant>(r.arguments().value(0)).variant()
                       : QVariant();
        };
        const QVariant powered = get(QStringLiteral("org.bluez.Adapter1"), QStringLiteral("Powered"));
        if (!powered.isValid())
            continue;
        return QStringLiteral("%1 powered=%2 pairable=%3 discoverable=%4 adv=%5")
            .arg(path, powered.toString(),
                 get(QStringLiteral("org.bluez.Adapter1"), QStringLiteral("Pairable")).toString(),
                 get(QStringLiteral("org.bluez.Adapter1"), QStringLiteral("Discoverable")).toString(),
                 QString::number(get(QStringLiteral("org.bluez.LEAdvertisingManager1"), QStringLiteral("ActiveInstances")).toInt()));
    }
    return {};
}

}  // namespace

class TstBluezHidMouse : public QObject
{
    Q_OBJECT

private slots:
    void reportMapIsAFiveByteMouse()
    {
        const QByteArray map = BluezHidMouse::reportMap();
        // Usage Page (Generic Desktop), Usage (Mouse), Collection (Application)
        QCOMPARE(map.left(6), QByteArray::fromHex("05010902a101"));
        // ... then Usage (Pointer), Collection (Physical)
        QCOMPARE(map.mid(6, 4), QByteArray::fromHex("0901a100"));
        QVERIFY(map.endsWith(QByteArray::fromHex("c0c0")));

        const DescriptorSummary s = parse(map);
        QVERIFY(s.ok);
        QCOMPARE(s.maxDepth, 2);
        QCOMPARE(s.inputBits, 5 * 8);  // buttons + padding, X, Y, Wheel, AC Pan
        // No Report ID item: one input report, sent bare
        for (const auto &item : s.items)
            QVERIFY2(item.first != 0x84, "the map must not carry a Report ID");
        // Wheel (Generic Desktop 0x38) and AC Pan (Consumer 0x0238) are there
        QVERIFY(map.contains(QByteArray::fromHex("0938")));
        QVERIFY(map.contains(QByteArray::fromHex("050c0a3802")));
        // Three buttons, relative axes in -127..127
        QVERIFY(map.contains(QByteArray::fromHex("05091901290315002501")));
        QVERIFY(map.contains(QByteArray::fromHex("1581257f")));
        QCOMPARE(map.count(QByteArray::fromHex("8106")), 2);  // Input (Data, Var, Rel)
    }

    void encodeReport_data()
    {
        QTest::addColumn<int>("buttons");
        QTest::addColumn<int>("dx");
        QTest::addColumn<int>("dy");
        QTest::addColumn<int>("wheel");
        QTest::addColumn<int>("pan");
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("idle") << 0 << 0 << 0 << 0 << 0 << QByteArray::fromHex("0000000000");
        QTest::newRow("left click") << 1 << 0 << 0 << 0 << 0 << QByteArray::fromHex("0100000000");
        QTest::newRow("right + middle") << 6 << 0 << 0 << 0 << 0 << QByteArray::fromHex("0600000000");
        QTest::newRow("high bits dropped") << 0xFF << 0 << 0 << 0 << 0 << QByteArray::fromHex("0700000000");
        QTest::newRow("move") << 0 << 10 << -10 << 0 << 0 << QByteArray::fromHex("000af60000");
        QTest::newRow("limits") << 0 << 127 << -127 << 1 << -1 << QByteArray::fromHex("007f8101ff");
        QTest::newRow("clamped") << 1 << 500 << -500 << 1000 << -128 << QByteArray::fromHex("017f817f81");
    }

    void encodeReport()
    {
        QFETCH(int, buttons);
        QFETCH(int, dx);
        QFETCH(int, dy);
        QFETCH(int, wheel);
        QFETCH(int, pan);
        QFETCH(QByteArray, bytes);
        QCOMPARE(BluezHidMouse::encodeReport(quint8(buttons), dx, dy, wheel, pan).toHex(), bytes.toHex());
    }

    void idleObject()
    {
        BluezHidMouse mouse;
        QVERIFY(!mouse.isRunning());
        QVERIFY(!mouse.isConnected());
        QVERIFY(mouse.peerName().isEmpty());
        QVERIFY(!mouse.status().isEmpty());
        mouse.sendReport(1, 5, 5);  // not connected: nothing happens, nothing crashes
        mouse.stop();
        QVERIFY(!mouse.isRunning());
    }

    // The system bus is a per-process singleton, so the "no bus" case runs in
    // a child copy of this test with DBUS_SYSTEM_BUS_ADDRESS pointing nowhere
    void startFailsWithoutSystemBus()
    {
        if (qEnvironmentVariableIsSet("OCTAVE_TST_NOBUS_CHILD")) {
            BluezHidMouse mouse;
            QSignalSpy spy(&mouse, &BluezHidMouse::stateChanged);
            QVERIFY(!mouse.start(QStringLiteral("OCTAVE")));
            QVERIFY(!mouse.isRunning());
            QVERIFY(!mouse.isConnected());
            QVERIFY2(mouse.status().contains(QLatin1String("BlueZ")), qPrintable(mouse.status()));
            QVERIFY(spy.count() >= 1);
            mouse.sendReport(1, 1, 1);
            mouse.stop();
            return;
        }
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("OCTAVE_TST_NOBUS_CHILD"), QStringLiteral("1"));
        env.insert(QStringLiteral("DBUS_SYSTEM_BUS_ADDRESS"),
                   QStringLiteral("unix:path=/nonexistent/octave-test-no-bus"));
        child.setProcessEnvironment(env);
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("startFailsWithoutSystemBus")});
        QVERIFY(child.waitForFinished(30000));
        const QByteArray out = child.readAll();
        QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, out.constData());
    }

    // Opt-in: registers a real advertisement and opens the pairing window
    // (Just Works) for a moment, so it doesn't run in a plain ctest
    void liveStartStopRestoresAdapter()
    {
        if (qEnvironmentVariableIsSet("OCTAVE_TST_NOBUS_CHILD"))
            QSKIP("child run");
        if (qEnvironmentVariableIntValue("OCTAVE_TEST_BT_LIVE") != 1)
            QSKIP("set OCTAVE_TEST_BT_LIVE=1 to register with the real bluetoothd");
        if (!bluezOnSystemBus())
            QSKIP("no system bus or no org.bluez");
        const QString before = adapterSnapshot();
        if (before.isEmpty())
            QSKIP("no Bluetooth adapter");
        if (!before.contains(QLatin1String("powered=true")))
            QSKIP("adapter is off; the live test doesn't power it on");
        qInfo() << "adapter before:" << before;

        {
            BluezHidMouse mouse;
            QVERIFY2(mouse.start(QStringLiteral("OCTAVE Test")), qPrintable(mouse.status()));
            QVERIFY(mouse.isRunning());
            QTRY_COMPARE_WITH_TIMEOUT(mouse.status(), QStringLiteral("Waiting for a paired iPhone"), 10000);
            QVERIFY(mouse.isRunning());
            QVERIFY(!mouse.isConnected());
            QVERIFY(!mouse.isPairing());
            const QString during = adapterSnapshot();
            qInfo() << "adapter while running:" << during;
            QCOMPARE(during.contains(QLatin1String("pairable=true")), before.contains(QLatin1String("pairable=true")));
            QVERIFY(during != before);  // one more advertising instance

            // The pairing window makes the adapter pairable, and closing it puts it back
            mouse.setPairing(true);
            QVERIFY(mouse.isPairing());
            QVERIFY(mouse.status().startsWith(QLatin1String("Ready to pair")));
            QVERIFY(adapterSnapshot().contains(QLatin1String("pairable=true")));
            mouse.setPairing(false);
            QVERIFY(!mouse.isPairing());
            QTRY_COMPARE_WITH_TIMEOUT(adapterSnapshot(), during, 3000);

            // Time to look with bluetoothctl by hand
            const int holdMs = qEnvironmentVariableIntValue("OCTAVE_TEST_BT_HOLD_MS");
            if (holdMs > 0)
                QTest::qWait(holdMs);
            QVERIFY(mouse.isRunning());

            mouse.sendReport(1, 3, 3);  // nobody subscribed: a no-op
            mouse.stop();
            QVERIFY(!mouse.isRunning());
            QCOMPARE(adapterSnapshot(), before);

            // A second round on the same object works too
            QVERIFY2(mouse.start(QStringLiteral("OCTAVE Test")), qPrintable(mouse.status()));
            QTRY_COMPARE_WITH_TIMEOUT(mouse.status(), QStringLiteral("Waiting for a paired iPhone"), 10000);
            mouse.setPairing(true);     // the destructor closes the window too
        }  // the destructor stops
        QCOMPARE(adapterSnapshot(), before);
    }
};

QTEST_GUILESS_MAIN(TstBluezHidMouse)
#include "tst_bluezhidmouse.moc"
