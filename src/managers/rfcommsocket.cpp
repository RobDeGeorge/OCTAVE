#include "rfcommsocket.h"

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QThread>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
// From <bluetooth/bluetooth.h> / <bluetooth/rfcomm.h>, spelled out so the
// build doesn't need libbluetooth-dev for three constants and a struct
constexpr int kAfBluetooth = 31;
constexpr int kBtProtoRfcomm = 3;
struct SockaddrRc {
    sa_family_t rc_family;
    unsigned char rc_bdaddr[6];  // little-endian: last MAC byte first
    unsigned char rc_channel;
};
}  // namespace
#endif

RfcommSocket::RfcommSocket(QObject *parent)
    : QIODevice(parent)
{
}

RfcommSocket::~RfcommSocket()
{
    close();
}

bool RfcommSocket::isUrl(const QString &port)
{
    return port.startsWith(QStringLiteral("rfcomm://"), Qt::CaseInsensitive);
}

bool RfcommSocket::parseUrl(const QString &url, QString *mac, int *channel)
{
    static const QRegularExpression re(QStringLiteral(
        "^rfcomm://([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})(?:/(\\d{1,2}))?/?$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(url.trimmed());
    if (!m.hasMatch())
        return false;
    const int ch = m.captured(2).isEmpty() ? 1 : m.captured(2).toInt();
    if (ch < 1 || ch > 30)
        return false;
    if (mac) *mac = m.captured(1).toUpper();
    if (channel) *channel = ch;
    return true;
}

QString RfcommSocket::makeUrl(const QString &mac, int channel)
{
    const QString base = QStringLiteral("rfcomm://") + mac.trimmed().toUpper();
    return channel == 1 ? base : base + QStringLiteral("/%1").arg(channel);
}

void RfcommSocket::fail(const QString &what, int err)
{
#ifdef Q_OS_LINUX
    setErrorString(QStringLiteral("%1: %2").arg(what, QString::fromLocal8Bit(strerror(err))));
#else
    Q_UNUSED(err);
    setErrorString(what);
#endif
}

bool RfcommSocket::connectTo(const QString &mac, int channel, int timeoutMs)
{
    close();
    m_broken = false;
#ifdef Q_OS_LINUX
    const QStringList parts = mac.split(QLatin1Char(':'));
    if (parts.size() != 6) {
        setErrorString(QStringLiteral("Not a Bluetooth MAC: %1").arg(mac));
        return false;
    }
    SockaddrRc addr{};
    addr.rc_family = kAfBluetooth;
    for (int i = 0; i < 6; ++i)
        addr.rc_bdaddr[i] = static_cast<unsigned char>(parts[5 - i].toUInt(nullptr, 16));
    addr.rc_channel = static_cast<unsigned char>(channel);

    m_fd = ::socket(kAfBluetooth, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, kBtProtoRfcomm);
    if (m_fd < 0) {
        fail(QStringLiteral("Bluetooth socket unavailable"), errno);
        return false;
    }

    if (::connect(m_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        if (errno != EINPROGRESS && errno != EAGAIN) {
            fail(QStringLiteral("Bluetooth connect failed"), errno);
            ::close(m_fd);
            m_fd = -1;
            return false;
        }
        // Wait in short slices so a reconnect/quit can abandon us
        QElapsedTimer t;
        t.start();
        bool writable = false;
        while (t.elapsed() < timeoutMs) {
            if (QThread::currentThread()->isInterruptionRequested())
                break;
            pollfd pfd{m_fd, POLLOUT, 0};
            const int r = ::poll(&pfd, 1, 100);
            if (r > 0) { writable = true; break; }
            if (r < 0 && errno != EINTR) break;
        }
        int soErr = 0;
        socklen_t len = sizeof(soErr);
        if (!writable)
            soErr = ETIMEDOUT;
        else if (::getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &soErr, &len) != 0)
            soErr = errno;
        if (soErr != 0) {
            fail(QStringLiteral("Bluetooth connect failed"), soErr);
            ::close(m_fd);
            m_fd = -1;
            return false;
        }
    }
    return QIODevice::open(QIODevice::ReadWrite | QIODevice::Unbuffered);
#else
    Q_UNUSED(mac); Q_UNUSED(channel); Q_UNUSED(timeoutMs);
    setErrorString(QStringLiteral("Direct Bluetooth (rfcomm://) is only supported on Linux"));
    return false;
#endif
}

void RfcommSocket::close()
{
#ifdef Q_OS_LINUX
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
    if (isOpen())
        QIODevice::close();
}

qint64 RfcommSocket::bytesAvailable() const
{
    qint64 n = QIODevice::bytesAvailable();
#ifdef Q_OS_LINUX
    int pending = 0;
    if (m_fd >= 0 && ::ioctl(m_fd, FIONREAD, &pending) == 0)
        n += pending;
#endif
    return n;
}

bool RfcommSocket::waitForReadyRead(int msecs)
{
#ifdef Q_OS_LINUX
    if (m_fd < 0 || m_broken)
        return false;
    pollfd pfd{m_fd, POLLIN, 0};
    const int r = ::poll(&pfd, 1, msecs);
    if (r <= 0)
        return false;
    if ((pfd.revents & POLLIN) == 0) {
        // POLLHUP / POLLERR with nothing left to read: the link is gone
        m_broken = true;
        setErrorString(QStringLiteral("Bluetooth link closed"));
        return false;
    }
    return true;
#else
    Q_UNUSED(msecs);
    return false;
#endif
}

qint64 RfcommSocket::readData(char *data, qint64 maxSize)
{
#ifdef Q_OS_LINUX
    if (m_fd < 0)
        return -1;
    const ssize_t n = ::read(m_fd, data, static_cast<size_t>(maxSize));
    if (n > 0)
        return n;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return 0;
    m_broken = true;
    if (n == 0)
        setErrorString(QStringLiteral("Bluetooth link closed by the adapter"));
    else
        fail(QStringLiteral("Bluetooth read failed"), errno);
    return -1;
#else
    Q_UNUSED(data); Q_UNUSED(maxSize);
    return -1;
#endif
}

qint64 RfcommSocket::writeData(const char *data, qint64 size)
{
#ifdef Q_OS_LINUX
    if (m_fd < 0)
        return -1;
    qint64 done = 0;
    QElapsedTimer t;
    t.start();
    while (done < size) {
        const ssize_t n = ::write(m_fd, data + done, static_cast<size_t>(size - done));
        if (n > 0) {
            done += n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) && t.elapsed() < 2000) {
            pollfd pfd{m_fd, POLLOUT, 0};
            ::poll(&pfd, 1, 100);
            continue;
        }
        m_broken = true;
        fail(QStringLiteral("Bluetooth write failed"), n < 0 ? errno : EPIPE);
        return done > 0 ? done : -1;
    }
    return done;
#else
    Q_UNUSED(data); Q_UNUSED(size);
    return -1;
#endif
}
