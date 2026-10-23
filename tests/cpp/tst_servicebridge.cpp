// The service protocol (src/service/): ops on the command server's socket,
// next to the dev tooling's `action` form, which keeps working unchanged.
// A real socket against a server on an ephemeral port.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QQmlPropertyMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTest>
#include <QThread>

#include <memory>

#include "service/commandserver.h"
#include "util/variantlistmodel.h"
#include "service/protocol.h"
#include "service/servicebridge.h"
#include "service/serviceregistry.h"

using namespace OctaveService;

class Store : public QQmlPropertyMap
{
    Q_OBJECT
public:
    Store() : QQmlPropertyMap(this, nullptr) {}
signals:
    void changed(const QStringList &ids);
};

// What a manager looks like to the bridge
class Fixture : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int level READ level WRITE setLevel NOTIFY levelChanged)
    Q_PROPERTY(Mode mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(Mode preferred READ preferred WRITE setPreferred NOTIFY preferredChanged)
    Q_PROPERTY(QAbstractListModel *queue READ queue CONSTANT)

public:
    enum class Mode { Off, Slow, Fast };
    Q_ENUM(Mode)

    int level() const { return m_level; }
    void setLevel(int v) { if (v != m_level) { m_level = v; emit levelChanged(); } }
    Mode mode() const { return m_mode; }
    QString label() const { return QStringLiteral("fixture"); }
    Mode preferred() const { return m_preferred; }
    QAbstractListModel *queue() const { return m_queue; }
    VariantListModel *m_queue = new VariantListModel({"songId", "status"}, this);
    void setPreferred(Mode m) { if (m != m_preferred) { m_preferred = m; emit preferredChanged(); } }

    Q_INVOKABLE QString greet(const QString &who) const { return QStringLiteral("hi ") + who; }
    Q_INVOKABLE int add(int a, int b) const { return a + b; }
    Q_INVOKABLE int add(int a) const { return a + 1; }

public slots:
    void setMode(Fixture::Mode m) { if (m != m_mode) { m_mode = m; emit modeChanged(); } }

signals:
    void levelChanged();
    void modeChanged();
    void pinged(const QString &from, int count);
    void preferredChanged();

private:
    void hidden() {}
    int m_level = 3;
    Mode m_mode = Mode::Off;
    Mode m_preferred = Mode::Off;
};

// Members typed QVariant or Qt JSON: the value inside goes on the wire,
// never the type's name (dashboardManager.loadDashboard() returns QVariant)
class Shapes : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariant spec READ spec NOTIFY specChanged)
    Q_PROPERTY(QJsonObject json READ json CONSTANT)

public:
    QVariant spec() const { return QVariantMap{{"schema", 1}, {"cells", QVariantList{"a", 2}}}; }
    QJsonObject json() const { return QJsonObject{{"k", "v"}}; }

    Q_INVOKABLE QVariant variantMap() const { return spec(); }
    Q_INVOKABLE QVariant variantList() const { return QVariantList{1, "two", QVariantMap{{"three", 3}}}; }
    Q_INVOKABLE QVariant variantString() const { return QStringLiteral("text"); }
    Q_INVOKABLE QVariant variantNone() const { return {}; }
    Q_INVOKABLE QVariant echo(const QVariant &v) const { return v; }
    Q_INVOKABLE QJsonValue jsonValue() const { return QJsonValue(QJsonObject{{"n", 1.5}}); }
    Q_INVOKABLE QJsonObject jsonObject() const { return QJsonObject{{"a", QJsonArray{1, 2}}}; }
    Q_INVOKABLE QJsonArray jsonArray() const { return QJsonArray{"x", QJsonObject{{"y", true}}}; }

signals:
    void specChanged();
    void loaded(const QVariant &spec, const QJsonObject &meta);
};

