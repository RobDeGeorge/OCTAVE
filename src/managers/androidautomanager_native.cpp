/*
 * AndroidAutoManager, native mode: OCTAVE is the head unit.
 *
 * The phone runs Android Auto's head unit server (developer mode, ⋮ > Start
 * head unit server, phone port 5277). OCTAVE finds the phone over adb the
 * way phone mirroring does, forwards a local port to 5277 and runs an AAP
 * session on it (src/android_auto/, docs/ANDROID_AUTO_PROTOCOL.md). The
 * Google head-unit certificate is the user's: never shipped, loaded from
 * androidAutoCertPath or <config>/android-auto/headunit.{crt,key}.
 *
 * adb work runs off the GUI thread; while the page wants Android Auto, a
 * missing phone or server is retried every few seconds.
 */

#ifndef Q_OS_MOBILE

#include "androidautomanager.h"

#include "phonemirrormanager.h"
#include "settingsmanager.h"

#ifdef OCTAVE_HAVE_NATIVE_AA
#include "../android_auto/aapconnection.h"
#endif

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QtConcurrent>

#ifdef OCTAVE_HAVE_NATIVE_AA
namespace {

constexpr int kRetryMs = 3000;          // phone or head unit server not there yet
constexpr int kRetryAfterErrorMs = 8000;
constexpr int kHeadUnitServerPort = 5277;

// Blocking adb call for the worker thread (runAdb() is the DHU path's, on
// the GUI thread)
QString adbOutput(const QString &adb, const QStringList &args, int timeoutMs, bool *ok = nullptr)
{
    QProcess proc;
    proc.setProgram(adb);
    proc.setArguments(args);
#ifdef Q_OS_WIN
    proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *cpa) {
        cpa->flags |= 0x08000000;   // CREATE_NO_WINDOW
    });
#endif
    proc.start();
    const bool done = proc.waitForFinished(timeoutMs);
    if (!done)
        proc.kill();
    if (ok)
        *ok = done && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    return QString::fromUtf8(proc.readAllStandardOutput() + proc.readAllStandardError()).trimmed();
}

struct StepResult {
    QString state;      // "" = go ahead and connect
    QString status;
    QString serial;
    int port = 0;
};

// Find the phone, check its head unit server listens, forward a port to it
StepResult prepare(const QString &adb, const QString &preferredSerial)
{
    StepResult r;
    const auto rows = PhoneMirrorManager::parseDevices(adbOutput(adb, {QStringLiteral("devices")}, 8000));
    r.serial = PhoneMirrorManager::pickSerial(rows, preferredSerial);
    if (r.serial.isEmpty()) {
        r.state = QStringLiteral("waitingForPhone");
        r.status = QStringLiteral("Connect your phone with USB debugging on (or pair it for wireless debugging "
                                  "in Phone Mirror)");
        return r;
    }
    // Listening sockets of every app, from the kernel: a row for local port
    // 5277 (0x149D) in state 0A (LISTEN). dumpsys can't tell a running
    // service from a stale record.
    const QString tcp = adbOutput(adb, {QStringLiteral("-s"), r.serial, QStringLiteral("shell"),
                                        QStringLiteral("cat /proc/net/tcp6 /proc/net/tcp")}, 8000);
    static const QRegularExpression listen(QStringLiteral(":149D [0-9A-F:]+ 0A "));
    if (!listen.match(tcp).hasMatch()) {
        r.state = QStringLiteral("waitingForServer");
        r.status = QStringLiteral("Start the head unit server on the phone: Android Auto settings > ⋮ > "
                                  "Start head unit server. (No menu? Tap Version in Android Auto settings "
                                  "10 times to turn on developer mode.)");
        return r;
    }
    bool ok = false;
    const QString out = adbOutput(adb, {QStringLiteral("-s"), r.serial, QStringLiteral("forward"),
                                        QStringLiteral("tcp:0"),
                                        QStringLiteral("tcp:%1").arg(kHeadUnitServerPort)}, 8000, &ok);
    r.port = out.toInt();
    if (!ok || r.port <= 0) {
        r.state = QStringLiteral("error");
        r.status = QStringLiteral("adb could not forward a port to the phone: %1").arg(out);
    }
    return r;
}

} // namespace
#endif // OCTAVE_HAVE_NATIVE_AA

QVariant AndroidAutoManager::setting(const QString &key, const QVariant &fallback) const
{
    return m_settings ? m_settings->get_setting_with_default(key, fallback) : fallback;
}

