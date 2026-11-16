#ifndef OCTAVE_CUSTOMPIDS_H
#define OCTAVE_CUSTOMPIDS_H

// Manufacturer-specific readings the user adds in <config>/custom-pids.json
// (docs/CUSTOM_PIDS.md): what the standard OBD-II PIDs don't carry, such as
// transmission temperature, read with the manufacturer's own identifiers.
//
//   {
//     "schema": 1,
//     "pids": [
//       { "id": "TRANS_TEMP", "title": "Transmission Temp", "unit": "°C",
//         "min": -40, "max": 150, "kind": "temperature", "decimals": 0,
//         "mode": "22", "pid": "1940", "header": "7E1", "formula": "A-40" }
//     ]
//   }
//
// Read only: the request is always read-by-identifier (mode 22 or 21), built
// by OCTAVE from the mode and id, and the header is hex digits handed to
// "ATSH". Nothing from the file is sent to the car as written.
//
// A formula is the kind PID lists for other apps give: A is the first data
// byte of the reply, B the second (... Z, then AA, AB), with + - * / ( ),
// & | << >>, SIGNED(A) for a two's complement byte and {A:0} for one bit.
//
// The file is read once, at startup, before anything reads the parameter
// catalog: the rows join ParameterCatalog (group "Custom") and the requests
// go to OBDManager::setCustomPids(), after which a custom parameter is
// demanded, polled, shown and recorded like a built-in one. Edit the file,
// restart OCTAVE.
// QML context property / service object "customPids".

#include "elm327protocol.h"
#include "parametercatalog.h"

#include <QObject>
#include <QVariantList>

#include <optional>

class PidFormula
{
public:
    // An invalid formula (isValid() false) and why in `error` when the text
    // doesn't parse
    static PidFormula compile(const QString &text, QString *error = nullptr);

    bool isValid() const { return !m_code.isEmpty(); }
    // Data bytes the formula reads: 2 for "A*256+B"
    int bytesNeeded() const { return m_bytes; }
    // nullopt when the reply is shorter than bytesNeeded(), on a division
    // by zero, or for an invalid formula
    std::optional<double> evaluate(const QVector<uint8_t> &bytes) const;

private:
    friend class FormulaParser;
    enum class Op { Number, Byte, Bit, Add, Sub, Mul, Div, Neg, And, Or, Shl, Shr, Signed };
    struct Step {
        Op op;
        double number = 0;   // Number
        int byte = 0;        // Byte, Bit
        int bit = 0;         // Bit
    };
    QVector<Step> m_code;    // postfix
    int m_bytes = 0;
};

struct CustomPid {
    ParameterCatalog::Parameter meta;
    int mode = 0x22;
    int pid = 0;
    QString header;
    QString formulaText;
    PidFormula formula;
};

class CustomPids : public QObject
{
    Q_OBJECT
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "vehicle")
    Q_CLASSINFO("octave.experimental", "*")
    // <config>/custom-pids.json
    Q_PROPERTY(QString file READ file CONSTANT)
    Q_PROPERTY(bool fileExists READ fileExists NOTIFY fileExistsChanged)
    // What was loaded at startup: [{id, title, unit, mode, pid, header,
    // formula}], mode / pid / header as the hex text of the file
    Q_PROPERTY(QVariantList pids READ pids CONSTANT)
    // What was wrong with the file, one line per entry left out
    Q_PROPERTY(QStringList errors READ errors CONSTANT)

public:
    static constexpr int kMaxPids = 64;

    struct Parsed {
        QList<CustomPid> pids;
        QStringList errors;
    };
    // The entries of a custom PID file that are usable, and a line for each
    // that isn't. No I/O.
    static Parsed parse(const QByteArray &json);

    // Reads `path` (default: custom-pids.json in the config folder) and
    // adds its parameters to ParameterCatalog. Construct before anything
    // reads the catalog.
    explicit CustomPids(const QString &path = QString(), QObject *parent = nullptr);

    QString file() const { return m_file; }
    bool fileExists() const;
    QVariantList pids() const;
    QStringList errors() const { return m_errors; }

    // For OBDManager::setCustomPids()
    QList<CustomPidRequest> requests() const;

    // Writes a commented starting file when there is none. False if one
    // exists or it can't be written. It takes effect at the next start.
    Q_INVOKABLE bool createExample();

signals:
    void fileExistsChanged();

private:
    QString m_file;
    QList<CustomPid> m_pids;
    QStringList m_errors;
};

#endif // OCTAVE_CUSTOMPIDS_H
