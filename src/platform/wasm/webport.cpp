#include "webport.h"

#include <QDeadlineTimer>
#include <QIODevice>

#include <cstring>

// The adapter as the OBD worker sees it. Lives on the worker thread.
class WebPortDevice : public QIODevice
{
public:
    WebPortDevice(WebPortChannel *channel, const WebPortChannel::Hooks &hooks, QObject *parent)
        : QIODevice(parent)
        , m_channel(channel)
        , m_hooks(hooks)
    {
    }

    bool isSequential() const override { return true; }

    bool open(OpenMode mode) override
    {
        WebPortChannel &s = *m_channel;
        QMutexLocker lock(&s.m_mutex);
        if (!s.m_up) {
            lock.unlock();
            QMetaObject::invokeMethod(m_hooks.context, m_hooks.reopen, Qt::QueuedConnection);
            setErrorString(m_hooks.reopening);
            return false;
        }
        s.m_rx.clear();
        setObjectName(s.m_describe);
        return QIODevice::open(mode);
    }

    qint64 bytesAvailable() const override
    {
        WebPortChannel &s = *m_channel;
        QMutexLocker lock(&s.m_mutex);
        return s.m_rx.size() + QIODevice::bytesAvailable();
    }

    bool waitForReadyRead(int msecs) override
    {
        WebPortChannel &s = *m_channel;
        QMutexLocker lock(&s.m_mutex);
        if (s.m_rx.isEmpty() && s.m_up)
            s.m_changed.wait(&s.m_mutex, QDeadlineTimer(msecs));
        if (!s.m_rx.isEmpty())
            return true;
        if (!s.m_up) {
            // OBDConnectionWorker::sendCommand() reports a closed custom
            // transport as a lost connection, with this error
            const QString err = s.m_error.isEmpty() ? m_hooks.lost : s.m_error;
            lock.unlock();
            setErrorString(err);
            close();
        }
        return false;
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        WebPortChannel &s = *m_channel;
        QMutexLocker lock(&s.m_mutex);
        const qint64 n = qMin<qint64>(maxSize, s.m_rx.size());
        std::memcpy(data, s.m_rx.constData(), size_t(n));
        s.m_rx.remove(0, n);
        return n;
    }

    qint64 writeData(const char *data, qint64 size) override
    {
        const QByteArray bytes(data, size);
        QMetaObject::invokeMethod(m_hooks.context, [write = m_hooks.write, bytes] { write(bytes); },
                                  Qt::QueuedConnection);
        return size;
    }

private:
    WebPortChannel *m_channel;
    WebPortChannel::Hooks m_hooks;
};

void WebPortChannel::connecting()
{
    QMutexLocker lock(&m_mutex);
    m_up = false;
    m_error.clear();
    m_changed.wakeAll();
}

void WebPortChannel::up(const QString &describe)
{
    QMutexLocker lock(&m_mutex);
    m_rx.clear();
    m_describe = describe;
    m_up = true;
    m_error.clear();
    m_changed.wakeAll();
}

void WebPortChannel::down(const QString &error)
{
    QMutexLocker lock(&m_mutex);
    m_up = false;
    m_error = error;
    m_changed.wakeAll();
}

void WebPortChannel::received(const QByteArray &bytes)
{
    QMutexLocker lock(&m_mutex);
    m_rx.append(bytes);
    m_changed.wakeAll();
}

QIODevice *WebPortChannel::createDevice(QObject *parent, const Hooks &hooks)
{
    return new WebPortDevice(this, hooks, parent);
}