void AndroidAutoManager::setSettingsManager(SettingsManager *settings)
{
    m_settings = settings;
    if (!settings)
        return;
    connect(settings, &SettingsManager::genericSettingChanged, this, [this](const QString &key) {
        if (key == QLatin1String("androidAutoBackend"))
            emit backendChanged();
    });
}

QString AndroidAutoManager::backend() const
{
    if (!nativeAvailable())
        return QStringLiteral("dhu");
    const QString b = setting(QStringLiteral("androidAutoBackend"), QStringLiteral("dhu")).toString();
    return b == QLatin1String("native") ? b : QStringLiteral("dhu");
}

bool AndroidAutoManager::nativeAvailable() const
{
#ifdef OCTAVE_HAVE_NATIVE_AA
    return true;
#else
    return false;
#endif
}

QString AndroidAutoManager::certificateDir() const
{
    return SettingsManager::getAppDataDir() + QStringLiteral("/android-auto");
}

void AndroidAutoManager::setVideoSink(QObject *sink)
{
    QVideoSink *s = qobject_cast<QVideoSink *>(sink);
    if (s == m_videoSink)
        return;
    m_videoSink = s;
#ifdef OCTAVE_HAVE_NATIVE_AA
    if (m_aap)
        m_aap->setVideoSink(s);
#endif
    emit videoSinkChanged();
}

void AndroidAutoManager::setVolume(float linear)
{
    m_volume = linear;
#ifdef OCTAVE_HAVE_NATIVE_AA
    if (m_aap)
        m_aap->setVolume(m_muted ? 0.0f : linear);
#endif
}

void AndroidAutoManager::setMuted(bool muted)
{
    m_muted = muted;
    setVolume(m_volume);
}

void AndroidAutoManager::setAudioOutputDevice(const QString &id)
{
    m_audioDeviceId = id;
#ifdef OCTAVE_HAVE_NATIVE_AA
    if (m_aap)
        m_aap->setAudioOutputDevice(id);
#endif
}

void AndroidAutoManager::setNightMode(bool night)
{
    m_nightMode = night;
#ifdef OCTAVE_HAVE_NATIVE_AA
    if (m_aap)
        m_aap->setNightMode(night);
#endif
}

void AndroidAutoManager::setViewVisible(bool visible)
{
    m_viewVisible = visible;
#ifdef OCTAVE_HAVE_NATIVE_AA
    if (m_aap)
        m_aap->setVideoFocus(visible);
#endif
}

void AndroidAutoManager::setNativeState(const QString &state, const QString &status)
{
    if (state == m_nativeState && status == m_nativeStatus)
        return;
    if (state != m_nativeState)
        qCInfo(lcAndroidAuto) << "native:" << state << status;
    m_nativeState = state;
    m_nativeStatus = status;
    emit nativeStateChanged();
}

bool AndroidAutoManager::loadCertificate(QByteArray &cert, QByteArray &key, QString &why) const
{
    QString certPath = setting(QStringLiteral("androidAutoCertPath"), QString()).toString();
    QString keyPath;
    if (certPath.isEmpty()) {
        certPath = certificateDir() + QStringLiteral("/headunit.crt");
        keyPath = certificateDir() + QStringLiteral("/headunit.key");
    } else {
        const QFileInfo fi(certPath);
        keyPath = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".key"));
    }
    QFile cf(certPath);
    if (!cf.open(QIODevice::ReadOnly)) {
        why = QStringLiteral("Android Auto needs a head-unit certificate. Put headunit.crt and headunit.key in %1 "
                             "(or set androidAutoCertPath).").arg(QDir::toNativeSeparators(certificateDir()));
        return false;
    }
    cert = cf.readAll();
    if (cert.contains("PRIVATE KEY")) {
        key.clear();            // the key is in the same file
        return true;
    }
    QFile kf(keyPath);
    if (!kf.open(QIODevice::ReadOnly)) {
        why = QStringLiteral("No private key next to the head-unit certificate (looked for %1).")
                  .arg(QDir::toNativeSeparators(keyPath));
        return false;
    }
    key = kf.readAll();
    return true;
}

