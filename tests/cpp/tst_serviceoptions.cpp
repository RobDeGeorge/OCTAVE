// Whether the service runs and where it listens (src/service/serviceoptions.h):
// off unless asked for, loopback unless told otherwise, and --profile only
// turns it on in a build with the dev tools.

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "service/serviceoptions.h"

using OctaveService::ServiceOptions;
using OctaveService::parseServiceOptions;
using OctaveService::webRootProblem;

class TstServiceOptions : public QObject
{
    Q_OBJECT

private slots:
    void offByDefault()
    {
        const ServiceOptions o = parseServiceOptions({"octave"}, false, true);
        QVERIFY(!o.enabled);
        QVERIFY(o.loopbackOnly());
        // A port alone does not turn it on
        QVERIFY(!parseServiceOptions({"octave", "--command-port", "5000"}, false, true).enabled);
        QVERIFY(!parseServiceOptions({"octave", "--dev"}, false, true).enabled);
    }

    void serviceFlag()
    {
        ServiceOptions o = parseServiceOptions({"octave", "--service"}, false, false);
        QVERIFY(o.enabled);
        QCOMPARE(o.port, ServiceOptions::kDefaultPort);
        QVERIFY(o.loopbackOnly());

        o = parseServiceOptions({"octave", "--service", "7000", "--smoke"}, false, false);
        QVERIFY(o.enabled);
        QCOMPARE(o.port, quint16(7000));

        // Not a number: the next flag, left alone
        o = parseServiceOptions({"octave", "--service", "--smoke"}, false, false);
        QVERIFY(o.enabled);
        QCOMPARE(o.port, ServiceOptions::kDefaultPort);

        o = parseServiceOptions({"octave", "--service=0"}, false, false);   // any free port
        QVERIFY(o.enabled);
        QCOMPARE(o.port, quint16(0));

        o = parseServiceOptions({"octave", "--service", "--command-port=7001"}, false, false);
        QCOMPARE(o.port, quint16(7001));
    }

    void setting()
    {
        const ServiceOptions o = parseServiceOptions({"octave"}, true, false);
        QVERIFY(o.enabled);
        QVERIFY(o.loopbackOnly());
    }

    void profileOnlyWithDevTools()
    {
        QVERIFY(parseServiceOptions({"octave", "--profile"}, false, true).enabled);
        QVERIFY(!parseServiceOptions({"octave", "--profile"}, false, false).enabled);
    }

    void headlessImpliesIt()
    {
        ServiceOptions o = parseServiceOptions({"octave", "--headless"}, false, false);
        QVERIFY(o.enabled);
        QVERIFY(o.loopbackOnly());
        o = parseServiceOptions({"octave", "--headless", "--service", "7002"}, false, false);
        QCOMPARE(o.port, quint16(7002));
        // ...but not past a bad value
        QVERIFY(!parseServiceOptions({"octave", "--headless", "--service-bind", "x"}, false, false).enabled);
    }

    void webRoot()
    {
        QVERIFY(parseServiceOptions({"octave", "--service"}, false, false).webRoot.isEmpty());
        ServiceOptions o = parseServiceOptions({"octave", "--service", "--web-root", "/srv/ui/"}, false, false);
        QVERIFY(o.enabled);
        QCOMPARE(o.webRoot, QStringLiteral("/srv/ui"));
        o = parseServiceOptions({"octave", "--service", "--web-root=/srv/a/../b"}, false, false);
        QCOMPARE(o.webRoot, QStringLiteral("/srv/b"));
        // Relative to where the app was started
        o = parseServiceOptions({"octave", "--service", "--web-root", "dist"}, false, false);
        QCOMPARE(o.webRoot, QDir::current().absoluteFilePath("dist"));
        // The setting, and the flag over it
        QCOMPARE(parseServiceOptions({"octave", "--service"}, false, false, "/srv/set").webRoot, QStringLiteral("/srv/set"));
        QCOMPARE(parseServiceOptions({"octave", "--service", "--web-root", "/srv/flag"}, false, false, "/srv/set").webRoot,
                 QStringLiteral("/srv/flag"));
        // It does not turn the service on by itself, and an empty one is an error
        QVERIFY(!parseServiceOptions({"octave", "--web-root", "/srv/ui"}, false, false).enabled);
        QVERIFY(!parseServiceOptions({"octave", "--service", "--web-root="}, false, false).enabled);
        QVERIFY(!parseServiceOptions({"octave", "--service", "--web-root"}, false, false).enabled);
    }

    void webRootMustHoldAPage()
    {
        QTemporaryDir tmp;
        QVERIFY(webRootProblem(tmp.filePath("missing")).contains("not a directory"));
        QVERIFY(webRootProblem(tmp.path()).contains("no index.html"));   // "/" or $HOME by mistake
        QFile page(tmp.filePath("index.html"));
        QVERIFY(page.open(QIODevice::WriteOnly));
        page.close();
        QVERIFY(webRootProblem(tmp.path()).isEmpty());
        QVERIFY(!webRootProblem("/").isEmpty());
    }

    void bindAddress()
    {
        ServiceOptions o = parseServiceOptions({"octave", "--service", "--service-bind", "0.0.0.0"}, false, false);
        QVERIFY(o.enabled);
        QVERIFY(!o.loopbackOnly());
        QCOMPARE(o.bind, QHostAddress(QHostAddress::AnyIPv4));

        o = parseServiceOptions({"octave", "--service", "--service-bind=::1"}, false, false);
        QVERIFY(o.loopbackOnly());

        // An address alone does not turn it on
        QVERIFY(!parseServiceOptions({"octave", "--service-bind", "0.0.0.0"}, false, false).enabled);
    }

    void badValuesKeepItOff()
    {
        ServiceOptions o = parseServiceOptions({"octave", "--service", "--service-bind", "the-car"}, false, false);
        QVERIFY(!o.enabled);
        QVERIFY(o.error.contains("the-car"));

        o = parseServiceOptions({"octave", "--service=99999"}, false, false);
        QVERIFY(!o.enabled);
        QVERIFY(!o.error.isEmpty());

        o = parseServiceOptions({"octave", "--service", "--command-port", "x"}, false, false);
        QVERIFY(!o.enabled);
    }
};

QTEST_GUILESS_MAIN(TstServiceOptions)
#include "tst_serviceoptions.moc"
