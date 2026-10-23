// The QML UI uses nothing a second frontend can't (TODO/open-backend.md,
// contract step 7). The bundled UI stays in-process for latency, so this
// keeps it honest from the source:
//
//  - every object C++ hands QML (setContextProperty) is in the service
//    registry (AppCore::expose, published in docs/api/), or is on the short
//    list below of things that only make sense inside the QML engine;
//  - every `object.member` the QML writes, and every `function onSignal()`
//    in a `Connections { target: object }`, names a property, method or
//    signal docs/api/ publishes for that object (tst_apidocs keeps docs/api
//    equal to the build, so this is the bridge's own view).
//
// A failure means QML reaches something the service doesn't serve (a Qt
// base-class member, a context property set around expose()) or a member
// that no longer exists.

#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTest>

namespace {
const QString kRoot = QStringLiteral(OCTAVE_SOURCE_DIR);

// Context properties that are not service objects, and why
const QHash<QString, QString> kQmlOnly = {
    {"screenAutoScale", "the window's scale factor"},
    {"isAndroid", "a build constant"},
    {"hasQuick3D", "whether this QML engine has the module"},
    {"hasQuick3DAssetUtils", "whether this QML engine has the module"},
    {"wikiViewerAvailable", "whether this build embeds a web view"},
    {"wikiHomeUrl", "where the embedded web view finds the wiki"},
    {"assetStore", "file:// URLs for QML's own asset loaders"},
    {"devMode", "dev tooling (--dev), not in release builds"},
    {"devKeyboardController", "dev tooling (--dev), not in release builds"},
    {"devToolsManager", "dev tooling (--dev), not in release builds"},
    {"perfMonitor", "dev tooling (--profile), not in release builds"},
};

QString read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QStringList files(const QString &dir, const QStringList &patterns)
{
    QStringList out;
    QDirIterator it(kRoot + '/' + dir, patterns, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << it.next();
    out.sort();
    return out;
}

// Line comments and block comments out, line breaks kept (for line numbers)
QString stripComments(QString s)
{
    static const QRegularExpression block(QStringLiteral(R"(/\*.*?\*/)"),
                                          QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = block.globalMatch(s);
    QList<QRegularExpressionMatch> matches;
    while (it.hasNext())
        matches.prepend(it.next());
    for (const auto &m : matches)
        s.replace(m.capturedStart(), m.capturedLength(), QString(m.captured().count('\n'), '\n'));
    static const QRegularExpression line(QStringLiteral(R"((?<!:)//[^\n]*)"));   // not https://
    s.replace(line, QString());
    return s;
}
} // namespace

class TstConformance : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        const QJsonObject index = QJsonDocument::fromJson(read(kRoot + "/docs/api/index.json").toUtf8()).object();
        QVERIFY2(!index.isEmpty(), "docs/api/index.json missing");
        for (const QString &name : index.value("objects").toObject().keys()) {
            const QJsonObject obj = QJsonDocument::fromJson(read(kRoot + "/docs/api/" + name + ".json").toUtf8()).object();
            QSet<QString> members, signalNames;
            for (const char *kind : {"properties", "methods", "signals"})
                for (const QJsonValue &m : obj.value(kind).toArray())
                    members.insert(m.toObject().value("name").toString());
            for (const QJsonValue &m : obj.value("signals").toArray())
                signalNames.insert(m.toObject().value("name").toString());
            m_members.insert(name, members);
            m_signals.insert(name, signalNames);
        }
        QVERIFY(m_members.size() > 10);
        m_qml = files("frontend", {"*.qml", "*.js"}) + files("src/dev/qml", {"*.qml"});
        QVERIFY(m_qml.size() > 50);
    }