bool AndroidAutoManager::startNative()
{
#ifdef OCTAVE_HAVE_NATIVE_AA
    m_nativeWanted = true;
    if (!m_retryTimer) {
        m_retryTimer = new QTimer(this);
        m_retryTimer->setSingleShot(true);
        connect(m_retryTimer, &QTimer::timeout, this, &AndroidAutoManager::nativeStep);
    }
    if (m_aap && m_aap->isActive())
        return true;    // already connecting or running
    nativeStep();
    return m_nativeState != QLatin1String("error");
#else
    setNativeState(QStringLiteral("error"),
                   QStringLiteral("This build has no native Android Auto (it needs OpenSSL and FFmpeg)."));
    return false;
#endif
}

void AndroidAutoManager::nativeStep()
{
#ifdef OCTAVE_HAVE_NATIVE_AA
    if (!m_nativeWanted || m_stepRunning || (m_aap && m_aap->isActive()))
        return;
    QByteArray cert, key;
    QString why;
    if (!loadCertificate(cert, key, why)) {
        setNativeState(QStringLiteral("needsCertificate"), why);
        m_retryTimer->start(kRetryMs);
        return;
    }
    const QString adb = findAdbPath();
    if (adb.isEmpty()) {
        setNativeState(QStringLiteral("error"), QStringLiteral("adb was not found (it comes with OCTAVE's "
                                                               "platform tools, or the Android SDK)."));
        m_retryTimer->start(kRetryAfterErrorMs);
        return;
    }
    if (m_nativeState == QLatin1String("idle") || m_nativeState == QLatin1String("error"))
        setNativeState(QStringLiteral("connecting"), QStringLiteral("Looking for the phone..."));
    m_stepRunning = true;
    auto *watcher = new QFutureWatcher<StepResult>(this);
    connect(watcher, &QFutureWatcher<StepResult>::finished, this, [this, watcher, cert, key] {
        watcher->deleteLater();
        m_stepRunning = false;
        const StepResult r = watcher->result();
        if (!r.serial.isEmpty())
            m_serial = r.serial;
        if (!m_nativeWanted) {
            if (r.port > 0) {
                m_forwardPort = r.port;
                removeForward();
            }
            return;
        }
        if (!r.state.isEmpty()) {
            setNativeState(r.state, r.status);
            m_retryTimer->start(r.state == QLatin1String("error") ? kRetryAfterErrorMs : kRetryMs);
            return;
        }
        m_forwardPort = r.port;

        AapSession::Options opt;
        opt.certPem = cert;
        opt.keyPem = key;
        const QString res = setting(QStringLiteral("androidAutoResolution"), QStringLiteral("1280x720")).toString();
        const QStringList wh = res.split(QLatin1Char('x'));
        if (wh.size() == 2 && Aap::videoResolutionCode(wh[0].toInt(), wh[1].toInt()) != 0) {
            opt.unit.width = wh[0].toInt();
            opt.unit.height = wh[1].toInt();
        }
        opt.unit.fps = setting(QStringLiteral("androidAutoFps"), 30).toInt() >= 60 ? 60 : 30;
        opt.unit.dpi = qBound(80, setting(QStringLiteral("androidAutoDpi"), 160).toInt(), 400);
        opt.unit.softwareVersion = QStringLiteral(OCTAVE_VERSION);
        m_videoWidth = opt.unit.width;
        m_videoHeight = opt.unit.height;

        if (!m_aap) {
            m_aap = new AapConnection(this);
            connect(m_aap, &AapConnection::stateChanged, this, [this](AapSession::State s) {
                if (s == AapSession::State::Handshake || s == AapSession::State::Discovery)
                    setNativeState(QStringLiteral("connecting"), QStringLiteral("Starting Android Auto..."));
            });
            connect(m_aap, &AapConnection::running, this, [this](const QString &name) {
                m_phoneName = name;
                m_aap->setNightMode(m_nightMode);
                m_aap->setVideoFocus(m_viewVisible);
                setNativeState(QStringLiteral("running"),
                               name.isEmpty() ? QStringLiteral("Android Auto") : QStringLiteral("Android Auto on %1").arg(name));
            });
            connect(m_aap, &AapConnection::videoStreaming, this, [this](bool on) {
                if (on == m_videoStreaming)
                    return;
                m_videoStreaming = on;
                emit videoStreamingChanged();
            });
            connect(m_aap, &AapConnection::failed, this, [this](const QString &reason) {
                setNativeState(QStringLiteral("error"), reason);
                emit error(reason);
            });
            connect(m_aap, &AapConnection::ended, this, [this](const QString &reason) {
                if (m_nativeWanted)
                    setNativeState(QStringLiteral("connecting"), QStringLiteral("Android Auto ended (%1); reconnecting...").arg(reason));
            });
            connect(m_aap, &AapConnection::finished, this, &AndroidAutoManager::onNativeFinished);
        }
        m_aap->setVideoSink(m_videoSink);
        m_aap->setAudioOutputDevice(m_audioDeviceId);
        m_aap->setVolume(m_muted ? 0.0f : m_volume);
        qCInfo(lcAndroidAuto) << "native: phone" << m_serial << "head unit server on local port" << m_forwardPort;
        m_aap->start(QStringLiteral("127.0.0.1"), quint16(m_forwardPort), opt);
    });
    watcher->setFuture(QtConcurrent::run(prepare, adb, m_serial));
#endif
}