// Consumer-keyed demand, as obdManager / berryIMU have it
class Demand : public QObject
{
    Q_OBJECT
public:
    QHash<QString, QStringList> ids;
    QHash<QString, bool> wanted;
    int releases = 0;
    Q_INVOKABLE void setDemand(const QString &consumer, const QStringList &list)
    {
        if (list.isEmpty()) { ids.remove(consumer); ++releases; } else { ids.insert(consumer, list); }
    }
    Q_INVOKABLE void setWanted(const QString &consumer, bool on)
    {
        if (!on) { wanted.remove(consumer); ++releases; } else { wanted.insert(consumer, true); }
    }
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
        m_buf.clear();
        m_lines.clear();
        m_events.clear();
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
        QCOMPARE(props, (QStringList{"level", "mode", "label", "preferred", "queue"}));   // no objectName (QObject's)
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
        QCOMPARE(sigs, (QStringList{"levelChanged", "modeChanged", "pinged", "preferredChanged"}));
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

    void getSetAndCall()
    {
        m_registry->add("fixture", &m_fixture);
        m_fixture.setMode(Fixture::Mode::Off);   // an earlier test moved it
        QCOMPARE(result(R"({"op": "get", "object": "fixture", "property": "level"})").toInt(), 3);
        QCOMPARE(result(R"({"op": "get", "object": "fixture", "property": "mode"})").toString(), QStringLiteral("Off"));

        QCOMPARE(result(R"({"op": "set", "object": "fixture", "property": "level", "value": 9})").toInt(), 9);
        QCOMPARE(m_fixture.level(), 9);
        // An enum property by key: QObject::setProperty alone would refuse a string
        QCOMPARE(result(R"({"op": "set", "object": "fixture", "property": "preferred", "value": "Fast"})").toString(),
                 QStringLiteral("Fast"));
        QCOMPARE(m_fixture.preferred(), Fixture::Mode::Fast);
        QVERIFY(error(R"({"op": "set", "object": "fixture", "property": "preferred", "value": "Warp"})").contains("takes"));
        QVERIFY(error(R"({"op": "set", "object": "fixture", "property": "mode", "value": "Fast"})").contains("read-only"));
        QVERIFY(error(R"({"op": "set", "object": "fixture", "property": "label", "value": "x"})").contains("read-only"));
        QVERIFY(error(R"({"op": "get", "object": "fixture", "property": "nope"})").contains("no property"));

        QCOMPARE(result(R"({"op": "call", "object": "fixture", "method": "greet", "args": ["rob"]})").toString(),
                 QStringLiteral("hi rob"));
        QCOMPARE(result(R"({"op": "call", "object": "fixture", "method": "add", "args": [2, 3]})").toInt(), 5);
        QVERIFY(result(R"({"op": "call", "object": "fixture", "method": "setMode", "args": ["Slow"]})").isNull());
        QCOMPARE(m_fixture.mode(), Fixture::Mode::Slow);
        QVERIFY(error(R"({"op": "call", "object": "fixture", "method": "greet", "args": []})").contains("no method"));
    }

    void variantAndJsonValuesAreSerialised()
    {
        Shapes shapes;
        m_registry->add("shapes", &shapes);
        const QVariantMap spec{{"schema", 1}, {"cells", QVariantList{"a", 2}}};
        // Returns
        QCOMPARE(result(R"({"op": "call", "object": "shapes", "method": "variantMap"})").toMap(), spec);
        QCOMPARE(result(R"({"op": "call", "object": "shapes", "method": "variantList"})").toList(),
                 (QVariantList{1, "two", QVariantMap{{"three", 3}}}));
        QCOMPARE(result(R"({"op": "call", "object": "shapes", "method": "variantString"})").toString(), QStringLiteral("text"));
        QJsonObject none = send(R"({"op": "call", "object": "shapes", "method": "variantNone"})");
        QVERIFY(none.value("ok").toBool());
        QVERIFY(none.contains("result") && none.value("result").isNull());
        QCOMPARE(result(R"({"op": "call", "object": "shapes", "method": "echo", "args": [{"x": [1]}]})").toMap(),
                 (QVariantMap{{"x", QVariantList{1}}}));
        QCOMPARE(result(R"({"op": "call", "object": "shapes", "method": "jsonValue"})").toMap(), (QVariantMap{{"n", 1.5}}));
        QCOMPARE(result(R"({"op": "call", "object": "shapes", "method": "jsonObject"})").toMap(),
                 (QVariantMap{{"a", QVariantList{1, 2}}}));
        QCOMPARE(result(R"({"op": "call", "object": "shapes", "method": "jsonArray"})").toList(),
                 (QVariantList{"x", QVariantMap{{"y", true}}}));
        // Properties
        QCOMPARE(result(R"({"op": "get", "object": "shapes", "property": "spec"})").toMap(), spec);
        QCOMPARE(result(R"({"op": "get", "object": "shapes", "property": "json"})").toMap(), (QVariantMap{{"k", "v"}}));
        // Signal arguments
        QVERIFY(result(R"({"op": "subscribe", "object": "shapes", "signals": ["loaded"]})").toMap().value("sub").toInt() > 0);
        emit shapes.loaded(spec, QJsonObject{{"id", "d1"}});
        QTRY_COMPARE_WITH_TIMEOUT(collect().size(), 1, 2000);
        const QJsonArray args = m_events.first().value("args").toArray();
        QCOMPARE(args.at(0).toVariant().toMap(), spec);
        QCOMPARE(args.at(1).toObject().value("id").toString(), QStringLiteral("d1"));
    }

