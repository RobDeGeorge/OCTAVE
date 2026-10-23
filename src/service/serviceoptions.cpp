#include "serviceoptions.h"

#include <QDir>
#include <QFileInfo>

namespace OctaveService {

namespace {
// A port argument: 1-65535, or 0 for any free port
bool parsePort(const QString &text, quint16 &port)
{
    bool ok = false;
    const uint value = text.toUInt(&ok);
    if (!ok || value > 65535)
        return false;
    port = quint16(value);
    return true;
}
} // namespace

ServiceOptions parseServiceOptions(const QStringList &arguments, bool settingEnabled, bool devToolsBuild,
                                   const QString &settingWebRoot)
{
    ServiceOptions o;
    QString webRoot = settingWebRoot;
    bool asked = settingEnabled;
    auto badPort = [&o](const QString &text) {
        o.error = QStringLiteral("not a port: %1").arg(text);
    };

    for (int i = 1; i < arguments.size(); ++i) {
        const QString &a = arguments.at(i);
        if (a == QLatin1String("--service")) {
            asked = true;
            // An optional port: the next argument, when it is a number
            quint16 port = 0;
            if (i + 1 < arguments.size() && parsePort(arguments.at(i + 1), port)) {
                o.port = port;
                ++i;
            }
        } else if (a.startsWith(QLatin1String("--service="))) {
            asked = true;
            const QString text = a.section(QLatin1Char('='), 1);
            if (!parsePort(text, o.port))
                badPort(text);
        } else if (a == QLatin1String("--command-port") || a.startsWith(QLatin1String("--command-port="))) {
            const QString text = a.contains(QLatin1Char('='))
                ? a.section(QLatin1Char('='), 1)
                : (i + 1 < arguments.size() ? arguments.at(++i) : QString());
            if (!parsePort(text, o.port))
                badPort(text);
        } else if (a == QLatin1String("--service-bind") || a.startsWith(QLatin1String("--service-bind="))) {
            const QString text = a.contains(QLatin1Char('='))
                ? a.section(QLatin1Char('='), 1)
                : (i + 1 < arguments.size() ? arguments.at(++i) : QString());
            if (!o.bind.setAddress(text))
                o.error = QStringLiteral("not an address: %1").arg(text);
        } else if (a == QLatin1String("--web-root") || a.startsWith(QLatin1String("--web-root="))) {
            webRoot = a.contains(QLatin1Char('='))
                ? a.section(QLatin1Char('='), 1)
                : (i + 1 < arguments.size() ? arguments.at(++i) : QString());
            if (webRoot.isEmpty())
                o.error = QStringLiteral("--web-root needs a directory");
        } else if (a == QLatin1String("--headless")) {
            asked = true;
        } else if (a == QLatin1String("--profile") && devToolsBuild) {
            asked = true;
        }
    }

    if (!webRoot.isEmpty())
        o.webRoot = QDir::cleanPath(QDir::current().absoluteFilePath(webRoot));
    o.enabled = asked && o.error.isEmpty();
    return o;
}

QString webRootProblem(const QString &dir)
{
    const QFileInfo info(dir);
    if (!info.isDir())
        return QStringLiteral("%1 is not a directory").arg(dir);
    if (!QFileInfo(QDir(dir).filePath(QStringLiteral("index.html"))).isFile())
        return QStringLiteral("%1 has no index.html").arg(dir);
    return {};
}

} // namespace OctaveService
