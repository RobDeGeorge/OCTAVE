#include "airplaymanager.h"

#include "settingsmanager.h"
#include "volumecontroller.h"

#ifdef OCTAVE_HAVE_AIRPLAY
#include "../airplay/airplayreceiver.h"
#if defined(OCTAVE_BLUEZ)
#include "../platform/bluezhidmouse.h"
#define OCTAVE_AIRPLAY_TOUCH
#endif
#endif

#include <QDir>
#include <algorithm>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtNetwork/qtnetworkglobal.h>
#if QT_CONFIG(networkinterface)
#include <QNetworkInterface>
#endif
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>

Q_LOGGING_CATEGORY(lcAirPlayManager, "octave.airplay.manager")

namespace {
constexpr int kReportIntervalMs = 8;        // one mouse report per BLE connection event, about
constexpr int kMaxQueuedReports = 400;
constexpr int kPairingWindowMs = 3 * 60 * 1000;
}

AirPlayManager::AirPlayManager(QObject *parent)
    : QObject(parent)
{
    m_reportTimer.setInterval(kReportIntervalMs);
    connect(&m_reportTimer, &QTimer::timeout, this, &AirPlayManager::sendNextReport);
    m_pairingTimer.setSingleShot(true);
    m_pairingTimer.setInterval(kPairingWindowMs);
    connect(&m_pairingTimer, &QTimer::timeout, this, [this] { pairTouch(false); });
    updateState();
}

AirPlayManager::~AirPlayManager()
{
    stopReceiver();
#ifdef OCTAVE_AIRPLAY_TOUCH
    if (m_mouse)
        m_mouse->stop();
#endif
}

bool AirPlayManager::available() const
{
#ifdef OCTAVE_HAVE_AIRPLAY
    return true;
#else
    return false;
#endif
}

bool AirPlayManager::touchAvailable() const
{
#ifdef OCTAVE_AIRPLAY_TOUCH
    return true;
#else
    return false;
#endif
}

QVariant AirPlayManager::setting(const QString &key, const QVariant &fallback) const
{
    return m_settings ? m_settings->get_setting_with_default(key, fallback) : fallback;
}

void AirPlayManager::setSettingsManager(SettingsManager *settings)
{
    m_settings = settings;
    if (!settings)
        return;
    connect(settings, &SettingsManager::genericSettingChanged, this, [this](const QString &key) {
        if (key.startsWith(QLatin1String("airPlay")))
            applySettings();
    });
    loadState();
    applySettings();
}

void AirPlayManager::applySettings()
{
    const bool enabled = available() && setting(QStringLiteral("airPlayEnabled"), false).toBool();
    const bool touch = setting(QStringLiteral("airPlayTouchEnabled"), true).toBool();
    const QString name = setting(QStringLiteral("airPlayName"), QStringLiteral("OCTAVE")).toString().trimmed();
    m_receiverName = name.isEmpty() ? QStringLiteral("OCTAVE") : name.left(48);
    const QString config = m_receiverName + QLatin1Char('|')
                           + setting(QStringLiteral("airPlayRequirePin"), true).toString() + QLatin1Char('|')
                           + setting(QStringLiteral("airPlayResolution"), QStringLiteral("1280x720")).toString();
    if (enabled != m_enabled) {
        m_enabled = enabled;
        emit enabledChanged();
    }
    if (touch != m_touchEnabled) {
        m_touchEnabled = touch;
        emit touchChanged();
    }
    // A new name, PIN rule or size needs a fresh advertisement
    if (m_enabled && m_receiverConfig != config)
        stopReceiver();
    if (m_enabled)
        startReceiver();
    else
        stopReceiver();
    m_receiverConfig = config;
    updateMouse();
    updateState();
}

void AirPlayManager::setEnabled(bool enabled)
{
    if (m_settings)
        m_settings->save_setting(QStringLiteral("airPlayEnabled"), enabled);
}

void AirPlayManager::setTouchEnabled(bool enabled)
{
    if (m_settings)
        m_settings->save_setting(QStringLiteral("airPlayTouchEnabled"), enabled);
}

// ─── receiver ────────────────────────────────────────────────────────

QString AirPlayManager::stateDir() const
{
    return SettingsManager::getAppDataDir() + QStringLiteral("/airplay");
}