    void aClosedConnectionReleasesItsDemand()
    {
        Demand demand;
        m_registry->add("demand", &demand);
        m_bridge->releaseOnClose("demand", "setDemand", QStringList());
        m_bridge->releaseOnClose("demand", "setWanted", false);

        auto other = std::make_unique<QTcpSocket>();
        other->connectToHost(QHostAddress::LocalHost, m_server->port());
        QVERIFY(other->waitForConnected(3000));
        auto callOn = [this](QTcpSocket &sock, const QByteArray &line) {
            sock.write(line + '\n');
            QElapsedTimer t;
            t.start();
            QByteArray in;
            while (!in.contains('\n') && t.elapsed() < 5000) {
                QCoreApplication::processEvents();
                if (sock.waitForReadyRead(10))
                    in += sock.readAll();
            }
            return in;
        };
        QVERIFY(callOn(*other, R"({"op": "call", "object": "demand", "method": "setDemand", "args": ["dash", ["RPM", "SPEED"]]})").contains("\"ok\":true"));
        QVERIFY(callOn(*other, R"({"op": "call", "object": "demand", "method": "setDemand", "args": ["gone", ["MAF"]]})").contains("\"ok\":true"));
        QVERIFY(callOn(*other, R"({"op": "call", "object": "demand", "method": "setWanted", "args": ["g", true]})").contains("\"ok\":true"));
        // Released by the client itself: nothing left to do on close
        QVERIFY(callOn(*other, R"({"op": "call", "object": "demand", "method": "setDemand", "args": ["gone", []]})").contains("\"ok\":true"));
        // A consumer this connection claimed and another one took over
        QVERIFY(callOn(*other, R"({"op": "call", "object": "demand", "method": "setDemand", "args": ["shared", ["A"]]})").contains("\"ok\":true"));
        QVERIFY(result(R"({"op": "call", "object": "demand", "method": "setDemand", "args": ["shared", ["B"]]})").isNull());
        // And one of m_socket's own
        QVERIFY(result(R"({"op": "call", "object": "demand", "method": "setDemand", "args": ["mine", ["RPM"]]})").isNull());
        QCOMPARE(demand.ids.value("dash"), (QStringList{"RPM", "SPEED"}));
        QVERIFY(demand.wanted.contains("g"));
        QCOMPARE(demand.releases, 1);

        other->abort();   // killed, no goodbye
        other.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!demand.ids.contains("dash"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!demand.wanted.contains("g"), 3000);
        QCOMPARE(demand.releases, 3);   // dash and g only
        QCOMPARE(demand.ids.value("shared"), QStringList{"B"});   // the other connection's now
        QCOMPARE(demand.ids.value("mine"), QStringList{"RPM"});
        // The surviving connection's claims go when it does
        m_socket->abort();
        QTRY_VERIFY_WITH_TIMEOUT(demand.ids.isEmpty(), 3000);
        QCOMPARE(demand.releases, 5);   // shared and mine
    }

