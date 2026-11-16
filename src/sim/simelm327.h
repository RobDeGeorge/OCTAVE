#ifndef OCTAVE_SIM_SIMELM327_H
#define OCTAVE_SIM_SIMELM327_H

// A simulated ELM327 adapter: a QIODevice the real OBDConnectionWorker talks
// to exactly as it would a serial port. It answers the AT init sequence,
// Mode 01 PIDs (encoded from the CarSim values with the inverse of the
// decoders in elm327protocol.cpp), the supported-PID bitmaps, Mode 03/04/07
// DTCs, the Mode 02 freeze-frame DTC and the Mode 09 VIN (CAN multi-frame).
//
// Every PID in ELM327Protocol's tables is reported as supported, so no
// dashboard widget is gated out in a simulated run. Modelled PIDs follow the
// simulation; the rest hold plausible fixed values.
//
// Lives on the OBD worker thread; registered for port "sim://elm327" by
// the Simulator (simulator.h) through OBDConnectionWorker::setCustomTransport().

#include <QElapsedTimer>
#include <QIODevice>
#include <QList>

#include <memory>

namespace OctaveSim {

class CarState;

inline constexpr const char *kSimPort = "sim://elm327";

class SimElm327Device : public QIODevice
{
    Q_OBJECT

public:
    explicit SimElm327Device(std::shared_ptr<CarState> state, QObject *parent = nullptr);

    bool open(OpenMode mode) override;
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;
    bool waitForReadyRead(int msecs) override;

    // Reply text for one command (no prompt), as the adapter would print it
    // with echo off. A reply of several frames comes with its ids when
    // headers are on (ATH1); writeData() adds the id to a one-line reply.
    // Public for tests.
    QByteArray respond(const QByteArray &command);

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 size) override;

private:
    struct Pending {
        QByteArray bytes;
        qint64 readyAtMs;   // on m_clock
    };

    int latencyMs(const QByteArray &command) const;
    QByteArray mode01(int pid);
    QByteArray formatMessage(const QByteArray &payload) const;
    QByteArray replyId() const;
    bool dropLink();

    std::shared_ptr<CarState> m_state;
    QByteArray m_input;      // command bytes not yet terminated by '\r'
    QByteArray m_ready;      // reply bytes the worker may read now
    QList<Pending> m_pending;
    QElapsedTimer m_clock;
    bool m_spaces = true;    // ATS0 turns them off; the worker always sends it
    bool m_echo = true;      // ATE0 turns it off; until then each command comes back first
    bool m_probed = false;   // a PID has been answered since the last ATZ
    QByteArray m_header;     // ATSH's, empty for the default (7DF)

    // "ATMA": the bus's broadcast traffic, printed until any byte arrives.
    // Six made-up messages of an 11-bit, 500 kbit/s CAN bus that follow the
    // car (0C9 engine speed and pedal every 20 ms ... 120 odometer every
    // second). Unfiltered, the adapter's buffer fills every kMonitorBuffer
    // messages, as a real one's does on a busy bus.
    static constexpr int kMonitorBuffer = 500;
    void pumpMonitor();
    bool m_headersOn = false;      // ATH1
    bool m_monitoring = false;
    QByteArray m_receiveFilter;    // ATCRA's, X = any digit; empty for all
    qint64 m_monitorAtMs = 0;      // traffic is generated up to here
    int m_monitorSent = 0;
};

} // namespace OctaveSim

#endif // OCTAVE_SIM_SIMELM327_H