void AirPlayManager::loadState()
{
    QFile f(stateDir() + QStringLiteral("/state.json"));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        m_deviceId = o.value(QStringLiteral("deviceId")).toString();
        m_clients = o.value(QStringLiteral("clients")).toArray().toVariantList();
    }
    static const QRegularExpression mac(QStringLiteral("^([0-9a-f]{2}:){5}[0-9a-f]{2}$"));
    if (!mac.match(m_deviceId).hasMatch()) {
        // A random, locally administered address: the iPhone remembers the
        // receiver (and its PIN pairing) by it
        quint8 b[6];
        for (quint8 &x : b)
            x = quint8(QRandomGenerator::global()->bounded(256));
        b[0] = quint8((b[0] | 0x02) & 0xfe);
        m_deviceId = QString::asprintf("%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
        saveState();
    }
}

void AirPlayManager::saveState()
{
    QDir().mkpath(stateDir());
    QJsonObject o;
    o[QStringLiteral("deviceId")] = m_deviceId;
    o[QStringLiteral("clients")] = QJsonArray::fromVariantList(m_clients);
    QSaveFile f(stateDir() + QStringLiteral("/state.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson());
        f.commit();
    }
}

void AirPlayManager::startReceiver()
{
#ifdef OCTAVE_HAVE_AIRPLAY
    if (m_receiver && m_receiver->isRunning())
        return;
    if (!m_receiver) {
        m_receiver = new AirPlayReceiver(this);
        m_receiver->setVideoSink(m_videoSink);
        connect(m_receiver, &AirPlayReceiver::clientConnecting, this,
                [this](const QString &name, const QString &model, const QString &) {
            m_deviceName = name;
            m_deviceModel = model;
            updateState();
        });
        connect(m_receiver, &AirPlayReceiver::connectionsChanged, this, [this](int open) {
            m_connections = open;
            if (open == 0) {
                m_mirroring = false;
                m_pin.clear();
            }
            updateState();
        });
        connect(m_receiver, &AirPlayReceiver::mirroringChanged, this, [this](bool on) {
            m_mirroring = on;
            if (on) {
                m_pin.clear();
                m_pointerKnown = false;
            }
            updateState();
            if (on)
                emit mirroringStarted();
        });
        connect(m_receiver, &AirPlayReceiver::audioStreamingChanged, this, [this](bool on) {
            m_audioStreaming = on;
            if (!on) {
                m_title.clear();
                m_artist.clear();
                m_album.clear();
                m_coverArt.clear();
                emit mediaChanged();
            }
            updateState();
            emit audioStreamingChanged(on);
        });
        connect(m_receiver, &AirPlayReceiver::frameSizeChanged, this, [this](int w, int h) {
            // A new size is a rotation: where the pointer is is no longer known
            m_frameWidth = w;
            m_frameHeight = h;
            m_pointerKnown = false;
            emit frameSizeChanged();
        });
        connect(m_receiver, &AirPlayReceiver::pinRequested, this, [this](const QString &pin) {
            m_pin = pin;
            updateState();
        });
        connect(m_receiver, &AirPlayReceiver::clientRegistered, this,
                [this](const QString &key, const QString &deviceId, const QString &name) {
            QVariantList kept;
            for (const QVariant &c : std::as_const(m_clients)) {
                if (c.toMap().value(QStringLiteral("publicKey")).toString() != key)
                    kept.append(c);
            }
            kept.append(QVariantMap{{QStringLiteral("publicKey"), key},
                                    {QStringLiteral("deviceId"), deviceId},
                                    {QStringLiteral("name"), name}});
            m_clients = kept;
            saveState();
            qCInfo(lcAirPlayManager) << "paired" << name << "with the PIN";
        });
        connect(m_receiver, &AirPlayReceiver::senderVolumeChanged, this, [this](double fraction) {
            m_senderVolume = fraction;
            applyVolume();
        });
        connect(m_receiver, &AirPlayReceiver::metadataChanged, this,
                [this](const QString &title, const QString &artist, const QString &album) {
            m_title = title;
            m_artist = artist;
            m_album = album;
            emit mediaChanged();
        });
        connect(m_receiver, &AirPlayReceiver::coverArtChanged, this, [this](const QByteArray &jpeg) {
            m_coverArt = jpeg;
            emit mediaChanged();
        });
        connect(m_receiver, &AirPlayReceiver::connectionLost, this, [this] {
            m_mirroring = false;
            updateState();
        });
    }
    QStringList keys;
    for (const QVariant &c : std::as_const(m_clients))
        keys.append(c.toMap().value(QStringLiteral("publicKey")).toString());
    m_receiver->setRegisteredKeys(keys);
    m_receiver->setAudioOutputDevice(m_audioDeviceId);
    applyVolume();

    AirPlayReceiver::Options opt;
    opt.name = m_receiverName;
    opt.deviceId = m_deviceId;
    QDir().mkpath(stateDir());
    opt.keyFile = stateDir() + QStringLiteral("/receiver.pem");
    opt.requirePin = setting(QStringLiteral("airPlayRequirePin"), true).toBool();
    const QStringList size = setting(QStringLiteral("airPlayResolution"), QStringLiteral("1280x720")).toString()
                                 .split(QLatin1Char('x'));
    if (size.size() == 2 && size[0].toInt() >= 640 && size[1].toInt() >= 360) {
        opt.width = qMin(size[0].toInt(), 3840);
        opt.height = qMin(size[1].toInt(), 2160);
    }
    QString error;
    m_error.clear();
    if (!m_receiver->start(opt, &error))
        m_error = error;
#endif
}

void AirPlayManager::stopReceiver()
{
#ifdef OCTAVE_HAVE_AIRPLAY
    if (m_receiver)
        m_receiver->stop();
#endif
    m_mirroring = false;
    m_audioStreaming = false;
    m_connections = 0;
    m_pin.clear();
    m_deviceName.clear();
    m_deviceModel.clear();
}

void AirPlayManager::setVideoSink(QObject *sink)
{
    QVideoSink *s = qobject_cast<QVideoSink *>(sink);
    if (s == m_videoSink)
        return;
    m_videoSink = s;
#ifdef OCTAVE_HAVE_AIRPLAY
    if (m_receiver)
        m_receiver->setVideoSink(s);
#endif
    emit videoSinkChanged();
}

void AirPlayManager::updateState()
{
    QString state, status;
    if (!available()) {
        state = QStringLiteral("unavailable");
        status = QStringLiteral("This build of OCTAVE has no AirPlay receiver.");
    } else if (!m_enabled) {
        state = QStringLiteral("off");
        status = QStringLiteral("AirPlay is off.");
    } else if (!m_error.isEmpty()) {
        state = QStringLiteral("error");
        status = QStringLiteral("AirPlay could not start: %1.").arg(m_error);
    } else if (!m_pin.isEmpty()) {
        state = QStringLiteral("pin");
        status = QStringLiteral("Type %1 on the iPhone.").arg(m_pin);
    } else if (m_mirroring) {
        state = QStringLiteral("mirroring");
        status = m_deviceName.isEmpty() ? QStringLiteral("Mirroring.")
                                        : QStringLiteral("Mirroring %1.").arg(m_deviceName);
    } else if (m_audioStreaming) {
        state = QStringLiteral("audio");
        status = m_deviceName.isEmpty() ? QStringLiteral("Playing AirPlay audio.")
                                        : QStringLiteral("Playing audio from %1.").arg(m_deviceName);
    } else if (m_connections > 0) {
        state = QStringLiteral("connected");
        status = m_deviceName.isEmpty() ? QStringLiteral("Connecting...")
                                        : QStringLiteral("Connecting to %1...").arg(m_deviceName);
    } else {
        state = QStringLiteral("waiting");
        status = QStringLiteral("On the iPhone: Control Center > Screen Mirroring > %1.").arg(m_receiverName);
    }
    if (state == m_state && status == m_status)
        return;
    m_state = state;
    m_status = status;
    emit stateChanged();
}

QStringList AirPlayManager::addresses() const
{
    QStringList out;
#if QT_CONFIG(networkinterface)   // not in the WebAssembly Qt
    // The networks an iPhone could share: up, not loopback, and not a
    // container bridge or VPN tunnel (docker0, br-*, veth*, tailscale0, ...)
    static const QStringList virtualPrefixes = {
        QStringLiteral("docker"), QStringLiteral("br-"), QStringLiteral("veth"), QStringLiteral("virbr"),
        QStringLiteral("tailscale"), QStringLiteral("tun"), QStringLiteral("tap"), QStringLiteral("wg"),
        QStringLiteral("zt"), QStringLiteral("lxc"), QStringLiteral("podman")};
    const QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        if (std::any_of(virtualPrefixes.begin(), virtualPrefixes.end(),
                        [&](const QString &p) { return iface.name().startsWith(p); }))
            continue;
        for (const QNetworkAddressEntry &e : iface.addressEntries()) {
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                out.append(e.ip().toString());
        }
    }
#endif
    return out;
}

