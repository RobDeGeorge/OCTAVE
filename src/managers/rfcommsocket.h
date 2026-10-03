#ifndef OCTAVE_RFCOMMSOCKET_H
#define OCTAVE_RFCOMMSOCKET_H

// A direct Bluetooth Classic RFCOMM connection to an ELM327 (Linux only),
// used by OBDConnectionWorker when the configured port is
// "rfcomm://<MAC>[/<channel>]". Unlike a bound /dev/rfcommN node it needs no
// `rfcomm bind` (root), connects explicitly with a timeout and a real error
// ("Host is down", "Connection refused"), and nothing else on the system
// (blueman) can release it underneath us. Blocking-friendly on purpose: the
// OBD worker drives it with waitForReadyRead() like a QSerialPort.
// Mirrors backend/obd_transports/protocol_rfcomm.py.

#include <QIODevice>
#include <QString>

class RfcommSocket : public QIODevice
{
    Q_OBJECT
public:
    explicit RfcommSocket(QObject *parent = nullptr);
    ~RfcommSocket() override;

    // "rfcomm://88:1B:99:66:DD:5F" or "rfcomm://88:1B:99:66:DD:5F/2"
    // (channel defaults to 1, the SPP channel ELM327 clones use)
    static bool isUrl(const QString &port);
    static bool parseUrl(const QString &url, QString *mac, int *channel);
    static QString makeUrl(const QString &mac, int channel = 1);

    // Blocks up to timeoutMs; gives up early if the calling thread is
    // asked to stop (QThread::requestInterruption)
    bool connectTo(const QString &mac, int channel, int timeoutMs);

    void close() override;
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;
    bool waitForReadyRead(int msecs) override;

    // The remote closed the link or a read/write failed; the socket is dead
    bool isBroken() const { return m_broken; }

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 size) override;

private:
    void fail(const QString &what, int err);

    int m_fd = -1;
    bool m_broken = false;
};

#endif  // OCTAVE_RFCOMMSOCKET_H
