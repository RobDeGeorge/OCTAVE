#include "parameterstore.h"
#include "parametercatalog.h"

ParameterStore::ParameterStore(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    for (const ParameterCatalog::Parameter &p : ParameterCatalog::all()) {
        if (!p.aliasOf.isEmpty())
            m_aliases[p.aliasOf].append(p.id);
    }
    m_flushTimer.setSingleShot(true);   // re-armed by the next write, idle otherwise
    m_flushTimer.setInterval(kFlushMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &ParameterStore::flush);
}

void ParameterStore::ingest(const QString &id, float value)
{
    m_pending.insert(id, value);
    const auto alias = m_aliases.constFind(id);
    if (alias != m_aliases.constEnd()) {
        for (const QString &a : alias.value())
            m_pending.insert(a, value);
    }
    if (!m_flushTimer.isActive())
        m_flushTimer.start();
}

void ParameterStore::flush()
{
    if (m_pending.isEmpty())
        return;
    QStringList ids;
    ids.reserve(m_pending.size());
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        insert(it.key(), static_cast<double>(it.value()));   // notifies that key's bindings
        ids.append(it.key());
    }
    m_pending.clear();
    emit changed(ids);
}

QVariant ParameterStore::get(const QString &id) const
{
    return contains(id) ? value(id) : QVariant();   // invalid for a key QML only read
}

QVariantMap ParameterStore::snapshot() const
{
    // keys() also has ids QML read before their first value (QQmlPropertyMap
    // creates a key on first access, invalid): leave those out
    QVariantMap map;
    const QStringList ids = keys();
    for (const QString &id : ids) {
        const QVariant v = value(id);
        if (v.isValid())
            map.insert(id, v);
    }
    return map;
}

QVariant ParameterStore::updateValue(const QString &key, const QVariant &input)
{
    Q_UNUSED(input)
    // Read-only for QML: the vehicle and the IMU are the only writers
    return value(key);
}
