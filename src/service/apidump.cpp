#include "apidump.h"

#include "commandserver.h"
#include "metadescribe.h"
#include "protocol.h"
#include "serviceregistry.h"

#include <QJsonDocument>
#include <QSaveFile>

namespace OctaveService {

namespace {
bool writeJson(const QDir &dir, const QString &name, const QVariant &value, QString *error)
{
    QSaveFile file(dir.filePath(name));
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("%1: %2").arg(file.fileName(), file.errorString());
        return false;
    }
    file.write(QJsonDocument::fromVariant(jsonSafe(value)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = QStringLiteral("%1: %2").arg(file.fileName(), file.errorString());
        return false;
    }
    return true;
}
} // namespace

bool writeApiDocs(const ServiceRegistry &registry, const QDir &dir, QString *error)
{
    QString ignored;
    if (!error)
        error = &ignored;
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        *error = QStringLiteral("cannot create %1").arg(dir.absolutePath());
        return false;
    }

    QVariantMap classes;
    for (const QString &name : registry.names()) {
        const QObject *object = registry.object(name);
        if (!object)
            continue;
        if (const QStringList bad = annotationErrors(object->metaObject()); !bad.isEmpty()) {
            *error = bad.join(QLatin1Char('\n'));
            return false;
        }
        QVariantMap described = describeObject(object);
        described.insert(QStringLiteral("object"), name);
        if (!writeJson(dir, name + QStringLiteral(".json"), described, error))
            return false;
        classes.insert(name, described.value(QStringLiteral("class")));
    }

    if (const QObject *catalog = registry.object(QStringLiteral("parameterCatalog"))) {
        if (!writeJson(dir, QStringLiteral("parameters.json"), catalog->property("parameters"), error))
            return false;
    }
    // The dashboard widget types, next to dashboard-spec.schema.json
    if (const QObject *dashboards = registry.object(QStringLiteral("dashboardManager"))) {
        if (!writeJson(dir, QStringLiteral("widget-catalog.json"), dashboards->property("widgetCatalog"), error))
            return false;
    }

    return writeJson(dir, QStringLiteral("index.json"),
                     QVariantMap{{QStringLiteral("protocolVersion"), kProtocolVersion},
                                 {QStringLiteral("objects"), classes},
                                 {QStringLiteral("generatedBy"),
                                  QStringLiteral("octave --dump-api (src/service/apidump.cpp); do not edit")}},
                     error);
}

} // namespace OctaveService