    void keyValueObjects()
    {
        // paramStore is a QQmlPropertyMap: keys, not declared properties
        QQmlPropertyMap store;
        store.insert("RPM", 2500.0);
        store.insert("SPEED", 31.0);
        m_registry->add("store", &store);
        QCOMPARE(result(R"({"op": "get", "object": "store", "property": "RPM"})").toDouble(), 2500.0);
        QVERIFY(result(R"({"op": "get", "object": "store", "property": "BOOST"})").isNull());
        const QVariantMap all = result(R"({"op": "get", "object": "store"})").toMap();
        QCOMPARE(all.value("SPEED").toDouble(), 31.0);
        QVERIFY(error(R"({"op": "set", "object": "store", "property": "RPM", "value": 1})").contains("read-only"));
    }

    void subscribeToPropertiesAndSignals()
    {
        m_registry->add("fixture", &m_fixture);
        m_fixture.setLevel(1);
        const int sub = result(R"({"op": "subscribe", "object": "fixture", "properties": ["level"], "signals": ["pinged"]})")
                            .toMap().value("sub").toInt();
        QVERIFY(sub > 0);
        // The current value at once
        QTRY_COMPARE_WITH_TIMEOUT(collect().size(), 1, 2000);
        QCOMPARE(m_events.first().value("event").toString(), QStringLiteral("property"));
        QCOMPARE(m_events.first().value("value").toInt(), 1);
        QCOMPARE(m_events.first().value("sub").toInt(), sub);
        m_events.clear();

        m_fixture.setLevel(2);
        emit m_fixture.pinged("dash", 4);
        QTRY_COMPARE_WITH_TIMEOUT(collect().size(), 2, 2000);
        QCOMPARE(m_events.at(0).value("name").toString(), QStringLiteral("level"));
        QCOMPARE(m_events.at(0).value("value").toInt(), 2);
        QCOMPARE(m_events.at(1).value("event").toString(), QStringLiteral("signal"));
        QCOMPARE(m_events.at(1).value("args").toArray().at(0).toString(), QStringLiteral("dash"));
        QCOMPARE(m_events.at(1).value("args").toArray().at(1).toInt(), 4);
        QVERIFY(m_events.at(1).contains("t"));
        m_events.clear();

        QVERIFY(result(QStringLiteral(R"({"op": "unsubscribe", "sub": %1})").arg(sub).toUtf8()).toBool());
        m_fixture.setLevel(5);
        QCOMPARE(events(200).size(), 0);
        QVERIFY(error(R"({"op": "subscribe", "object": "fixture", "signals": ["nope"]})").contains("no signal"));
        QVERIFY(error(R"({"op": "unsubscribe", "sub": 999})").contains("No subscription"));
    }

    void subscribeReplyComesBeforeItsEvents()
    {
        // A client learns its sub id from the reply: no event of that
        // subscription may reach it first, even pipelined
        m_registry->add("fixture", &m_fixture);
        m_socket->write(R"({"op": "subscribe", "object": "fixture", "properties": ["level", "mode"], "id": 1})" "\n"
                        R"({"op": "subscribe", "object": "fixture", "properties": ["label"], "id": 2})" "\n");
        QList<QJsonObject> lines;
        QElapsedTimer t;
        t.start();
        while (lines.size() < 5 && t.elapsed() < 5000) {
            pump(10);
            lines += m_lines;
            m_lines.clear();
        }
        QCOMPARE(lines.size(), 5);   // two replies, three current values
        QHash<int, int> replyAt;     // sub id -> line index of its reply
        for (int i = 0; i < lines.size(); ++i) {
            const QJsonObject &l = lines.at(i);
            if (l.contains("ok")) {
                replyAt.insert(l.value("result").toObject().value("sub").toInt(), i);
            } else {
                const int sub = l.value("sub").toInt();
                QVERIFY2(replyAt.contains(sub), qPrintable(QStringLiteral("event of sub %1 before its reply").arg(sub)));
            }
        }
        QCOMPARE(replyAt.size(), 2);
    }

