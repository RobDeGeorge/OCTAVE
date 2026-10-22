// OBDManager without an adapter: the demand-driven poll-list rules, and the
// Linux rfcomm bind behind connect_to_adapter(MAC), which must run off the
// GUI thread and report its outcome later.

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "managers/elm327protocol.h"
#include "managers/obdmanager.h"
#include "managers/settingsmanager.h"

using Demand = QHash<QString, QSet<QString>>;

namespace {
// Command names of a poll list, sorted, plus the multi-value PID keys
QStringList baseNames(const QList<PidKey> &pids)
{
    QStringList names;
    for (const PidKey &k : pids) {
        const auto it = ELM327Protocol::pidTable().constFind(k);
        if (it != ELM327Protocol::pidTable().constEnd())
            names << it->commandName;
    }
    names.sort();
    return names;
}

QList<PidKey> extendedKeys(const QList<PidKey> &pids)
{
    QList<PidKey> out;
    for (const PidKey &k : pids)
        if (ELM327Protocol::extendedPidTable().contains(k))
            out << k;
    return out;
}

const PidKey kBoostKey{1, 0x70};
} // namespace

class TstObdManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        qputenv("HOME", m_tmp.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp.path() + "/config").toUtf8());
        qputenv("APPDATA", (m_tmp.path() + "/appdata").toUtf8());
    }

    // ── Poll list ───────────────────────────────────────────────────
    void noDemandPollsOnlyTheHeartbeat()
    {
        QCOMPARE(baseNames(OBDManager::pidsToWatch({}, {})), QStringList{"RPM"});
    }

    void unionOfConsumers()
    {
        const auto pids = OBDManager::pidsToWatch({{"dashboard", {"SPEED"}}, {"cards", {"COOLANT_TEMP", "SPEED"}}}, {});
        QCOMPARE(baseNames(pids), (QStringList{"COOLANT_TEMP", "RPM", "SPEED"}));
        QVERIFY(extendedKeys(pids).isEmpty());
    }

    void imuAndUnknownIdsAreIgnored()
    {
        const Demand demand{{"dashboard", {"PITCH", "ROLL", "HEADING", "LATERAL_G", "NOT_A_PID"}}};
        QCOMPARE(OBDManager::pidsToWatch(demand, {}), (QList<PidKey>{{1, 0x0C}}));
    }

    void supportedSetFiltersDemand()
    {
        const auto pids = OBDManager::pidsToWatch({{"cards", {"SPEED", "FUEL_LEVEL"}}}, {"SPEED", "RPM"});
        QCOMPARE(baseNames(pids), (QStringList{"RPM", "SPEED"}));
    }

    void emptyScanResultFiltersNothing()
    {
        const auto pids = OBDManager::pidsToWatch({{"cards", {"FUEL_LEVEL"}}}, {});
        QCOMPARE(baseNames(pids), (QStringList{"FUEL_LEVEL", "RPM"}));
    }

    void heartbeatFallsBackToElmVoltageWithoutRpm()
    {
        QCOMPARE(baseNames(OBDManager::pidsToWatch({}, {"SPEED", "ELM_VOLTAGE"})), QStringList{"ELM_VOLTAGE"});
    }

    void extendedPidPolledWhenAnySignalWanted()
    {
        const Demand demand{{"dashboard", {"BOOST_PRESSURE_A"}}};
        QCOMPARE(extendedKeys(OBDManager::pidsToWatch(demand, {})), QList<PidKey>{kBoostKey});
        // The vehicle didn't report it
        QVERIFY(extendedKeys(OBDManager::pidsToWatch(demand, {"RPM"})).isEmpty());
    }

    // IGNITION_TIMING is TIMING_ADVANCE under its old name (ParameterCatalog
    // aliasOf): wanting it polls 0x0E, and a car that reports timing advance
    // supports it. Before, a dashboard showing only IGNITION_TIMING polled
    // nothing and the gate called it unsupported.
    void aliasDemandPollsItsTarget()
    {
        QCOMPARE(baseNames(OBDManager::pidsToWatch({{"dashboard", {"IGNITION_TIMING"}}}, {})),
                 (QStringList{"RPM", "TIMING_ADVANCE"}));
        QCOMPARE(baseNames(OBDManager::pidsToWatch({{"dashboard", {"IGNITION_TIMING"}}},
                                                   {"RPM", "TIMING_ADVANCE"})),
                 (QStringList{"RPM", "TIMING_ADVANCE"}));
        QCOMPARE(baseNames(OBDManager::pidsToWatch({{"dashboard", {"IGNITION_TIMING"}}}, {"RPM"})),
                 QStringList{"RPM"});
    }

    void aliasSupportedWithItsTarget()
    {
        QVERIFY(OBDManager::withAliases({"RPM", "TIMING_ADVANCE"}).contains("IGNITION_TIMING"));
        QVERIFY(!OBDManager::withAliases({"RPM", "SPEED"}).contains("IGNITION_TIMING"));
        QCOMPARE(OBDManager::withAliases({}), QStringList{});
    }

    // ── connect_to_adapter(MAC): async rfcomm bind (Linux) ──────────
    void bindFailureReportsErrorWithoutBlocking()
    {
        if (const QString skip = prepareFakeRfcomm(1); !skip.isEmpty())
            QSKIP(qPrintable(skip));
        SettingsManager settings;
        OBDManager obd(&settings);
        QSignalSpy status(&obd, &OBDManager::connectionStatusChanged);
        QSignalSpy log(&obd, &OBDManager::connectionLogLineAppended);

        QElapsedTimer t;
        t.start();
        obd.connect_to_adapter(kMac);
        QVERIFY2(t.elapsed() < 500, "connect_to_adapter blocked on the bind");
        QVERIFY(!statuses(status).contains("Error"));   // not yet: the fake takes 1 s

        QTRY_VERIFY_WITH_TIMEOUT(statuses(status).contains("Error"), 10000);
        QStringList lines;
        for (const QList<QVariant> &a : log)
            lines << a.at(0).toString();
        QVERIFY2(lines.filter("binding " + QString(kMac)).size() == 1, qPrintable(lines.join(" | ")));
        QVERIFY2(!lines.filter("rfcomm bind failed").isEmpty(), qPrintable(lines.join(" | ")));
        QCOMPARE(settings.obdBluetoothPort(), QString(kMac));
    }

    void supersededBindIsIgnored()
    {
        if (const QString skip = prepareFakeRfcomm(1); !skip.isEmpty())
            QSKIP(qPrintable(skip));
        SettingsManager settings;
        OBDManager obd(&settings);
        QSignalSpy status(&obd, &OBDManager::connectionStatusChanged);
        QSignalSpy log(&obd, &OBDManager::connectionLogLineAppended);

        obd.connect_to_adapter(kMac);
        // A newer request (a plain serial port) wins before the bind finishes
        obd.connect_to_adapter(m_tmp.filePath("ttyFAKE0"));
        QTest::qWait(2500);   // the fake bind has failed by now
        QVERIFY2(!statuses(status).contains("Error"), qPrintable(statuses(status).join(", ")));
        QCOMPARE(settings.obdBluetoothPort(), m_tmp.filePath("ttyFAKE0"));
    }

