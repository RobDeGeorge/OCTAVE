#include "frontendmanifest.h"

#include "../service/servicescopes.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVariantMap>

QString FrontendManifest::currentPlatform()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS) || defined(OCTAVE_WASM)
    return {};
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

bool FrontendManifest::safeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.size() > 512 || path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char(':'))
        || path.startsWith(QLatin1Char('/')) || path.contains(QChar(0)))
        return false;
    for (const QString &part : path.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    }
    return true;
}

bool FrontendManifest::validId(const QString &id)
{
    static const QRegularExpression idShape(QStringLiteral("^[a-z0-9][a-z0-9._-]{1,63}$"));
    return idShape.match(id).hasMatch() && !id.contains(QLatin1String(".."));
}

FrontendManifest FrontendManifest::parse(const QByteArray &json, QStringList *errors)
{
    FrontendManifest m;
    QStringList errs;
    auto fail = [&]() {
        if (errors)
            *errors += errs;
        return FrontendManifest();
    };
    if (json.size() > kMaxBytes) {
        errs << QStringLiteral("the manifest is over %1 KB").arg(kMaxBytes / 1024);
        return fail();
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject()) {
        errs << QStringLiteral("not a JSON object: %1").arg(pe.errorString());
        return fail();
    }
    const QJsonObject o = doc.object();
    auto text = [&](const char *key, int max, bool required) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined() && !required)
            return QString();
        if (!v.isString() || v.toString().trimmed().isEmpty() || v.toString().size() > max) {
            errs << QStringLiteral("\"%1\" must be a string of 1 to %2 characters").arg(QLatin1String(key)).arg(max);
            return QString();
        }
        return v.toString().trimmed();
    };

    m.id = text("id", 64, true);
    if (!m.id.isEmpty() && !validId(m.id))
        errs << QStringLiteral("\"id\" must be lower case letters, digits, '.', '_' or '-' (2 to 64), starting with a letter or digit");
    m.name = text("name", 64, true);
    m.version = text("version", 32, true);
    m.description = text("description", 500, false);

    const QJsonArray proto = o.value(QLatin1String("protocol")).toArray();
    if (proto.size() != 2 || !proto.at(0).isDouble() || !proto.at(1).isDouble()
        || proto.at(0).toInt() < 1 || proto.at(1).toInt() < proto.at(0).toInt()) {
        errs << QStringLiteral("\"protocol\" must be [min, max], the protocol versions it speaks (min >= 1)");
    } else {
        m.protocolMin = proto.at(0).toInt();
        m.protocolMax = proto.at(1).toInt();
    }

    const QJsonValue scopes = o.value(QLatin1String("scopes"));
    if (scopes.isUndefined()) {
        m.scopes = {QStringLiteral("read")};
    } else if (!scopes.isArray()) {
        errs << QStringLiteral("\"scopes\" must be a list");
    } else {
        for (const QJsonValue &s : scopes.toArray()) {
            const QString name = s.toString();
            if (!OctaveService::knownScopes().contains(name))
                errs << QStringLiteral("unknown scope \"%1\" (known: %2)")
                            .arg(name, OctaveService::knownScopes().join(QStringLiteral(", ")));
            else if (!m.scopes.contains(name))
                m.scopes << name;
        }
        if (m.scopes.isEmpty() && errs.isEmpty())
            m.scopes = {QStringLiteral("read")};
    }

    const bool hasWeb = o.contains(QLatin1String("web"));
    const bool hasExec = o.contains(QLatin1String("exec"));
    if (hasWeb == hasExec)
        errs << QStringLiteral("exactly one of \"web\" and \"exec\"");
    if (hasWeb) {
        const QString root = o.value(QLatin1String("web")).toObject().value(QLatin1String("root")).toString();
        if (root == QLatin1String("."))
            m.webRoot = root;
        else if (!safeRelativePath(root))
            errs << QStringLiteral("\"web.root\" must be a folder inside the frontend (\".\" for its top)");
        else
            m.webRoot = root;
    }
    if (hasExec) {
        const QJsonObject exec = o.value(QLatin1String("exec")).toObject();
        static const QStringList platforms{QStringLiteral("linux"), QStringLiteral("windows"), QStringLiteral("macos")};
        for (auto it = exec.begin(); it != exec.end(); ++it) {
            if (!platforms.contains(it.key()))
                continue;   // a platform this version doesn't know
            QStringList argv;
            for (const QJsonValue &a : it.value().toArray()) {
                if (!a.isString() || a.toString().size() > 1024) {
                    argv.clear();
                    break;
                }
                argv << a.toString();
            }
            const QString program = argv.value(0);
            const bool bare = !program.isEmpty() && !program.contains(QLatin1Char('/')) && !program.contains(QLatin1Char('\\'));
            if (argv.isEmpty() || argv.size() > 64 || (!bare && !safeRelativePath(program)))
                errs << QStringLiteral("\"exec.%1\" must be [program, args...]: a path inside the frontend or a bare program name")
                            .arg(it.key());
            else
                m.exec.insert(it.key(), argv);
        }
        if (m.exec.isEmpty() && errs.isEmpty())
            errs << QStringLiteral("\"exec\" names no platform (linux, windows, macos)");
    }

    if (!errs.isEmpty())
        return fail();
    return m;
}

bool FrontendManifest::validateFiles(const FrontendManifest &m, const QDir &dir, QStringList *errors)
{
    QStringList errs;
    if (m.isWeb()) {
        const QFileInfo index(dir.filePath(m.webRoot + QStringLiteral("/index.html")));
        if (!index.isFile())
            errs << QStringLiteral("\"web.root\" (%1) has no index.html").arg(m.webRoot);
    }
    for (auto it = m.exec.begin(); it != m.exec.end(); ++it) {
        const QString program = it.value().first();
        if (program.contains(QLatin1Char('/')) && !QFileInfo(dir.filePath(program)).isFile())
            errs << QStringLiteral("\"exec.%1\": %2 is not in the frontend").arg(it.key(), program);
    }
    if (errors)
        *errors += errs;
    return errs.isEmpty();
}

QVariantMap FrontendManifest::toMap() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("version"), version},
            {QStringLiteral("description"), description},
            {QStringLiteral("kind"), isWeb() ? QStringLiteral("web") : QStringLiteral("exec")},
            {QStringLiteral("protocol"), QVariantList{protocolMin, protocolMax}},
            {QStringLiteral("scopes"), scopes},
            {QStringLiteral("command"), command()}};
}
