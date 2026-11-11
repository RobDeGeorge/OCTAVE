#include "webfrontends.h"

#include "../../managers/frontendmanager.h"
#include "../../managers/frontendmanifest.h"
#include "../../managers/volumecontroller.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <emscripten/val.h>

#include <string>

Q_LOGGING_CATEGORY(lcWebFrontends, "octave.webfrontends")

namespace {

// globalThis.octaveFrontendCatalog as JSON text ("[]" when the page set none)
QByteArray catalogText()
{
    using emscripten::val;
    const val catalog = val::global("octaveFrontendCatalog");
    if (!val::global("Array").call<bool>("isArray", catalog))
        return QByteArrayLiteral("[]");
    return QByteArray::fromStdString(val::global("JSON").call<std::string>("stringify", catalog));
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}

} // namespace

void WebFrontends::install(FrontendManager *frontends, VolumeController *volume)
{
    if (!frontends || frontends->root().isEmpty())
        return;
    const QDir root(frontends->root());
    int count = 0;
    for (const QJsonValue &v : QJsonDocument::fromJson(catalogText()).array()) {
        const QJsonObject entry = v.toObject();
        const QString id = entry.value(QLatin1String("id")).toString();
        if (!FrontendManifest::validId(id))
            continue;
        // Its manifest as the site has it, served from the site, not from here
        QJsonObject manifest;
        for (const char *key : {"id", "name", "version", "description", "protocol", "scopes"})
            if (entry.contains(QLatin1String(key)))
                manifest.insert(QLatin1String(key), entry.value(QLatin1String(key)));
        manifest.insert(QStringLiteral("web"), QJsonObject{{QStringLiteral("root"), QStringLiteral(".")}});
        const QString dir = root.filePath(id);
        QDir().mkpath(dir);
        if (!writeFile(QDir(dir).filePath(QLatin1String(FrontendManifest::kFileName)),
                       QJsonDocument(manifest).toJson())
            || !writeFile(QDir(dir).filePath(QStringLiteral("index.html")),
                          QByteArrayLiteral("<!-- served by the site: ")
                              + entry.value(QLatin1String("path")).toString().toUtf8() + " -->\n")) {
            qCWarning(lcWebFrontends) << "could not list" << id;
            continue;
        }
        ++count;
    }
    if (count)
        qCInfo(lcWebFrontends) << count << "web frontends from the site";

    // What the page shows is what is chosen: no restart in a page
    QObject::connect(frontends, &FrontendManager::activeChanged, frontends,
                     [frontends] { frontends->setRunningId(frontends->activeId()); });
    // The escape, as on a head unit: mute five times, this screen again
    if (volume)
        QObject::connect(volume, &VolumeController::escapeGesture, frontends, [frontends] {
            if (!frontends->activeId().isEmpty())
                frontends->setActive(QString());
        });
}