void AirPlayManager::forgetDevices()
{
    m_clients.clear();
    saveState();
#ifdef OCTAVE_HAVE_AIRPLAY
    if (m_receiver)
        m_receiver->setRegisteredKeys({});
#endif
    qCInfo(lcAirPlayManager) << "forgot every paired iPhone";
}

// ─── audio ───────────────────────────────────────────────────────────

void AirPlayManager::setVolume(float linear)
{
    m_volume = linear;
    applyVolume();
}

void AirPlayManager::setMuted(bool muted)
{
    m_muted = muted;
    applyVolume();
}

void AirPlayManager::setAudioOutputDevice(const QString &id)
{
    m_audioDeviceId = id;
#ifdef OCTAVE_HAVE_AIRPLAY
    if (m_receiver)
        m_receiver->setAudioOutputDevice(id);
#endif
}

void AirPlayManager::applyVolume()
{
#ifdef OCTAVE_HAVE_AIRPLAY
    // OCTAVE's volume, then the iPhone's own slider through the same curve
    if (m_receiver)
        m_receiver->setVolume(m_muted ? 0.0f
                                      : m_volume * VolumeController::toLinearStatic(float(m_senderVolume * 100.0)));
#endif
}

// ─── touch (Bluetooth mouse) ─────────────────────────────────────────

