// servicebridge.h
//
// The service protocol over CommandServer's socket: the ops any frontend
// uses to read, write, call and follow the managers in a ServiceRegistry
// (TODO/open-backend.md, contract step 1). Ops so far:
//
//   hello -> {protocolVersion, app: {version, build, headless}, objects}
//   describe {object?} -> one object's properties / methods / signals
//       (metadescribe.h), or every object's as {protocolVersion, objects}

#ifndef OCTAVE_SERVICE_SERVICEBRIDGE_H
#define OCTAVE_SERVICE_SERVICEBRIDGE_H

#include <QString>

class QObject;

namespace OctaveService {

class CommandServer;
class ServiceRegistry;

struct AppInfo {
    QString version;      // PROJECT_VERSION
    bool debugBuild = false;
    bool headless = false;
};

class ServiceBridge
{
public:
    ServiceBridge(ServiceRegistry *registry, AppInfo app);

    // Register the ops on `server` (before or after it starts)
    void install(CommandServer &server);

private:
    QObject *object(const QString &name) const;   // throws CommandError if unknown

    ServiceRegistry *m_registry;
    AppInfo m_app;
};

} // namespace OctaveService

#endif // OCTAVE_SERVICE_SERVICEBRIDGE_H
