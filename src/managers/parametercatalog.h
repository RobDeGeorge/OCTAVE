#ifndef PARAMETERCATALOG_H
#define PARAMETERCATALOG_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

/**
 * @brief Display metadata for every value a gauge can show: the OBD PIDs
 *        and the BerryIMU readings.
 *
 * One table, read by C++ (the poller, the black box) and by every frontend
 * (QML through OBDParameterModel, others through the bridge). The ids are the
 * python-obd command names OBDManager emits on parameterSampled and that the
 * settings and dashboard files store; the decoders stay in ELM327Protocol,
 * and tst_parametercatalog keeps the two id sets equal.
 *
 * Registered in main.cpp as QML context property "parameterCatalog".
 */
class ParameterCatalog : public QObject
{
    Q_OBJECT
    // Every parameter as {id, title, shortTitle, unit, min, max, kind,
    // decimals, source, aliasOf}, in display order
    Q_PROPERTY(QVariantList parameters READ parameters CONSTANT)

public:
    enum class Source { Obd, Imu };

    struct Parameter {
        QString id;          // "COOLANT_TEMP"
        QString title;       // "Coolant Temp": what users see
        QString shortTitle;  // "CLT", or empty (use title)
        QString unit;        // "°C", "" for counts
        double min = 0;      // the usual range, for gauge scales
        double max = 100;
        // percentage | temperature | voltage | pressure | bidirectional |
        // numeric: which widgets can show it (WidgetCatalog supportedKinds)
        QString kind;
        int decimals = 1;    // for plain-text readouts
        Source source = Source::Obd;
        // Not polled itself: carries the value of another id
        // (IGNITION_TIMING is TIMING_ADVANCE under its old name)
        QString aliasOf;
    };

    explicit ParameterCatalog(QObject *parent = nullptr);

    static const QList<Parameter> &all();
    static const Parameter *find(const QString &id);   // nullptr if unknown
    static QStringList imuIds();
    static QStringList obdIds();                        // polled OBD ids (no aliases)

    QVariantList parameters() const;

    Q_INVOKABLE bool contains(const QString &id) const;
    // One parameter as in `parameters`; empty map for an unknown id
    Q_INVOKABLE QVariantMap info(const QString &id) const;

    static QVariantMap toVariant(const Parameter &p);
};

#endif // PARAMETERCATALOG_H
