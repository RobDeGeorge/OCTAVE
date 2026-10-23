// The service protocol (src/service/): ops on the command server's socket,
// next to the dev tooling's `action` form, which keeps working unchanged.
// A real socket against a server on an ephemeral port.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTest>

#include <memory>

#include "service/commandserver.h"
#include "service/protocol.h"
#include "service/servicebridge.h"
#include "service/serviceregistry.h"

using namespace OctaveService;

// What a manager looks like to the bridge
class Fixture : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int level READ level WRITE setLevel NOTIFY levelChanged)
    Q_PROPERTY(Mode mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(QString label READ label CONSTANT)

public:
    enum class Mode { Off, Slow, Fast };
    Q_ENUM(Mode)

    int level() const { return m_level; }
    void setLevel(int v) { if (v != m_level) { m_level = v; emit levelChanged(); } }
    Mode mode() const { return m_mode; }
    QString label() const { return QStringLiteral("fixture"); }

    Q_INVOKABLE QString greet(const QString &who) const { return QStringLiteral("hi ") + who; }
    Q_INVOKABLE int add(int a, int b) const { return a + b; }
    Q_INVOKABLE int add(int a) const { return a + 1; }

public slots:
    void setMode(Fixture::Mode m) { if (m != m_mode) { m_mode = m; emit modeChanged(); } }

signals:
    void levelChanged();
    void modeChanged();
    void pinged(const QString &from, int count);

private:
    void hidden() {}
    int m_level = 3;
    Mode m_mode = Mode::Off;
};