private:
    static constexpr const char *kMac = "AA:BB:CC:DD:EE:FF";

    static QStringList statuses(const QSignalSpy &spy)
    {
        QStringList out;
        for (const QList<QVariant> &a : spy)
            out << a.at(0).toString();
        return out;
    }

    // A stand-in for `rfcomm`: sleeps, then fails like an unprivileged bind.
    // Returns why the test can't run here, or "" when it is ready.
    QString prepareFakeRfcomm(int seconds)
    {
#ifndef Q_OS_LINUX
        Q_UNUSED(seconds);
        return QStringLiteral("rfcomm bind is Linux-only");
#else
        for (int i = 0; i < 8; ++i) {
            if (QFile::exists(QStringLiteral("/dev/rfcomm%1").arg(i)))
                return QStringLiteral("a /dev/rfcommN exists here; the manager would use it instead of binding");
        }
        const QString script = m_tmp.filePath("fake-rfcomm");
        QFile f(script);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QStringLiteral("cannot write the fake rfcomm");
        f.write(QStringLiteral("#!/bin/sh\nsleep %1\necho \"Can't create device: Operation not permitted\" >&2\nexit 1\n")
                    .arg(seconds).toUtf8());
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("OCTAVE_RFCOMM_PROGRAM", script.toUtf8());
        return QString();
#endif
    }

    QTemporaryDir m_tmp;
};

QTEST_MAIN(TstObdManager)
#include "tst_obdmanager.moc"
