#ifndef OCTAVE_WASM_QSERIALPORT_H
#define OCTAVE_WASM_QSERIALPORT_H

// WebAssembly build only (this directory is on the include path when
// OCTAVE_WASM): a browser has no serial ports and Qt SerialPort has no wasm
// port, so OBDManager's QSerialPort path compiles against this stand-in whose
// open() always fails. The simulated car connects through
// OBDConnectionWorker::setCustomTransport() and never reaches it.

#include <QIODevice>
#include <QString>

class QSerialPort : public QIODevice
{
    Q_OBJECT
public:
    enum BaudRate { Baud9600 = 9600, Baud38400 = 38400, Baud115200 = 115200 };
    enum DataBits { Data8 = 8 };
    enum Parity { NoParity = 0 };
    enum StopBits { OneStop = 1 };
    enum FlowControl { NoFlowControl };
    enum SerialPortError {
        NoError, DeviceNotFoundError, PermissionError, OpenError, WriteError,
        ReadError, ResourceError, UnsupportedOperationError, UnknownError,
        TimeoutError, NotOpenError
    };
    Q_ENUM(SerialPortError)

    explicit QSerialPort(QObject *parent = nullptr) : QIODevice(parent) {}

    void setPortName(const QString &name) { m_name = name; }
    QString portName() const { return m_name; }
    bool setBaudRate(qint32) { return true; }
    bool setDataBits(DataBits) { return true; }
    bool setParity(Parity) { return true; }
    bool setStopBits(StopBits) { return true; }
    bool setFlowControl(FlowControl) { return true; }
    void setReadBufferSize(qint64) {}
    bool flush() { return false; }
    SerialPortError error() const { return m_error; }
    void clearError() { m_error = NoError; }

    bool open(OpenMode) override
    {
        m_error = DeviceNotFoundError;
        setErrorString(QStringLiteral("Serial ports are not available in the browser"));
        emit errorOccurred(m_error);
        return false;
    }
    bool isSequential() const override { return true; }

signals:
    void errorOccurred(QSerialPort::SerialPortError error);

protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QString m_name;
    SerialPortError m_error = NoError;
};

#endif