    void contextPropertiesAreServiceObjects()
    {
        static const QRegularExpression set(QStringLiteral(R"re(setContextProperty\(\s*(?:QStringLiteral\(\s*)?"(\w+)")re"));
        QStringList offenders;
        for (const QString &file : files("src", {"*.cpp"})) {
            const QString text = read(file);
            auto it = set.globalMatch(text);
            while (it.hasNext()) {
                const QString name = it.next().captured(1);
                if (!m_members.contains(name) && !kQmlOnly.contains(name))
                    offenders << QStringLiteral("%1 (%2)").arg(name, QDir(kRoot).relativeFilePath(file));
            }
        }
        QVERIFY2(offenders.isEmpty(),
                 qPrintable("set as QML context properties but not service objects (use AppCore::expose, "
                            "or list a QML-only value in tst_conformance): " + offenders.join(", ")));
    }

    void qmlUsesOnlyPublishedMembers()
    {
        QStringList names = m_members.keys();
        // paramStore's members are parameter ids (docs/api/parameters.json)
        names.removeAll("paramStore");
        const QRegularExpression use(QStringLiteral(R"((?<![\w.])(%1)\s*\.\s*(\w+))").arg(names.join('|')));
        QStringList offenders;
        int uses = 0;
        for (const QString &file : m_qml) {
            const QString text = stripComments(read(file));
            auto it = use.globalMatch(text);
            while (it.hasNext()) {
                const auto m = it.next();
                ++uses;
                if (!m_members.value(m.captured(1)).contains(m.captured(2)))
                    offenders << QStringLiteral("%1:%2 %3.%4").arg(QDir(kRoot).relativeFilePath(file))
                                     .arg(text.left(m.capturedStart()).count('\n') + 1)
                                     .arg(m.captured(1), m.captured(2));
            }
        }
        QVERIFY2(uses > 500, qPrintable(QString::number(uses)));   // the scan still finds the UI's calls
        QVERIFY2(offenders.isEmpty(), qPrintable("QML uses members the service doesn't publish: " + offenders.join("; ")));
    }

    void connectionsHandleRealSignals()
    {
        static const QRegularExpression open(QStringLiteral(R"(\bConnections\s*\{)"));
        // The target line, bare or a guard ("typeof x !== 'undefined' ? x : null"):
        // its first service object, if it names one
        static const QRegularExpression target(QStringLiteral(R"(\btarget\s*:([^\n]*))"));
        QStringList names = m_signals.keys();
        names.removeAll("paramStore");   // the value store
        const QRegularExpression service(QStringLiteral(R"(\b(%1)\b)").arg(names.join('|')));
        static const QRegularExpression handler(QStringLiteral(R"(\bfunction\s+on([A-Z]\w*)\s*\()"));
        QStringList offenders;
        int checked = 0;
        for (const QString &file : m_qml) {
            const QString text = stripComments(read(file));
            auto it = open.globalMatch(text);
            while (it.hasNext()) {
                const auto m = it.next();
                // The block: braces from the opening one to its match
                int depth = 0, end = m.capturedEnd() - 1;
                for (; end < text.size(); ++end) {
                    if (text.at(end) == '{')
                        ++depth;
                    else if (text.at(end) == '}' && --depth == 0)
                        break;
                }
                const QString block = text.mid(m.capturedStart(), end - m.capturedStart() + 1);
                const QString obj = service.match(target.match(block).captured(1)).captured(1);
                if (obj.isEmpty())
                    continue;   // a QML item
                auto h = handler.globalMatch(block);
                while (h.hasNext()) {
                    QString signal = h.next().captured(1);
                    signal[0] = signal.at(0).toLower();
                    ++checked;
                    if (!m_signals.value(obj).contains(signal))
                        offenders << QStringLiteral("%1:%2 %3.%4").arg(QDir(kRoot).relativeFilePath(file))
                                         .arg(text.left(m.capturedStart()).count('\n') + 1).arg(obj, signal);
                }
            }
        }
        QVERIFY2(checked > 150, qPrintable(QString::number(checked)));
        QVERIFY2(offenders.isEmpty(), qPrintable("Connections handlers for signals the service doesn't publish: "
                                                 + offenders.join("; ")));
    }

private:
    QHash<QString, QSet<QString>> m_members;
    QHash<QString, QSet<QString>> m_signals;
    QStringList m_qml;
};

QTEST_GUILESS_MAIN(TstConformance)
#include "tst_conformance.moc"