void AirPlayManager::updateMouse()
{
#ifdef OCTAVE_AIRPLAY_TOUCH
    const bool want = m_enabled && m_touchEnabled;
    if (want && !m_mouse) {
        m_mouse = new BluezHidMouse(this);
        connect(m_mouse, &BluezHidMouse::stateChanged, this, [this] {
            if (!m_mouse->isConnected()) {
                m_pointerKnown = false;
                m_reports.clear();
            } else if (m_mouse->isPairing()) {
                pairTouch(false);       // paired and connected: close the window
            }
            emit touchChanged();
        });
    }
    if (!m_mouse)
        return;
    if (want && !m_mouse->isRunning())
        m_mouse->start(m_receiverName + QStringLiteral(" Touch"));
    else if (!want && m_mouse->isRunning())
        m_mouse->stop();
    emit touchChanged();
#endif
}

QString AirPlayManager::touchState() const
{
#ifdef OCTAVE_AIRPLAY_TOUCH
    if (!m_mouse || !m_mouse->isRunning())
        return QStringLiteral("off");
    return m_mouse->isConnected() ? QStringLiteral("connected") : QStringLiteral("waiting");
#else
    return QStringLiteral("unavailable");
#endif
}

QString AirPlayManager::touchStatus() const
{
#ifdef OCTAVE_AIRPLAY_TOUCH
    if (!m_mouse)
        return m_touchEnabled ? QString() : QStringLiteral("Touch is off.");
    return m_mouse->status();
#else
    return QStringLiteral("Touch needs Bluetooth through BlueZ (Linux).");
#endif
}

bool AirPlayManager::touchPairing() const
{
#ifdef OCTAVE_AIRPLAY_TOUCH
    return m_mouse && m_mouse->isPairing();
#else
    return false;
#endif
}

void AirPlayManager::pairTouch(bool open)
{
#ifdef OCTAVE_AIRPLAY_TOUCH
    if (!m_mouse || !m_mouse->isRunning())
        open = false;
    if (m_mouse)
        m_mouse->setPairing(open);
    if (open)
        m_pairingTimer.start();
    else
        m_pairingTimer.stop();
    emit touchChanged();
#else
    Q_UNUSED(open)
#endif
}

QString AirPlayManager::touchDevice() const
{
#ifdef OCTAVE_AIRPLAY_TOUCH
    return m_mouse ? m_mouse->peerName() : QString();
#else
    return QString();
#endif
}

QSizeF AirPlayManager::pointerCounts() const
{
    const double scale = setting(QStringLiteral("airPlayTouchScale"), 600.0).toDouble();
    const QSizeF screen = m_frameWidth > 0 && m_frameHeight > 0 ? QSizeF(m_frameWidth, m_frameHeight)
                                                                 : QSizeF(9, 19.5);   // a portrait iPhone
    return PointerPath::countsAcross(screen, scale);
}

double AirPlayManager::scaleFromCalibration(QPointF shownAt, const QSizeF &screen)
{
    // calibrateTouch() moved kCalibrationCounts right and down from the
    // corner; the pointer showed at shownAt. Points are square, so both axes
    // give the counts across the short side; use their mean.
    const double shortSide = qMin(screen.width(), screen.height());
    if (shortSide <= 0 || shownAt.x() < 0.02 || shownAt.y() < 0.02)
        return 0;
    const double fromX = kCalibrationCounts / shownAt.x() * shortSide / screen.width();
    const double fromY = kCalibrationCounts / shownAt.y() * shortSide / screen.height();
    const double scale = (fromX + fromY) / 2;
    return scale >= 100 && scale <= 20000 ? scale : 0;
}

