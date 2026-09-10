#ifndef ELM327PROTOCOL_H
#define ELM327PROTOCOL_H

#include <QByteArray>
#include <QLoggingCategory>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>

#include <cstdint>
#include <functional>
#include <optional>
#include <tuple>

// ---------------------------------------------------------------------------
// ELM327/OBD-II Protocol Handler -- pure C++, no I/O.
//
// Handles command formatting, response parsing, and PID decoding.
// Port of backend/elm327_protocol.py
// ---------------------------------------------------------------------------

// Standard OBD-II SPP UUID for Bluetooth RFCOMM
static constexpr const char *SPP_UUID = "00001101-0000-1000-8000-00805f9b34fb";

// ---------------------------------------------------------------------------
// Init command: (bytes to send, timeout in ms)
// ---------------------------------------------------------------------------
struct InitCommand {
    QByteArray command;
    int timeoutMs;
};

// ---------------------------------------------------------------------------
// PID table entry
// ---------------------------------------------------------------------------
using PidDecoder = std::function<double(const QVector<uint8_t> &)>;

struct PidEntry {
    QString name;          // Human-readable name ("Coolant Temp")
    QString signalName;    // Signal name on OBDManager ("coolantTempChanged")
    PidDecoder decoder;    // Decodes raw bytes to a double
    int expectedBytes;     // Number of data bytes expected
};

// Key for PID table: (mode, pid)
using PidKey = QPair<int, int>;

// Parsed response from ELM327: (mode, pid, data_bytes)
struct ParsedResponse {
    int mode;
    int pid;
    QVector<uint8_t> dataBytes;
};

// Decoded PID result: (signal_name, value)
struct DecodedPid {
    QString signalName;
    double value;
};

// DTC code string (e.g. "P0301")
using DtcCode = QString;

// ---------------------------------------------------------------------------
// ELM327Protocol -- static utility class
// ---------------------------------------------------------------------------
Q_DECLARE_LOGGING_CATEGORY(lcElm327)

class ELM327Protocol
{
public:
    // Init sequence
    static QList<InitCommand> initCommands();

    // Error response tokens
    static const QStringList &errorResponses();

    // Format a PID request (e.g. mode=1, pid=0x0C -> "010C\r")
    static QByteArray formatPidRequest(int mode, int pid);

    // Parse raw ELM327 response string.
    // Returns std::nullopt on error/invalid.
    static std::optional<ParsedResponse> parseResponse(const QString &raw);

    // Decode a parsed PID response into (signal_name, value).
    // Returns std::nullopt if PID is unknown or data is insufficient.
    static std::optional<DecodedPid> decodePid(int mode, int pid,
                                               const QVector<uint8_t> &dataBytes);

    // Parse supported-PIDs bitmap (mode 01, PID 00/20/40/60).
    // Returns set of supported PID numbers (1-indexed relative to the base).
    static QSet<int> parseSupportedPids(const QVector<uint8_t> &dataBytes);

    // Parse Mode 03 (GET_DTC) response.
    // Returns list of DTC code strings, e.g. ["P0301", "P0420"].
    static QStringList parseDtcResponse(const QString &raw);

    // Default PIDs to poll (the 16 most common)
    static QList<PidKey> defaultPids();

    // Access the full PID table
    static const QHash<PidKey, PidEntry> &pidTable();

    // Get all parameter names from the PID table
    static QStringList allParameterNames();

private:
    ELM327Protocol() = delete; // Static-only class

    // Build the PID table (called once)
    static QHash<PidKey, PidEntry> buildPidTable();

    // DTC prefix lookup
    static QChar dtcPrefix(int code);
};

// ---------------------------------------------------------------------------
// ResponseBuffer -- accumulates bytes from the ELM327 until a complete
// response (terminated by '>') is available.
// ---------------------------------------------------------------------------
class ResponseBuffer
{
public:
    ResponseBuffer() = default;

    // Add incoming bytes
    void feed(const QByteArray &data);

    // Extract a complete response (up to '>' prompt).
    // Returns std::nullopt if no complete response yet.
    std::optional<QString> getResponse();

    // Clear the buffer
    void clear();

private:
    QByteArray m_buffer;
};

#endif // ELM327PROTOCOL_H