    void throttleKeepsTheLatest()
    {
        m_registry->add("fixture", &m_fixture);
        result(R"({"op": "subscribe", "object": "fixture", "properties": ["level"], "throttleMs": 150})");
        QTRY_COMPARE_WITH_TIMEOUT(collect().size(), 1, 2000);   // the current value is not held back
        m_events.clear();
        for (int v = 10; v < 15; ++v)
            m_fixture.setLevel(v);
        QCOMPARE(events(60).size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(collect().size(), 1, 2000);
        QCOMPARE(m_events.first().value("value").toInt(), 14);
    }

    void parameterBatches()
    {
        // paramStore: a QQmlPropertyMap with a changed(QStringList) batch signal
        Store store;
        store.insert("RPM", 800.0);
        m_registry->add("store", &store);
        result(R"({"op": "subscribe", "object": "store", "properties": ["RPM", "SPEED"]})");
        QTRY_COMPARE_WITH_TIMEOUT(collect().size(), 1, 2000);
        QCOMPARE(m_events.first().value("event").toString(), QStringLiteral("params"));
        QCOMPARE(m_events.first().value("values").toObject().value("RPM").toDouble(), 800.0);
        m_events.clear();
        store.insert("RPM", 2500.0);
        store.insert("SPEED", 30.0);
        store.insert("PITCH", 3.0);
        emit store.changed({"RPM", "SPEED", "PITCH"});
        QTRY_COMPARE_WITH_TIMEOUT(collect().size(), 1, 2000);
        const QJsonObject values = m_events.first().value("values").toObject();
        QCOMPARE(values.size(), 2);   // PITCH wasn't asked for
        QCOMPARE(values.value("SPEED").toDouble(), 30.0);
        m_registry->add("store", nullptr);
    }

    void subscriptionEndsWithItsObject()
    {
        auto *temp = new Fixture;
        m_registry->add("temp", temp);
        const int sub = result(R"({"op": "subscribe", "object": "temp", "properties": ["level"]})").toMap().value("sub").toInt();
        QTRY_COMPARE_WITH_TIMEOUT(collect().size(), 1, 2000);
        delete temp;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);   // the subscription's deleteLater
        QVERIFY(error(QStringLiteral(R"({"op": "unsubscribe", "sub": %1})").arg(sub).toUtf8()).contains("No subscription"));
    }

    void crossThreadSignalsArriveOnTheGuiThread()
    {
        m_registry->add("fixture", &m_fixture);
        result(R"({"op": "subscribe", "object": "fixture", "signals": ["pinged"]})");
        QThread *worker = QThread::create([this] { emit m_fixture.pinged("worker", 1); });
        worker->start();
        worker->wait();
        delete worker;
        QTRY_COMPARE_WITH_TIMEOUT(collect().size(), 1, 2000);
        QCOMPARE(m_events.first().value("args").toArray().at(0).toString(), QStringLiteral("worker"));
    }

    void pipelinedRequestsAllAnswer()
    {
        // Several requests in one write: none may wait for a later one
        m_registry->add("fixture", &m_fixture);
        m_socket->write(R"({"op": "get", "id": 1, "object": "fixture", "property": "level"})" "\n"
                        R"({"op": "call", "id": 2, "object": "fixture", "method": "greet", "args": ["a"]})" "\n"
                        R"({"op": "hello", "id": 3})" "\n");
        QSet<int> ids;
        QElapsedTimer t;
        t.start();
        while (ids.size() < 3 && t.elapsed() < 3000) {
            pump(10);
            for (int i = m_lines.size() - 1; i >= 0; --i)
                if (m_lines.at(i).contains("ok"))
                    ids.insert(m_lines.takeAt(i).value("id").toInt());
        }
        QCOMPARE(ids, (QSet<int>{1, 2, 3}));
    }

    void listModels()
    {
        m_registry->add("fixture", &m_fixture);
        m_fixture.m_queue->reset({{{"songId", "a"}, {"status", "complete"}}});
        const QVariantMap summary = result(R"({"op": "get", "object": "fixture", "property": "queue"})").toMap();
        QCOMPARE(summary.value("model").toBool(), true);
        QCOMPARE(summary.value("count").toInt(), 1);
        QCOMPARE(summary.value("roles").toStringList(), (QStringList{"songId", "status"}));
        const QVariantList rows = result(R"({"op": "rows", "object": "fixture", "property": "queue"})").toList();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value("status").toString(), QStringLiteral("complete"));
        QVERIFY(error(R"({"op": "rows", "object": "fixture", "property": "level"})").contains("not a list model"));