void AirPlayManager::calibrateTouch()
{
    m_reports.clear();
    m_calibrating = true;
    m_pointerKnown = false;
    QList<PointerPath::Report> path = PointerPath::home();
    // Straight kCalibrationCounts right and down, in small steps
    path += PointerPath::move({0, 0}, {1, 1}, QSizeF(kCalibrationCounts, kCalibrationCounts), false);
    queueReports(path);
    emit touchChanged();
}

void AirPlayManager::cancelCalibration()
{
    if (!m_calibrating)
        return;
    m_calibrating = false;
    emit touchChanged();
}

void AirPlayManager::injectTouch(int pointerId, int action, double x, double y)
{
    const QPointF at(qBound(0.0, x, 1.0), qBound(0.0, y, 1.0));
    if (m_calibrating) {
        if (action != 0)
            return;
        m_calibrating = false;
        const QSizeF screen = m_frameWidth > 0 ? QSizeF(m_frameWidth, m_frameHeight) : QSizeF(9, 19.5);
        const double scale = scaleFromCalibration(at, screen);
        if (scale > 0 && m_settings) {
            m_settings->save_setting(QStringLiteral("airPlayTouchScale"), scale);
            m_pointer = at;
            m_pointerKnown = true;
            m_carry = {};
            qCInfo(lcAirPlayManager) << "touch calibrated:" << scale << "counts across the short side";
        } else {
            qCWarning(lcAirPlayManager) << "touch calibration tap at" << at << "is unusable; scale unchanged";
        }
        emit touchChanged();
        return;
    }
    if (touchState() != QLatin1String("connected"))
        return;
    const QSizeF counts = pointerCounts();
    if (counts.isEmpty())
        return;
    switch (action) {
    case 0:     // down: put the pointer there, press
        if (m_activePointer >= 0)
            return;
        m_activePointer = pointerId;
        if (!m_pointerKnown) {
            queueReports(PointerPath::home());
            m_pointer = {0, 0};
            m_carry = {};
            m_pointerKnown = true;
        }
        queueReports(PointerPath::move(m_pointer, at, counts, false, &m_carry));
        queueReports({{0, 0, true}});
        m_pointer = at;
        break;
    case 2:     // move: drag with the button held
        if (pointerId != m_activePointer)
            return;
        queueReports(PointerPath::move(m_pointer, at, counts, true, &m_carry));
        m_pointer = at;
        break;
    case 1:     // up: release where the finger lifted
        if (pointerId != m_activePointer)
            return;
        queueReports(PointerPath::move(m_pointer, at, counts, true, &m_carry));
        queueReports({{0, 0, false}});
        m_pointer = at;
        m_activePointer = -1;
        break;
    default:
        break;
    }
}

void AirPlayManager::scroll(int dx, int dy)
{
    if (touchState() != QLatin1String("connected"))
        return;
    PointerPath::Report r;
    r.wheel = qBound(-127, -dy, 127);   // HID wheel: positive is up
    r.pan = qBound(-127, dx, 127);
    queueReports({r});
}

void AirPlayManager::queueReports(const QList<PointerPath::Report> &reports)
{
    m_reports += reports;
    // A drag that outruns the link: fold the oldest moves into the next one
    // (never across a press or release)
    while (m_reports.size() > kMaxQueuedReports && m_reports[0].button == m_reports[1].button) {
        const PointerPath::Report first = m_reports.takeFirst();
        PointerPath::Report &next = m_reports.first();
        next.dx = qBound(-127, next.dx + first.dx, 127);
        next.dy = qBound(-127, next.dy + first.dy, 127);
    }
    if (!m_reportTimer.isActive() && !m_reports.isEmpty()) {
        sendNextReport();
        m_reportTimer.start();
    }
}

void AirPlayManager::sendNextReport()
{
    if (m_reports.isEmpty()) {
        m_reportTimer.stop();
        return;
    }
    const PointerPath::Report r = m_reports.takeFirst();
#ifdef OCTAVE_AIRPLAY_TOUCH
    if (m_mouse)
        m_mouse->sendReport(r.button ? 0x01 : 0x00, r.dx, r.dy, r.wheel, r.pan);
#else
    Q_UNUSED(r)
#endif
}
