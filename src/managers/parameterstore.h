#ifndef PARAMETERSTORE_H
#define PARAMETERSTORE_H

#include <QHash>
#include <QQmlPropertyMap>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

/**
 * @brief The latest value of every OBD PID and IMU reading, one notifying
 *        key per ParameterCatalog id.
 *
 * QML binds `paramStore.RPM` or `paramStore[id]`: a binding re-evaluates
 * only when its own id changes, and an id with no reading yet is undefined.
 * main.cpp feeds it from OBDManager::parameterSampled and the eight BerryIMU
 * signals; nothing else writes it (QML writes are rejected).
 *
 * Writes are coalesced: ingest() is allocation-light and the values land
 * together every kFlushMs (the IMU alone sends ~440 values a second; one
 * fold per frame-ish is plenty for needles). After each fold `changed(ids)`
 * lists what moved, for consumers that want the batch.
 *
 * An alias in the catalog (IGNITION_TIMING) is written whenever its target
 * (TIMING_ADVANCE) arrives.
 *
 * Registered in main.cpp as QML context property "paramStore".
 */
class ParameterStore : public QQmlPropertyMap
{
    Q_OBJECT
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "read")
    Q_CLASSINFO("octave.scope.full", "ingest")

public:
    static constexpr int kFlushMs = 33;

    explicit ParameterStore(QObject *parent = nullptr);

    // Current value of `id`, or an invalid QVariant if it has none yet
    Q_INVOKABLE QVariant get(const QString &id) const;
    // Every id that has a value -> its value
    Q_INVOKABLE QVariantMap snapshot() const;

signals:
    void changed(const QStringList &ids);

public slots:
    void ingest(const QString &id, float value);

protected:
    QVariant updateValue(const QString &key, const QVariant &input) override;

private:
    void flush();

    QHash<QString, float> m_pending;
    QHash<QString, QStringList> m_aliases;   // target id -> alias ids
    QTimer m_flushTimer;
};

#endif // PARAMETERSTORE_H