void AndroidAutoManager::onNativeFinished()
{
    removeForward();
    m_touchSlots.clear();
    m_touchPos.clear();
    if (m_videoStreaming) {
        m_videoStreaming = false;
        emit videoStreamingChanged();
    }
    if (!m_nativeWanted) {
        setNativeState(QStringLiteral("idle"), QString());
        return;
    }
    // Still wanted: reconnect (a phone that ended the session, a dropped link)
    if (m_nativeState == QLatin1String("running"))
        setNativeState(QStringLiteral("connecting"), QStringLiteral("Reconnecting..."));
    if (m_retryTimer)
        m_retryTimer->start(m_nativeState == QLatin1String("error") ? kRetryAfterErrorMs : kRetryMs);
}

void AndroidAutoManager::removeForward()
{
    if (m_forwardPort <= 0 || m_serial.isEmpty())
        return;
    const QString adb = findAdbPath();
    const QStringList args{QStringLiteral("-s"), m_serial, QStringLiteral("forward"), QStringLiteral("--remove"),
                           QStringLiteral("tcp:%1").arg(m_forwardPort)};
    m_forwardPort = 0;
    if (!adb.isEmpty())
        QProcess::startDetached(adb, args);
}

void AndroidAutoManager::stopNative()
{
    m_nativeWanted = false;
    if (m_retryTimer)
        m_retryTimer->stop();
#ifdef OCTAVE_HAVE_NATIVE_AA
    if (m_aap && m_aap->isActive()) {
        m_aap->stop();      // bye-bye; onNativeFinished() follows
        return;
    }
#endif
    removeForward();
    setNativeState(QStringLiteral("idle"), QString());
}

void AndroidAutoManager::injectTouch(int pointerId, int action, double x, double y)
{
#ifdef OCTAVE_HAVE_NATIVE_AA
    if (!m_aap || m_nativeState != QLatin1String("running"))
        return;
    const QPoint pos(qBound(0, int(x * m_videoWidth), m_videoWidth - 1),
                     qBound(0, int(y * m_videoHeight), m_videoHeight - 1));
    // Android Auto pointer ids are small and reused; the touch area's are not
    int slot = m_touchSlots.value(pointerId, -1);
    if (action == 0) {
        if (slot < 0) {
            for (slot = 0; m_touchPos.contains(slot); ++slot) {}
            m_touchSlots.insert(pointerId, slot);
        }
    } else if (slot < 0) {
        return;     // a move or release whose press we never saw
    }
    m_touchPos.insert(slot, pos);

    QList<Aap::TouchPointer> pointers;
    int index = 0;
    QList<int> ids = m_touchPos.keys();
    std::sort(ids.begin(), ids.end());
    for (int i = 0; i < ids.size(); ++i) {
        const QPoint p = m_touchPos.value(ids[i]);
        pointers.append(Aap::TouchPointer{p.x(), p.y(), ids[i]});
        if (ids[i] == slot)
            index = i;
    }
    Aap::TouchAction a = Aap::TouchAction::Moved;
    if (action == 0)
        a = pointers.size() == 1 ? Aap::TouchAction::Down : Aap::TouchAction::PointerDown;
    else if (action == 1)
        a = pointers.size() == 1 ? Aap::TouchAction::Up : Aap::TouchAction::PointerUp;
    m_aap->sendTouch(pointers, a == Aap::TouchAction::Moved ? 0 : index, a);
    if (action == 1) {
        m_touchPos.remove(slot);
        m_touchSlots.remove(pointerId);
    }
#else
    Q_UNUSED(pointerId) Q_UNUSED(action) Q_UNUSED(x) Q_UNUSED(y)
#endif
}

#endif // Q_OS_MOBILE
