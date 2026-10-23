// serviceregistry.cpp

#include "serviceregistry.h"

namespace OctaveService {

void ServiceRegistry::add(const QString &name, QObject *object)
{
    QWriteLocker l(&m_lock);
    for (auto &entry : m_objects) {
        if (entry.first == name) {
            entry.second = object;
            return;
        }
    }
    m_objects.emplace_back(name, object);
}

QObject *ServiceRegistry::object(const QString &name) const
{
    QReadLocker l(&m_lock);
    for (const auto &entry : m_objects)
        if (entry.first == name)
            return entry.second.data();
    return nullptr;
}

QStringList ServiceRegistry::names() const
{
    QReadLocker l(&m_lock);
    QStringList out;
    for (const auto &entry : m_objects)
        out.append(entry.first);
    return out;
}

} // namespace OctaveService
