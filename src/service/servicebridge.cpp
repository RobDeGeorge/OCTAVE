// servicebridge.cpp

#include "servicebridge.h"

#include "commandserver.h"
#include "metadescribe.h"
#include "protocol.h"
#include "serviceregistry.h"

namespace OctaveService {

ServiceBridge::ServiceBridge(ServiceRegistry *registry, AppInfo app)
    : m_registry(registry)
    , m_app(std::move(app))
{
}

QObject *ServiceBridge::object(const QString &name) const
{
    QObject *o = m_registry->object(name);
    if (!o)
        throw CommandError(QStringLiteral("Unknown object: %1").arg(name));
    return o;
}

void ServiceBridge::install(CommandServer &server)
{
    server.registerOp(QStringLiteral("hello"), [this](const QVariantMap &) {
        return QVariant(QVariantMap{
            {QStringLiteral("protocolVersion"), kProtocolVersion},
            {QStringLiteral("app"), QVariantMap{{QStringLiteral("version"), m_app.version},
                                                {QStringLiteral("build"), m_app.debugBuild ? QStringLiteral("debug")
                                                                                           : QStringLiteral("release")},
                                                {QStringLiteral("headless"), m_app.headless}}},
            {QStringLiteral("objects"), m_registry->names()},
        });
    });

    // Meta-objects are static data: no GUI-thread hop needed
    server.registerOp(QStringLiteral("describe"), [this](const QVariantMap &args) {
        const QString name = args.value(QStringLiteral("object")).toString();
        if (!name.isEmpty())
            return QVariant(describeObject(object(name)));
        QVariantMap all;
        for (const QString &n : m_registry->names())
            all.insert(n, describeObject(m_registry->object(n)));
        return QVariant(QVariantMap{{QStringLiteral("protocolVersion"), kProtocolVersion},
                                    {QStringLiteral("objects"), all}});
    });
}

} // namespace OctaveService