class TstServiceBridge : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_registry = std::make_unique<ServiceRegistry>();
        m_registry->add("first", &m_first);
        m_registry->add("second", &m_second);
        m_server = std::make_unique<CommandServer>(0);
        m_bridge = std::make_unique<ServiceBridge>(m_registry.get(), AppInfo{"1.2.3", true, false});
        m_bridge->install(*m_server);
        QVERIFY(m_server->start());
        QVERIFY(m_server->port() != 0);
        m_socket = std::make_unique<QTcpSocket>();
        m_socket->connectToHost(QHostAddress::LocalHost, m_server->port());
        QVERIFY(m_socket->waitForConnected(3000));
    }

    void cleanup()
    {
        m_socket.reset();
        m_server.reset();
        m_bridge.reset();
        m_registry.reset();
    }

    void helloReportsTheProtocol()
    {
        const QJsonObject reply = send(R"({"op": "hello", "id": 7})");
        QCOMPARE(reply.value("ok").toBool(), true);
        QCOMPARE(reply.value("id").toInt(), 7);   // echoed for pipelining
        const QJsonObject r = reply.value("result").toObject();
        QCOMPARE(r.value("protocolVersion").toInt(), kProtocolVersion);
        QCOMPARE(r.value("app").toObject().value("version").toString(), QStringLiteral("1.2.3"));
        QCOMPARE(r.value("app").toObject().value("build").toString(), QStringLiteral("debug"));
        QCOMPARE(r.value("objects").toVariant().toStringList(), (QStringList{"first", "second"}));
    }

    void unknownOpIsAnError()
    {
        const QJsonObject reply = send(R"({"op": "nope", "id": "x"})");
        QCOMPARE(reply.value("ok").toBool(), false);
        QCOMPARE(reply.value("id").toString(), QStringLiteral("x"));
        QVERIFY(reply.value("error").toString().contains("Unknown op"));
    }

    void legacyActionsStillWork()
    {
        // The dev clients' form, byte for byte
        const QJsonObject reply = send(R"({"action": "ping", "args": {}})");
        QCOMPARE(reply.value("ok").toBool(), true);
        QCOMPARE(reply.value("result").toString(), QStringLiteral("pong"));
        QVERIFY(!reply.contains("id"));
    }

    void describeListsWhatTheClassDeclares()
    {
        m_registry->add("fixture", &m_fixture);
        const QJsonObject d = send(R"({"op": "describe", "object": "fixture"})").value("result").toObject();
        QCOMPARE(d.value("class").toString(), QStringLiteral("Fixture"));
        QStringList props;
        for (const QJsonValue &p : d.value("properties").toArray())
            props << p.toObject().value("name").toString();
        QCOMPARE(props, (QStringList{"level", "mode", "label"}));   // no objectName (QObject's)
        const QJsonObject level = d.value("properties").toArray().at(0).toObject();
        QCOMPARE(level.value("type").toString(), QStringLiteral("int"));
        QCOMPARE(level.value("writable").toBool(), true);
        QCOMPARE(level.value("notify").toString(), QStringLiteral("levelChanged"));
        const QJsonObject mode = d.value("properties").toArray().at(1).toObject();
        QCOMPARE(mode.value("enum").toVariant().toStringList(), (QStringList{"Off", "Slow", "Fast"}));
        QCOMPARE(d.value("properties").toArray().at(2).toObject().value("constant").toBool(), true);

        QStringList methods;
        for (const QJsonValue &m : d.value("methods").toArray())
            methods << m.toObject().value("name").toString() + "/" + QString::number(m.toObject().value("params").toArray().size());
        QVERIFY(methods.contains("greet/1"));
        QVERIFY(methods.contains("add/2"));
        QVERIFY(methods.contains("add/1"));   // overloads listed apart
        QVERIFY(methods.contains("setMode/1"));
        QVERIFY(!methods.contains("deleteLater/0"));
        QStringList sigs;
        for (const QJsonValue &m : d.value("signals").toArray())
            sigs << m.toObject().value("name").toString();
        QCOMPARE(sigs, (QStringList{"levelChanged", "modeChanged", "pinged"}));
        QCOMPARE(d.value("signals").toArray().at(2).toObject().value("params").toArray().at(1).toObject().value("name").toString(),
                 QStringLiteral("count"));

        // All objects at once, with the version
        const QJsonObject all = send(R"({"op": "describe"})").value("result").toObject();
        QCOMPARE(all.value("protocolVersion").toInt(), kProtocolVersion);
        QVERIFY(all.value("objects").toObject().contains("fixture"));
        QVERIFY(send(R"({"op": "describe", "object": "nope"})").value("error").toString().contains("Unknown object"));
    }

    void enumsConvertByNameOrNumber()
    {
        const QMetaType t = QMetaType::fromType<Fixture::Mode>();
        QVariant byName(QStringLiteral("Fast"));
        QVERIFY(convertTo(byName, t));
        QCOMPARE(byName.value<Fixture::Mode>(), Fixture::Mode::Fast);
        QVariant byNumber(1);
        QVERIFY(convertTo(byNumber, t));
        QCOMPARE(byNumber.value<Fixture::Mode>(), Fixture::Mode::Slow);
        QVariant bad(QStringLiteral("Warp"));
        QVERIFY(!convertTo(bad, t));
        QVariant outOfRange(7);
        QVERIFY(!convertTo(outOfRange, t));

        // A slot taking the enum, called by key name
        invokeByName(&m_fixture, "setMode", {QStringLiteral("Slow")});
        QCOMPARE(m_fixture.mode(), Fixture::Mode::Slow);
        QCOMPARE(invokeByName(&m_fixture, "add", {2, 3}).toInt(), 5);
        QCOMPARE(invokeByName(&m_fixture, "add", {2}).toInt(), 3);
    }

    void registryForgetsDeletedObjects()
    {
        auto *temp = new QObject;
        m_registry->add("temp", temp);
        QCOMPARE(m_registry->object("temp"), temp);
        delete temp;
        QCOMPARE(m_registry->object("temp"), nullptr);
    }

private:
    QJsonObject send(const QByteArray &line)
    {
        m_socket->write(line + '\n');
        m_socket->waitForBytesWritten(3000);
        QByteArray buf;
        while (!buf.contains('\n') && m_socket->waitForReadyRead(5000))
            buf += m_socket->readAll();
        return QJsonDocument::fromJson(buf.left(buf.indexOf('\n'))).object();
    }

    QObject m_first, m_second;
    Fixture m_fixture;
    std::unique_ptr<ServiceRegistry> m_registry;
    std::unique_ptr<CommandServer> m_server;
    std::unique_ptr<ServiceBridge> m_bridge;
    std::unique_ptr<QTcpSocket> m_socket;
};

QTEST_GUILESS_MAIN(TstServiceBridge)
#include "tst_servicebridge.moc"
