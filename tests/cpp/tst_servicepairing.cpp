// ServicePairing (src/service/servicepairing.{h,cpp}): the 6-digit code
// (single use, void after 5 wrong tries, gone after 2 minutes), one attempt
// a second overall and a per-address limit, tokens kept only as hashes in
// the settings file, revoke (with its tickets), media tickets bound to a
// token or, minted locally, good only from this device.

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "managers/settingsmanager.h"
#include "service/servicepairing.h"

class TstServicePairing : public QObject
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

    void codesAreSixDigitsAndSingleUse()
    {
        SettingsManager settings;
        ServicePairing p(&settings);
        QVERIFY(p.pairingCode().isEmpty());
        QString error;
        QVERIFY(p.pair("123456", "x", "10.0.0.2", &error).isEmpty());   // nothing showing
        QVERIFY(error.contains("no pairing code"));
        p.startPairing();
        const QString code = p.pairingCode();
        QCOMPARE(code.size(), 6);
        QVERIFY(code.toInt() >= 0);
        QVERIFY(p.pairingSecondsLeft() > 100);
        QTest::qWait(ServicePairing::kAttemptIntervalMs + 10);
        const QString token = p.pair(code, "  Kitchen   tablet ", "10.0.0.2", &error);
        QCOMPARE(token.size(), 64);
        QVERIFY(p.pairingCode().isEmpty());   // used up
        QTest::qWait(ServicePairing::kAttemptIntervalMs + 10);
        QVERIFY(p.pair(code, "again", "10.0.0.2", &error).isEmpty());
        QTRY_COMPARE(p.devices().size(), 1);
        QCOMPARE(p.devices().first().toMap().value("name").toString(), QStringLiteral("Kitchen tablet"));
        const QString id = p.checkToken(token);
        QVERIFY(!id.isEmpty());
        QVERIFY(p.tokenIdValid(id));
        QVERIFY(p.checkToken(QString(64, 'a')).isEmpty());
        QVERIFY(p.checkToken("short").isEmpty());
    }

    void onlyTheHashIsStored()
    {
        QString token;
        {
            SettingsManager settings;
            ServicePairing p(&settings);
            p.startPairing();
            QString error;
            token = p.pair(p.pairingCode(), "Phone", "10.0.0.3", &error);
            QVERIFY(!token.isEmpty());
            // Written on the GUI thread, after the pairing answered
            QTRY_COMPARE(settings.fileOnlySetting("serviceTokens").toList().size(), 1);
            settings.flushPendingSave();
        }
        QFile f(SettingsManager::getAppDataDir() + "/settingsConfigure.json");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray file = f.readAll();
        QVERIFY(!file.contains(token.toLatin1()));
        QVERIFY(file.contains("serviceTokens"));
        // A fresh start still knows the device
        SettingsManager settings;
        ServicePairing p(&settings);
        QVERIFY(!p.checkToken(token).isEmpty());
        // ...and nothing generic can write the list
        settings.save_setting("serviceTokens", QVariantList{});
        settings.apply_settings({{"serviceTokens", QVariantList{}}, {"serviceLoopbackOpen", false}});
        QVERIFY(!ServicePairing(&settings).checkToken(token).isEmpty());
        QVERIFY(ServicePairing(&settings).loopbackOpen());
    }

    void wrongCodesVoidItAndAreRateLimited()
    {
        SettingsManager settings;
        ServicePairing p(&settings);
        p.startPairing();
        const QString code = p.pairingCode();
        const QString wrong = code == "000000" ? "000001" : "000000";
        QString error;
        QTest::qWait(ServicePairing::kAttemptIntervalMs + 10);
        QVERIFY(p.pair(wrong, "x", "10.0.0.4", &error).isEmpty());
        QCOMPARE(error, QStringLiteral("wrong code"));
        QVERIFY(p.pair(code, "x", "10.0.1.5", &error).isEmpty());   // within a second, even the right one
        QVERIFY(error.contains("too fast"));
        // kMaxTries wrong codes in all (from many addresses, each under its
        // own limit) void the code
        for (int i = 1; i < ServicePairing::kMaxTries; ++i) {
            QTest::qWait(ServicePairing::kAttemptIntervalMs + 10);
            p.pair(wrong, "x", QStringLiteral("10.0.2.%1").arg(i), &error);
        }
        QVERIFY(p.pairingCode().isEmpty());
        QTest::qWait(ServicePairing::kAttemptIntervalMs + 10);
        QVERIFY(p.pair(code, "x", "10.0.3.6", &error).isEmpty());
    }

    void addressesCountByPrefix()
    {
        QCOMPARE(ServicePairing::peerKey("192.168.1.20"), QStringLiteral("192.168.1.20"));
        QCOMPARE(ServicePairing::peerKey("::ffff:192.168.1.20"), QStringLiteral("192.168.1.20"));
        QCOMPARE(ServicePairing::peerKey("2001:db8:1:2:aaaa:bbbb:cccc:dddd"), ServicePairing::peerKey("2001:db8:1:2::1"));
        QVERIFY(ServicePairing::peerKey("2001:db8:1:2::1") != ServicePairing::peerKey("2001:db8:1:3::1"));
    }

    void ticketsAreBounded()
    {
        SettingsManager settings;
        ServicePairing p(&settings);
        const QString first = p.mintTicket(QString());
        for (int i = 0; i < ServicePairing::kMaxTickets; ++i)
            p.mintTicket(QString());
        QVERIFY(!p.checkTicket(first, true));   // the oldest went
    }

    void anAddressWithTooManyFailuresIsRefused()
    {
        SettingsManager settings;
        ServicePairing p(&settings);
        QString error;
        for (int i = 0; i < ServicePairing::kMaxFailuresPerPeer; ++i) {
            if (p.pairingCode().isEmpty())
                p.startPairing();
            const QString wrong = p.pairingCode() == "000000" ? "000001" : "000000";
            QTest::qWait(ServicePairing::kAttemptIntervalMs + 10);
            p.pair(wrong, "x", "10.0.0.9", &error);
        }
        p.startPairing();
        QTest::qWait(ServicePairing::kAttemptIntervalMs + 10);
        QVERIFY(p.pair(p.pairingCode(), "x", "10.0.0.9", &error).isEmpty());   // even the right code
        QVERIFY(error.contains("this address"));
        QTest::qWait(ServicePairing::kAttemptIntervalMs + 10);
        QVERIFY(!p.pair(p.pairingCode(), "x", "10.0.0.10", &error).isEmpty());  // another address is fine
    }

    void revokeVoidsTokenAndTickets()
    {
        SettingsManager settings;
        ServicePairing p(&settings);
        p.startPairing();
        QString error;
        const QString token = p.pair(p.pairingCode(), "Phone", "10.0.0.7", &error);
        const QString id = p.checkToken(token);
        const QString ticket = p.mintTicket(id);
        QCOMPARE(ticket.size(), 32);
        QVERIFY(p.checkTicket(ticket, false));   // a device's ticket works from the network
        QVERIFY(!p.checkTicket(QString(32, 'b'), true));
        QSignalSpy revoked(&p, &ServicePairing::revoked);
        p.revoke(id);
        QCOMPARE(revoked.size(), 1);
        QCOMPARE(revoked.first().at(0).toString(), id);
        QVERIFY(!p.tokenIdValid(id));
        QVERIFY(p.checkToken(token).isEmpty());
        QVERIFY(!p.checkTicket(ticket, false));
        // A ticket minted by a trusted local connection is good from here only
        const QString local = p.mintTicket(QString());
        QVERIFY(p.checkTicket(local, true));
        QVERIFY(!p.checkTicket(local, false));
    }

    void theServiceSwitchWritesTheFileKey()
    {
        SettingsManager settings;
        ServicePairing p(&settings);
        QVERIFY(!p.serviceEnabled());
        p.setServiceEnabled(true);
        QVERIFY(settings.fileOnlySetting("serviceEnabled").toBool());
        settings.save_setting("serviceEnabled", false);   // still refused for the generic path
        QVERIFY(settings.fileOnlySetting("serviceEnabled").toBool());
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_GUILESS_MAIN(TstServicePairing)
#include "tst_servicepairing.moc"