        result(R"({"op": "subscribe", "object": "fixture", "properties": ["queue"], "throttleMs": 500})");
        QTRY_COMPARE_WITH_TIMEOUT(collect().size(), 1, 2000);
        QCOMPARE(m_events.first().value("change").toString(), QStringLiteral("reset"));
        QCOMPARE(m_events.first().value("rows").toArray().size(), 1);
        m_events.clear();
        m_fixture.m_queue->append({{"songId", "b"}, {"status", "downloading"}});
        m_fixture.m_queue->setValue(1, "status", "complete");
        m_fixture.m_queue->remove(0);
        // In order, none coalesced despite the throttle
        QTRY_COMPARE_WITH_TIMEOUT(collect().size(), 3, 2000);
        QCOMPARE(m_events.at(0).value("change").toString(), QStringLiteral("insert"));
        QCOMPARE(m_events.at(0).value("rows").toArray().at(0).toObject().value("songId").toString(), QStringLiteral("b"));
        QCOMPARE(m_events.at(1).value("change").toString(), QStringLiteral("update"));
        QCOMPARE(m_events.at(1).value("first").toInt(), 1);
        QCOMPARE(m_events.at(2).value("change").toString(), QStringLiteral("remove"));
        QCOMPARE(m_events.at(2).value("first").toInt(), 0);
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
    QVariant result(const QByteArray &line)
    {
        const QJsonObject r = send(line);
        if (!r.value("ok").toBool())
            qWarning() << line << r;
        return r.value("result").toVariant();
    }
    QString error(const QByteArray &line) { return send(line).value("error").toString(); }
    const QList<QJsonObject> &collect()
    {
        m_events += events();
        return m_events;
    }
    QList<QJsonObject> m_events;

    // Replies and events share the line stream: a reply has "ok"
    QJsonObject send(const QByteArray &line)
    {
        m_socket->write(line + '\n');
        m_socket->waitForBytesWritten(3000);
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 5000) {
            for (int i = 0; i < m_lines.size(); ++i) {
                if (m_lines.at(i).contains("ok"))
                    return m_lines.takeAt(i);
            }
            pump(10);
        }
        return {};
    }

    // Events received so far (and reads more for up to `waitMs`)
    QList<QJsonObject> events(int waitMs = 0)
    {
        QElapsedTimer t;
        t.start();
        do
            pump(10);
        while (t.elapsed() < waitMs);
        QList<QJsonObject> out;
        for (int i = m_lines.size() - 1; i >= 0; --i)
            if (m_lines.at(i).contains("event"))
                out.prepend(m_lines.takeAt(i));
        return out;
    }

    // get / set / call / subscribe hop to this (the GUI) thread: keep it turning
    void pump(int ms)
    {
        QCoreApplication::processEvents();
        // An event loop turn (QTRY's qWait) may already have buffered the data,
        // which waitForReadyRead() doesn't count as new
        if (m_socket->bytesAvailable() > 0 || m_socket->waitForReadyRead(ms))
            m_buf += m_socket->readAll();
        int nl;
        while ((nl = m_buf.indexOf('\n')) >= 0) {
            m_lines.append(QJsonDocument::fromJson(m_buf.left(nl)).object());
            m_buf.remove(0, nl + 1);
        }
    }

    QByteArray m_buf;
    QList<QJsonObject> m_lines;
    QObject m_first, m_second;
    Fixture m_fixture;
    std::unique_ptr<ServiceRegistry> m_registry;
    std::unique_ptr<CommandServer> m_server;
    std::unique_ptr<ServiceBridge> m_bridge;
    std::unique_ptr<QTcpSocket> m_socket;
};

QTEST_GUILESS_MAIN(TstServiceBridge)
#include "tst_servicebridge.moc"
