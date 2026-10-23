// serviceregistry.h
//
// The objects the service bridge serves, by the name QML knows them by
// (main.cpp's expose() puts each manager in both places, so the QML UI and
// any other frontend see the same set). Insertion ordered; GUI thread
// objects, read from the bridge's connection threads only through
// CommandServer::onGui.

#ifndef OCTAVE_SERVICE_SERVICEREGISTRY_H
#define OCTAVE_SERVICE_SERVICEREGISTRY_H

#include <QObject>
#include <QPointer>
#include <QReadWriteLock>
#include <QStringList>

#include <utility>
#include <vector>

namespace OctaveService {

class ServiceRegistry
{
public:
    void add(const QString &name, QObject *object);
    QObject *object(const QString &name) const;   // nullptr if unknown (or gone)
    QStringList names() const;

private:
    mutable QReadWriteLock m_lock;
    std::vector<std::pair<QString, QPointer<QObject>>> m_objects;
};

} // namespace OctaveService

#endif // OCTAVE_SERVICE_SERVICEREGISTRY_H
