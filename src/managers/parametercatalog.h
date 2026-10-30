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
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "read")
    // Every parameter as {id, title, shortTitle, unit, min, max, kind,
    // decimals, source, aliasOf, group}, in display order
    Q_PROPERTY(QVariantList parameters READ parameters CONSTANT)
    // The same rows in the units the gauges show (setDisplayUnits): unit,
    // min and max converted, plus displayScale / displayOffset so a value
    // converts as raw * displayScale + displayOffset (1 / 0 when unchanged).
    // `parameters`, all() and find() stay in each parameter's own unit (the
    // black box, the poller, Trip History).
    Q_PROPERTY(QVariantList displayParameters READ displayParameters NOTIFY displayParametersChanged)
    // The `group` values in the order pickers list them: the car's systems
    // ("Engine", "Fuel & air", ...), then "Sensors" (the IMU)
    Q_PROPERTY(QStringList groups READ groups CONSTANT)

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
        // The system it belongs to, one of groups(): how pickers sort it
        QString group;
    };

    explicit ParameterCatalog(QObject *parent = nullptr);

    static const QList<Parameter> &all();
    static const Parameter *find(const QString &id);   // nullptr if unknown
    static QStringList imuIds();
    static QStringList obdIds();                        // polled OBD ids (no aliases)
    static QStringList groups();

    QVariantList parameters() const;
    QVariantList displayParameters() const;

    // Unit preferences (main.cpp feeds SettingsManager's): temperatures
    // ("C" | "F", every kind "temperature" row), SPEED ("mph" | "km/h"),
    // ALTITUDE ("m" | "ft"). The defaults leave every row unchanged.
    void setDisplayUnits(const QString &temp, const QString &speed, const QString &altitude);
    // A value of `id` in display units (the reference QML's scale/offset follow)
    Q_INVOKABLE QVariant toDisplay(const QString &id, const QVariant &value) const;

    struct Conversion {
        double scale = 1.0;
        double offset = 0.0;
        QString unit;   // empty: the parameter's own
    };
    Conversion conversionFor(const Parameter &p) const;


    Q_INVOKABLE bool contains(const QString &id) const;
    // One parameter as in `parameters`; empty map for an unknown id
    Q_INVOKABLE QVariantMap info(const QString &id) const;

    static QVariantMap toVariant(const Parameter &p);

signals:
    void displayParametersChanged();

private:
    QString m_tempUnit = QStringLiteral("C");
    QString m_speedUnit = QStringLiteral("mph");
    QString m_altitudeUnit = QStringLiteral("m");
};

#endif // PARAMETERCATALOG_H
