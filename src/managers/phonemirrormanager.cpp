#ifndef Q_OS_MOBILE

#include "phonemirrormanager.h"
#include "../phone_mirror/scrcpyclient.h"
#include "../phone_mirror/qrcodegen.hpp"
#include "settingsmanager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QUrl>
#include <algorithm>
#include <climits>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>

Q_LOGGING_CATEGORY(lcPhoneMirror, "octave.phonemirror")

// Size of the virtual display scrcpy creates on the phone (--new-display,
// Android 11+) when the setting is "auto" but the view has not reported its
// size yet. Landscape suits a dash screen. scrcpy rounds each dimension down
// to a multiple of 8, so sizes are snapped the same way to keep the UI honest.
static const QString kDefaultDisplaySize = QStringLiteral("1280x800");
static const QString kAutoDisplaySize = QStringLiteral("auto");
// A view resize restarts an "auto" session once the size has settled
static constexpr int kResizeRestartMs = 1500;

// ─── Construction ─────────────────────────────────────────────────────

PhoneMirrorManager::PhoneMirrorManager(QObject *parent)
    : QObject(parent), m_displaySize(kDefaultDisplaySize)
{
    m_adbPath = findAdb();
    if (m_adbPath.isEmpty()) {
        qCWarning(lcPhoneMirror) << "No adb found: phone mirroring is unavailable";
    } else {
        const int tools = adbToolsMajor(m_adbPath);
        qCInfo(lcPhoneMirror).noquote() << "Using adb" << m_adbPath << "(platform-tools"
                                        << (tools > 0 ? QString::number(tools) : QStringLiteral("unknown")) + ')';
        if (tools < kAdbWirelessMinTools)
            qCWarning(lcPhoneMirror) << "This adb is too old for wireless pairing; QR pairing needs platform-tools"
                                     << kAdbWirelessMinTools << "or newer";
    }
    // Wireless jobs run one at a time, so a reconnect never races a pairing
    m_wirelessPool.setMaxThreadCount(1);
    // adb probes and start prep: one at a time, never behind a 2-minute QR wait
    m_probePool.setMaxThreadCount(1);
    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(kResizeRestartMs);
    m_queueArtTimer.setSingleShot(true);
    m_queueArtTimer.setInterval(150);
    connect(&m_queueArtTimer, &QTimer::timeout, this, &PhoneMirrorManager::publishMediaQueue);
    connect(&m_resizeTimer, &QTimer::timeout, this, [this]() {
        if (isRunning() && m_displaySize == kAutoDisplaySize && !m_startedAutoSize.isEmpty()
            && autoDisplaySize(m_viewWidth, m_viewHeight) != m_startedAutoSize) {
            qCInfo(lcPhoneMirror) << "View resized: restarting the mirror at"
                                  << autoDisplaySize(m_viewWidth, m_viewHeight);
            restartSession();
        }
    });
}

PhoneMirrorManager::~PhoneMirrorManager()
{
    cleanup();
    ++m_qrGeneration;   // a QR pairing wait gives up within a second
    m_wirelessPool.clear();
    m_wirelessPool.waitForDone();
    m_probePool.clear();
    m_probePool.waitForDone();
}

// ─── Availability / environment ───────────────────────────────────────

bool PhoneMirrorManager::nativeAvailable() const
{
    return ScrcpyClient::available() && !m_adbPath.isEmpty();
}

QString PhoneMirrorManager::serverVersion() const
{
    return QLatin1String(ScrcpyClient::kServerVersion);
}

bool PhoneMirrorManager::environmentOk()
{
    // Everything OCTAVE needs is present, i.e. a failure is about the phone,
    // not the setup (the view then hides the setup text).
    return nativeAvailable();
}

// How to get adb, for the person who actually hit the error: a source
// checkout has the fetch script, a Linux install wants the distro package
// (Debian / Raspberry Pi OS call it "adb"), and on Windows / macOS a release
// bundles adb, so a missing one means a broken install.
static QString adbInstallHint()
{
    const QString script = QDir::cleanPath(QCoreApplication::applicationDirPath()
                                           + QStringLiteral("/../scripts/fetch_platform_tools.py"));
    if (QFile::exists(script))
        return QStringLiteral("run python3 scripts/fetch_platform_tools.py in the OCTAVE checkout");
#if defined(Q_OS_LINUX)
    return QStringLiteral("install it: sudo apt install adb on Raspberry Pi OS / Debian / Ubuntu, "
                          "sudo pacman -S android-tools on Arch");
#else
    return QStringLiteral("reinstall OCTAVE, which bundles it, or install Google's Android platform-tools");
#endif
}

QString PhoneMirrorManager::getInstallInstructions()
{
    QStringList missing;
    if (!ScrcpyClient::available())
        missing << QStringLiteral("a build with libavcodec (the H.264 decoder) and the bundled phone server");
    if (m_adbPath.isEmpty())
        missing << QStringLiteral("adb (") + adbInstallHint() + QLatin1Char(')');
    if (!missing.isEmpty())
        return QStringLiteral("Phone mirroring needs ") + missing.join(QStringLiteral(" and ")) + QLatin1Char('.');
    return QStringLiteral("Enable USB debugging on the phone (Settings > Developer options), connect it "
                          "over USB and accept the authorization prompt. To go wireless, open Phone Mirror and "
                          "scan its QR code from the phone's Wireless debugging screen.");
}

// ─── adb discovery ────────────────────────────────────────────────────

QString PhoneMirrorManager::findAdb()
{
    // Bundled platform-tools first (scripts/fetch_platform_tools.py; CI ships
    // it next to the binary). Dev layout: <repo>/tools/platform-tools/<os>/,
    // deployed: <app dir>/platform-tools/ (or Contents/Resources on macOS).
#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("adb.exe");
    const QString osName = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    const QString exe = QStringLiteral("adb");
    const QString osName = QStringLiteral("darwin");
#else
    const QString exe = QStringLiteral("adb");
    const QString osName = QStringLiteral("linux");
#endif
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString home = QDir::homePath();
    QStringList candidates{
        appDir + QStringLiteral("/platform-tools/") + exe,
        appDir + QStringLiteral("/../tools/platform-tools/") + osName + QLatin1Char('/') + exe,
        appDir + QStringLiteral("/../Resources/platform-tools/") + exe,
        QStandardPaths::findExecutable(QStringLiteral("adb")),
#ifdef Q_OS_WIN
        QStringLiteral("C:/Program Files/Android/platform-tools/adb.exe"),
        QStringLiteral("C:/Program Files (x86)/Android/platform-tools/adb.exe"),
        home + QStringLiteral("/AppData/Local/Android/Sdk/platform-tools/adb.exe"),
#else
        // The SDK before the distro paths: Ubuntu 22.04's /usr/bin/adb is
        // 28.x, too old to pair, and Google ships no ARM64 Linux build
        home + QStringLiteral("/Android/Sdk/platform-tools/adb"),
        QStringLiteral("/usr/bin/adb"),
        QStringLiteral("/usr/local/bin/adb"),
#endif
    };
    candidates.removeAll(QString());
    // First adb new enough for wireless pairing wins; with none, the first
    // one found still serves USB
    QString fallback;
    for (const QString &c : std::as_const(candidates)) {
        if (!QFileInfo::exists(c))
            continue;
        const QString path = QFileInfo(c).absoluteFilePath();
        if (adbToolsMajor(path) >= kAdbWirelessMinTools)
            return path;
        if (fallback.isEmpty())
            fallback = path;
    }
    return fallback;
}

int PhoneMirrorManager::adbToolsMajor(const QString &adbPath)
{
    // `adb version` prints "Version 36.0.2-..." (platform-tools); builds
    // too old to have that line are far too old to pair. Cached: findAdb
    // runs from several managers and a box64-run adb is slow to start.
    static QMutex mutex;
    static QHash<QString, int> cache;
    {
        QMutexLocker lock(&mutex);
        const auto it = cache.constFind(adbPath);
        if (it != cache.constEnd())
            return *it;
    }
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    ScrcpyClient::hideConsoleWindow(&proc);
    proc.start(adbPath, {QStringLiteral("version")});
    int major = 0;
    if (proc.waitForFinished(5000)) {
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("^Version (\\d+)\\."),
                                                             QRegularExpression::MultilineOption)
                                              .match(QString::fromUtf8(proc.readAllStandardOutput()));
        if (m.hasMatch())
            major = m.captured(1).toInt();
    } else {
        proc.kill();
        proc.waitForFinished(1000);
    }
    QMutexLocker lock(&mutex);
    cache.insert(adbPath, major);
    return major;
}

QString PhoneMirrorManager::runAdb(const QStringList &args, int timeoutMs) const
{
    if (m_adbPath.isEmpty())
        return {};
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    ScrcpyClient::hideConsoleWindow(&proc);
    proc.start(m_adbPath, args);
    if (!proc.waitForFinished(timeoutMs) || proc.exitCode() != 0)
        return {};
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

QString PhoneMirrorManager::runAdbFull(const QStringList &args, int timeoutMs, int *exitCode) const
{
    if (exitCode)
        *exitCode = -1;
    if (m_adbPath.isEmpty())
        return {};
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    ScrcpyClient::hideConsoleWindow(&proc);
    proc.start(m_adbPath, args);
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(1000);
        return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    }
    if (exitCode)
        *exitCode = proc.exitCode();
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

QList<QPair<QString, QString>> PhoneMirrorManager::parseDevices(const QString &output)
{
    // "List of devices attached", then "<serial>\t<state>" per phone. adb
    // prints "* daemon not running; starting now ..." lines first when it
    // has to start its server, and "no permissions (user in plugdev group;
    // are your udev rules wrong?); see [...]" as the state of a USB phone
    // this user may not open.
    QList<QPair<QString, QString>> rows;
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('*')) || line.startsWith(QLatin1String("List of devices"))
            || line.startsWith(QLatin1String("adb server")) || line.startsWith(QLatin1String("adb:")))
            continue;
        const int split = line.indexOf(ws);
        if (split <= 0)
            continue;
        const QString serial = line.left(split);
        const QString rest = line.mid(split).trimmed();
        QString state = rest.section(ws, 0, 0);
        if (rest.startsWith(QLatin1String("no permissions")))
            state = QStringLiteral("no-permissions");
        if (!state.isEmpty())
            rows.append({serial, state});
    }
    return rows;
}

QString PhoneMirrorManager::summarizeState(const QList<QPair<QString, QString>> &rows)
{
    // The most useful state wins: a usable phone, then the ones the user can fix
    static const char *const kOrder[] = {"device", "unauthorized", "no-permissions", "offline"};
    for (const char *wanted : kOrder)
        for (const auto &r : rows)
            if (r.second == QLatin1String(wanted))
                return r.second;
    return QStringLiteral("none");
}

QString PhoneMirrorManager::pickSerial(const QList<QPair<QString, QString>> &rows, const QString &preferred)
{
    // The phone picked in the UI, then USB (the faster, steadier link when
    // the phone is on both), then Wi-Fi
    QString usb;
    QString wireless;
    for (const auto &d : rows) {
        if (d.second != QLatin1String("device"))
            continue;
        if (!preferred.isEmpty() && d.first == preferred)
            return d.first;
        if (!ScrcpyClient::isNetworkSerial(d.first)) {
            if (usb.isEmpty())
                usb = d.first;
        } else if (wireless.isEmpty()) {
            wireless = d.first;
        }
    }
    return usb.isEmpty() ? wireless : usb;
}

// An iPhone or iPad on USB (Apple's vendor id with an iOS-device product id).
// Linux only, from sysfs; elsewhere it is not worth a USB library.
static bool iosDeviceOnUsb()
{
#ifdef Q_OS_LINUX
    const QDir dir(QStringLiteral("/sys/bus/usb/devices"));
    const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        QFile vendor(dir.filePath(entry + QStringLiteral("/idVendor")));
        if (!vendor.open(QIODevice::ReadOnly) || vendor.readAll().trimmed() != "05ac")
            continue;
        QFile product(dir.filePath(entry + QStringLiteral("/idProduct")));
        if (!product.open(QIODevice::ReadOnly))
            continue;
        bool ok = false;
        const int pid = QString::fromLatin1(product.readAll().trimmed()).toInt(&ok, 16);
        if (ok && pid >= 0x1290 && pid <= 0x12af)
            return true;
    }
#endif
    return false;
}

PhoneMirrorManager::Probe PhoneMirrorManager::probeDevices() const
{
    Probe probe;
    const QString out = runAdbFull({QStringLiteral("devices")}, 10000);
    probe.rows = parseDevices(out);
    probe.state = summarizeState(probe.rows);
    if (out.contains(QLatin1String("doesn't match this client")) || out.contains(QLatin1String("does not match this client")))
        probe.hint = QStringLiteral("Another adb (Android Studio, or a system adb of a different version) keeps "
                                    "restarting the adb server, which drops the phone. Close it, or make both use "
                                    "the same adb.");
    else if (probe.state == QLatin1String("none") && iosDeviceOnUsb())
        probe.hint = QStringLiteral("An iPhone is plugged in. Phone mirroring works with Android phones only.");
    // Names for the picker, only when there is something to pick
    int usable = 0;
    for (const auto &r : std::as_const(probe.rows))
        usable += r.second == QLatin1String("device");
    if (usable > 1) {
        for (const auto &r : std::as_const(probe.rows)) {
            if (r.second != QLatin1String("device"))
                continue;
            probe.models.insert(r.first, runAdbFull({QStringLiteral("-s"), r.first, QStringLiteral("shell"),
                                                     QStringLiteral("getprop"), QStringLiteral("ro.product.model")},
                                                    3000));
        }
    }
    return probe;
}

void PhoneMirrorManager::runProbeJob(std::function<void()> job)
{
    m_probePool.start(std::move(job));
}

void PhoneMirrorManager::refreshDeviceState()
{
    if (m_adbPath.isEmpty()) {
        Probe probe;
        probe.state = QStringLiteral("no-adb");
        applyProbe(probe);
        return;
    }
    if (m_probeRunning) {
        m_probeAgain = true;
        return;
    }
    m_probeRunning = true;
    runProbeJob([this]() {
        const Probe probe = probeDevices();
        QMetaObject::invokeMethod(this, [this, probe]() {
            m_probeRunning = false;
            applyProbe(probe);
            if (m_probeAgain) {
                m_probeAgain = false;
                refreshDeviceState();
            }
        }, Qt::QueuedConnection);
    });
}

void PhoneMirrorManager::applyProbe(const Probe &probe)
{
    // Keep known model names: a probe only fetches them with several phones
    QHash<QString, QString> models = m_deviceModels;
    for (auto it = probe.models.cbegin(); it != probe.models.cend(); ++it)
        if (!it.value().isEmpty())
            models.insert(it.key(), it.value());
    const bool changed = probe.state != m_deviceState || probe.hint != m_deviceHint || probe.rows != m_deviceRows
                         || models != m_deviceModels;
    m_deviceState = probe.state;
    m_deviceHint = probe.hint;
    m_deviceRows = probe.rows;
    m_deviceModels = models;
    if (changed)
        emit deviceStateChanged();
    // Nothing usable: if a Wi-Fi phone is remembered, try to reach it in the
    // background (throttled). The view keeps refreshing, so it is retried for free.
    if (m_deviceState != QLatin1String("device") && m_deviceState != QLatin1String("no-adb"))
        maybeReconnectWireless();
}

QVariantList PhoneMirrorManager::deviceList() const
{
    QVariantList out;
    for (const auto &r : m_deviceRows) {
        out.append(QVariantMap{
            {QStringLiteral("serial"), r.first},
            {QStringLiteral("state"), r.second},
            {QStringLiteral("wireless"), ScrcpyClient::isNetworkSerial(r.first)},
            {QStringLiteral("model"), m_deviceModels.value(r.first)},
            {QStringLiteral("selected"), isRunning() && r.first == m_serial},
        });
    }
    return out;
}

void PhoneMirrorManager::selectDevice(const QString &serial)
{
    if (serial.isEmpty())
        return;
    m_preferredSerial = serial;
    qCInfo(lcPhoneMirror) << "Phone picked:" << serial;
    if (serial == m_serial)
        return;
    m_persistScid.clear();   // never reattach to the other phone's session
    if (isRunning())
        restartSession();
}

// ─── Device probes ────────────────────────────────────────────────────

QString PhoneMirrorManager::getDeviceState()
{
    refreshDeviceState();
    return m_deviceState;
}

QString PhoneMirrorManager::describeDeviceState(const QString &state)
{
    if (state == QLatin1String("no-adb"))
        return QStringLiteral("adb not found: ") + adbInstallHint() + QLatin1Char('.');
    if (state == QLatin1String("unauthorized"))
        return QStringLiteral("Phone is connected but USB debugging is not authorized. "
                              "Unlock the phone and tap 'Allow' on the USB debugging prompt.");
    if (state == QLatin1String("offline"))
        return QStringLiteral("Phone is connected but adb reports it offline. Unplug and replug the cable.");
    if (state == QLatin1String("no-permissions"))
        return QStringLiteral("A phone is plugged in, but this computer's USB permissions keep adb from opening it. "
                              "Install the udev rules (packaging/linux/51-android.rules, see the wiki) or add your "
                              "user to the plugdev group, then replug the phone.");
    if (state == QLatin1String("none"))
        return QStringLiteral("No Android device connected. Connect via USB and enable USB debugging.");
    return {};
}

bool PhoneMirrorManager::hasConnectedDevice()
{
    return getDeviceState() == QLatin1String("device");
}

QString PhoneMirrorManager::describeProblem(const QString &state, const QList<QPair<QString, QString>> &rows) const
{
    // describeDeviceState() with the Wi-Fi case spelled out. The phrases the
    // view keys its auto-retry on (No Android device / not authorized /
    // offline / USB permissions) are kept.
    bool wireless = false;
    if (state == QLatin1String("offline") || state == QLatin1String("unauthorized")) {
        wireless = true;
        for (const auto &d : rows)
            if (d.second == state && !ScrcpyClient::isNetworkSerial(d.first))
                wireless = false;
    }
    if (wireless && state == QLatin1String("offline"))
        return QStringLiteral("The phone's Wi-Fi link is offline. Check that it is on the same Wi-Fi network "
                              "as OCTAVE and that Wireless debugging is on.");
    if (wireless && state == QLatin1String("unauthorized"))
        return QStringLiteral("Phone is connected over Wi-Fi but debugging is not authorized. "
                              "Unlock the phone and tap 'Allow' on the debugging prompt.");
    if (state == QLatin1String("none") && !m_wirelessAddress.isEmpty())
        return QStringLiteral("No Android device connected. Waiting for %1 over Wi-Fi: turn on Wireless "
                              "debugging on the phone (same Wi-Fi network as OCTAVE), or plug it in over USB.")
            .arg(m_wirelessAddress);
    return describeDeviceState(state);
}

void PhoneMirrorManager::killStaleServer()
{
    // A crashed session can leave the device-side server running
    if (!m_adbPath.isEmpty()) {
        QProcess proc;
        proc.setProgram(m_adbPath);
        QStringList args;
        if (!m_serial.isEmpty())
            args << QStringLiteral("-s") << m_serial;
        args << QStringLiteral("shell") << QStringLiteral("pkill")
             << QStringLiteral("-f") << QLatin1String(ScrcpyClient::kServerProcessPattern);
        proc.setArguments(args);
        ScrcpyClient::hideConsoleWindow(&proc);
        proc.startDetached();
    }
}

// ─── Settings ─────────────────────────────────────────────────────────

void PhoneMirrorManager::setVolume(float volume)
{
    m_volume = volume;
    if (m_client)
        m_client->setVolume(m_muted ? 0.0f : volume);
}

void PhoneMirrorManager::setMuted(bool muted)
{
    m_muted = muted;
    if (m_client)
        m_client->setVolume(muted ? 0.0f : m_volume);
}

void PhoneMirrorManager::setAudioGain(float gain)
{
    m_audioGain = qBound(0.25f, gain, 8.0f);
    if (m_client)
        m_client->setAudioGain(m_audioGain);
}

bool PhoneMirrorManager::audioActive() const
{
    return m_client && m_client->audioActive();
}

bool PhoneMirrorManager::audioPlaying() const
{
    return m_audioPlaying;
}

float PhoneMirrorManager::duckingFactor() const
{
    return m_duckFactor;
}

void PhoneMirrorManager::setAudioDuckEnabled(bool enabled)
{
    if (enabled == m_duckEnabled)
        return;
    m_duckEnabled = enabled;
    updateDucking();
}

void PhoneMirrorManager::setAudioDuckLevel(float level)
{
    m_duckLevel = qBound(0.0f, level, 1.0f);
    updateDucking();
}

void PhoneMirrorManager::updateDucking()
{
    const float factor = (m_duckEnabled && m_audioPlaying) ? m_duckLevel : 1.0f;
    if (qFuzzyCompare(factor, m_duckFactor))
        return;
    m_duckFactor = factor;
    qCDebug(lcPhoneMirror) << "ducking factor" << factor;
    emit duckingChanged(factor);
}

void PhoneMirrorManager::restartSession()
{
    if (isRunning()) {
        stopScrcpy();
        QTimer::singleShot(300, this, &PhoneMirrorManager::startScrcpy);
    }
}

QString PhoneMirrorManager::normalizeDisplaySize(const QString &value)
{
    if (value.trimmed().compare(kAutoDisplaySize, Qt::CaseInsensitive) == 0)
        return kAutoDisplaySize;
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d+)\\s*[xX]\\s*(\\d+)\\s*$"));
    const auto m = re.match(value);
    if (!m.hasMatch())
        return {};
    int w = m.captured(1).toInt();
    int h = m.captured(2).toInt();
    w = qMax(8, (w / 8) * 8);
    h = qMax(8, (h / 8) * 8);
    return QStringLiteral("%1x%2").arg(w).arg(h);
}

void PhoneMirrorManager::setDisplaySize(const QString &size)
{
    const QString norm = normalizeDisplaySize(size);
    if (norm != size.trimmed())
        qCInfo(lcPhoneMirror) << "Display size" << size << "normalized to" << norm
                              << "(scrcpy rounds to multiples of 8)";
    if (norm == m_displaySize)
        return;
    m_displaySize = norm;
    emit displaySizeChanged();
    restartSession();
}

QString PhoneMirrorManager::autoDisplaySize(int viewWidth, int viewHeight)
{
    if (viewWidth < 64 || viewHeight < 64)
        return kDefaultDisplaySize;
    const double scale = qMin(1.0, double(kAutoMaxSide) / qMax(viewWidth, viewHeight));
    return normalizeDisplaySize(QStringLiteral("%1x%2").arg(qRound(viewWidth * scale)).arg(qRound(viewHeight * scale)));
}

QString PhoneMirrorManager::requestedDisplay() const
{
    QString size = m_displaySize == kAutoDisplaySize ? autoDisplaySize(m_viewWidth, m_viewHeight) : m_displaySize;
    if (!size.isEmpty() && m_displayDpi > 0)
        size += QLatin1Char('/') + QString::number(m_displayDpi);
    return size;
}

void PhoneMirrorManager::setViewSize(int width, int height)
{
    if (width == m_viewWidth && height == m_viewHeight)
        return;
    m_viewWidth = width;
    m_viewHeight = height;
    if (isRunning() && m_displaySize == kAutoDisplaySize)
        m_resizeTimer.start();
}

void PhoneMirrorManager::setDisplayDpi(int dpi)
{
    dpi = qMax(0, dpi);
    if (dpi == m_displayDpi)
        return;
    m_displayDpi = dpi;
    if (!m_displaySize.isEmpty())
        restartSession();
}

void PhoneMirrorManager::setBitRateUsb(int mbps)
{
    mbps = qBound(2, mbps, 40);
    if (mbps == m_bitRateUsbMbps)
        return;
    m_bitRateUsbMbps = mbps;
    if (m_connectionType == QLatin1String("usb"))
        restartSession();
}

void PhoneMirrorManager::setBitRateWifi(int mbps)
{
    mbps = qBound(2, mbps, 40);
    if (mbps == m_bitRateWifiMbps)
        return;
    m_bitRateWifiMbps = mbps;
    if (m_connectionType == QLatin1String("wifi"))
        restartSession();
}

void PhoneMirrorManager::setBitRateAuto(bool enabled)
{
    if (enabled == m_bitRateAuto)
        return;
    m_bitRateAuto = enabled;
    restartSession();
}

void PhoneMirrorManager::setAutoWifi(bool enabled)
{
    m_autoWifi = enabled;
}

void PhoneMirrorManager::setVideoSink(QObject *sink)
{
    m_videoSink = sink;
    if (m_client)
        m_client->setVideoSink(sink);
    emit videoSinkChanged();
}

// ─── Input ────────────────────────────────────────────────────────────

void PhoneMirrorManager::injectTouch(int pointerId, int action, float relX, float relY)
{
    if (!m_client || !m_client->isRunning())
        return;
    m_client->injectTouch(pointerId, action,
                          int(relX * m_client->frameWidth()), int(relY * m_client->frameHeight()));
}

void PhoneMirrorManager::injectKey(int keycode)
{
    if (m_client && m_client->isRunning())
        m_client->pressKey(keycode);
}

void PhoneMirrorManager::pressHome()
{
    // HOME is a system key that Android routes to the default display, so on
    // a virtual display it does nothing; launch the launcher there instead.
    if (m_vdisplayId >= 0 && !m_serial.isEmpty() && !m_adbPath.isEmpty()) {
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
        ScrcpyClient::hideConsoleWindow(proc);
        proc->start(m_adbPath, {QStringLiteral("-s"), m_serial, QStringLiteral("shell"), QStringLiteral("am"),
                                QStringLiteral("start"), QStringLiteral("--display"), QString::number(m_vdisplayId),
                                QStringLiteral("-a"), QStringLiteral("android.intent.action.MAIN"),
                                QStringLiteral("-c"), QStringLiteral("android.intent.category.HOME")});
        return;
    }
    injectKey(ScrcpyClient::KeycodeHome);
}
void PhoneMirrorManager::pressBack()      { injectKey(ScrcpyClient::KeycodeBack); }
void PhoneMirrorManager::pressAppSwitch() { injectKey(ScrcpyClient::KeycodeAppSwitch); }

// ─── Session lifecycle ────────────────────────────────────────────────

bool PhoneMirrorManager::isRunning() const
{
    return m_client && m_client->isRunning();
}

void PhoneMirrorManager::startScrcpy()
{
    if (isRunning()) {
        if (m_ready)
            emit scrcpyStarted(1);
        return;
    }
    if (m_isStarting)
        return;
    if (!nativeAvailable()) {
        emit scrcpyError(getInstallInstructions());
        return;
    }
    // Everything that talks to adb runs on the probe thread: a dead Wi-Fi
    // link, an adb server restart or a box64 adb must never freeze the dash.
    m_isStarting = true;
    const int gen = ++m_startGeneration;
    QString attachScid;
    if (!m_persistScid.isEmpty() && QDateTime::currentMSecsSinceEpoch() < m_persistUntilMs)
        attachScid = m_persistScid;
    const QString preferred = m_preferredSerial;
    const QString persistPhoneId = m_persistPhoneId;
    runProbeJob([this, gen, attachScid, persistPhoneId, preferred]() {
        StartPrep prep;
        prep.probe = probeDevices();
        prep.serial = pickSerial(prep.probe.rows, preferred);
        if (!prep.serial.isEmpty()) {
            const QStringList shell{QStringLiteral("-s"), prep.serial, QStringLiteral("shell")};
            prep.phoneId = runAdbFull(shell + QStringList{QStringLiteral("getprop"),
                                                          QStringLiteral("ro.serialno")}, 5000).trimmed();
            // Clear a stale server first, unless this is the phone whose
            // session we are about to reattach to
            if (attachScid.isEmpty() || !samePhone(prep.phoneId, persistPhoneId))
                runAdbFull(shell + QStringList{QStringLiteral("pkill"), QStringLiteral("-f"),
                                               QLatin1String(ScrcpyClient::kServerProcessPattern)}, 5000);
            bool ok = false;
            const int sdk = runAdbFull(shell + QStringList{QStringLiteral("getprop"),
                                                           QStringLiteral("ro.build.version.sdk")}, 5000)
                                .trimmed().toInt(&ok);
            prep.sdk = ok ? sdk : 0;
        }
        QMetaObject::invokeMethod(this, [this, prep, gen]() { continueStart(prep, gen); }, Qt::QueuedConnection);
    });
}

void PhoneMirrorManager::continueStart(const StartPrep &prep, int generation)
{
    if (generation != m_startGeneration)
        return;   // stopped (or restarted) meanwhile
    // applyProbe emits deviceStateChanged, and the view answers a phone that
    // is back by calling startScrcpy(): keep m_isStarting set until after it,
    // or that call starts a second session over this one
    applyProbe(prep.probe);
    m_isStarting = false;
    if (prep.serial.isEmpty()) {
        emit scrcpyError(describeProblem(prep.probe.state, prep.probe.rows));
        return;
    }
    const QString serial = prep.serial;
    const bool wireless = ScrcpyClient::isNetworkSerial(serial);
    if (!wireless && m_autoWifi && !m_tcpipChecked) {
        // Off the GUI thread: `adb tcpip` restarts the phone's adbd, which takes
        // a couple of seconds. The session starts once it is back.
        m_isStarting = true;
        const int gen = ++m_startGeneration;
        runWirelessJob([this, serial, gen]() {
            const QString address = enableTcpip(serial);
            QMetaObject::invokeMethod(this, [this, address, gen]() {
                if (!address.isEmpty() && address != m_wirelessAddress) {
                    qCInfo(lcPhoneMirror) << "Remembering the phone for Wi-Fi at" << address;
                    m_wirelessAddress = address;
                    emit wirelessAddressChanged(address);
                }
                if (gen != m_startGeneration)
                    return;   // stopped meanwhile
                m_isStarting = false;
                m_tcpipChecked = true;
                startScrcpy();
            }, Qt::QueuedConnection);
        }, false);
        return;
    }
    m_tcpipChecked = false;
    const QString jar = ScrcpyClient::bundledServerJar();
    if (jar.isEmpty()) {
        emit scrcpyError(QStringLiteral("Bundled scrcpy server could not be extracted"));
        return;
    }

    m_isStarting = true;
    m_isStopping = false;
    m_ready = false;
    m_fatalCause.clear();
    m_sessionSdk = prep.sdk;
    clearNotices();
    const QString requested = requestedDisplay();
    m_activeDisplaySize = requested.section(QLatin1Char('/'), 0, 0);
    m_startedAutoSize = m_displaySize == kAutoDisplaySize ? m_activeDisplaySize : QString();
    QString attachScid;
    // Any transport to the same phone may reattach: a cable pull fails over to
    // Wi-Fi (a different serial) and keeps the virtual display and its apps.
    // Picking another phone clears m_persistScid (selectDevice), and a
    // different phone picked automatically is caught by its ro.serialno.
    if (!m_persistScid.isEmpty() && QDateTime::currentMSecsSinceEpoch() < m_persistUntilMs) {
        if (samePhone(prep.phoneId, m_persistPhoneId))
            attachScid = m_persistScid;
        else
            qCInfo(lcPhoneMirror) << "Not reattaching: the persisted session belongs to another phone";
    }
    m_sessionPhoneId = prep.phoneId;
    m_attaching = !attachScid.isEmpty();
    if (!m_attaching)
        m_persistScid.clear();

    if (m_client) {
        m_client->stop();
        m_client->deleteLater();
    }
    m_client = new ScrcpyClient(m_adbPath, jar, this);
    m_client->setVideoSink(m_videoSink);
    connect(m_client, &ScrcpyClient::connected, this, &PhoneMirrorManager::onConnected, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::disconnected, this, &PhoneMirrorManager::onDisconnected, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::frameSizeChanged, this, [this](int w, int h) {
        if (w != m_frameWidth || h != m_frameHeight) {
            m_frameWidth = w; m_frameHeight = h;
            emit frameSizeChanged(w, h);
        }
    }, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::frameReady, this, &PhoneMirrorManager::frameReady, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::serverLog, this, &PhoneMirrorManager::onServerLog, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::phoneStateChanged, this, &PhoneMirrorManager::onPhoneState, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::mediaStateChanged, this, &PhoneMirrorManager::onMediaState, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::mediaArtChanged, this, &PhoneMirrorManager::onMediaArt, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::mediaQueueChanged, this, &PhoneMirrorManager::onMediaQueue, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::mediaQueueArt, this, &PhoneMirrorManager::onMediaQueueArt, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::bitRateChanged, this, [this](int bps) {
        if (bps != m_currentBitRate) {
            m_currentBitRate = bps;
            emit currentBitRateChanged();
        }
    }, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::audioStateChanged, this, &PhoneMirrorManager::audioActiveChanged, Qt::QueuedConnection);
    connect(m_client, &ScrcpyClient::audioPlayed, this, &PhoneMirrorManager::audioPlayed);
    connect(m_client, &ScrcpyClient::audioPlayingChanged, this, [this](bool playing) {
        if (playing == m_audioPlaying)
            return;
        m_audioPlaying = playing;
        emit audioPlayingChanged(playing);
        updateDucking();
    });
    m_client->setAudioGain(m_audioGain);
    m_client->setVolume(m_muted ? 0.0f : m_volume);

    const int sdk = prep.sdk;
    QString displaySize = requested;
    if (!displaySize.isEmpty() && sdk > 0 && sdk < kNewDisplayMinSdk) {
        qCWarning(lcPhoneMirror) << "Device SDK" << sdk << "<" << kNewDisplayMinSdk
                                 << ": virtual display unavailable, mirroring phone screen";
        displaySize.clear();
        m_activeDisplaySize.clear();
        m_startedAutoSize.clear();
    }
    if (sdk > 0 && sdk < kAudioMinSdk)
        addNotice(noticeText(QStringLiteral("no-audio")));
    if (!m_softwareEncoderSerial.isEmpty() && m_softwareEncoderSerial == serial) {
        m_client->setVideoEncoder(softwareEncoderFor(sdk));
        addNotice(noticeText(QStringLiteral("software-encoder")));
    }
    const int bitRateMbps = wireless ? m_bitRateWifiMbps : m_bitRateUsbMbps;
    int startBps = bitRateMbps * 1000000;
    if (m_bitRateAuto) {
        // The setting is the ceiling; Wi-Fi starts below it and climbs while the link keeps up
        m_client->setAdaptiveBitRate(kAdaptiveMinBps, startBps);
        if (wireless)
            startBps = qMax(kAdaptiveMinBps, int(startBps * kAdaptiveWifiStart));
    }
    qCInfo(lcPhoneMirror) << "Starting phone mirror (server" << serverVersion() << ") for" << serial
                          << "(display" << (displaySize.isEmpty() ? QStringLiteral("phone screen") : displaySize)
                          << "," << startBps / 1000000.0 << "Mbps" << (m_bitRateAuto ? "adaptive, max" : "fixed")
                          << bitRateMbps
                          << "," << (wireless ? "Wi-Fi" : "USB") << ")";
    m_serial = serial;
    setConnectionType(wireless ? QStringLiteral("wifi") : QStringLiteral("usb"));
    if (!m_attaching)
        m_vdisplayId = -1;
    else
        qCInfo(lcPhoneMirror) << "Reattaching to the phone's running mirror session (scid" << attachScid << ")";
    // Phone audio always plays through OCTAVE while mirroring
    m_client->start(serial, displaySize, 60, startBps, true, true, attachScid);
    emit isRunningChanged();
    emit deviceStateChanged();   // "selected" in devices follows m_serial
}

// ── session notices (shown over the mirror; the session keeps running) ──

QString PhoneMirrorManager::classifyServerLine(const QString &line)
{
    if (line.contains(QLatin1String("INJECT_EVENTS")))
        return QStringLiteral("inject");
    if (line.contains(QLatin1String("Audio disabled")) && line.contains(QLatin1String("before Android 11")))
        return QStringLiteral("no-audio");
    if (line.contains(QLatin1String("Capture/encoding error")) || line.contains(QLatin1String("Video encoding error"))
        || line.contains(QLatin1String("Could not create default video encoder"))
        || line.contains(QLatin1String("Could not create video encoder"))
        || (line.contains(QLatin1String("Video encoder '")) && line.contains(QLatin1String("not found"))))
        return QStringLiteral("encoder");
    return {};
}

QString PhoneMirrorManager::softwareEncoderFor(int sdk)
{
    // Google's software AVC encoder: Codec2 since Android 10, OMX before
    return sdk >= 29 || sdk <= 0 ? QStringLiteral("c2.android.avc.encoder") : QStringLiteral("OMX.google.h264.encoder");
}

QString PhoneMirrorManager::noticeText(const QString &kind)
{
    if (kind == QLatin1String("inject"))
        return QStringLiteral("The phone is blocking touch input from OCTAVE. On Xiaomi / MIUI phones, turn on "
                              "\"USB debugging (Security settings)\" in Developer options and restart the phone.");
    if (kind == QLatin1String("no-audio"))
        return QStringLiteral("This phone runs Android 10 or older, which cannot send its sound to OCTAVE. "
                              "Audio keeps playing on the phone.");
    if (kind == QLatin1String("software-encoder"))
        return QStringLiteral("The phone's hardware video encoder failed, so OCTAVE switched to its software "
                              "encoder. The picture may be less smooth.");
    return {};
}

void PhoneMirrorManager::addNotice(const QString &notice)
{
    if (notice.isEmpty() || m_notices.contains(notice))
        return;
    m_notices.append(notice);
    emit mirrorNoticeChanged();
}

void PhoneMirrorManager::clearNotices()
{
    if (m_notices.isEmpty())
        return;
    m_notices.clear();
    emit mirrorNoticeChanged();
}

void PhoneMirrorManager::dismissNotice()
{
    clearNotices();
}

void PhoneMirrorManager::onConnected(int w, int h)
{
    m_ready = true;
    m_staleRetried = false;
    m_isStarting = false;
    m_attaching = false;
    m_persistScid.clear();
    m_frameWidth = w; m_frameHeight = h;
    if (!m_activeDisplaySize.isEmpty())
        m_activeDisplaySize = QStringLiteral("%1x%2").arg(w).arg(h);
    emit frameSizeChanged(w, h);
    qCInfo(lcPhoneMirror) << "Phone mirror connected:" << w << "x" << h;
    emit scrcpyStarted(1);
    // Hand the sleep/panel policy to the phone-side keeper. The server
    // powers the device on asynchronously at start; give it a moment.
    QTimer::singleShot(500, this, &PhoneMirrorManager::sendKeeperPolicy);
    // Follow the phone's media session so OCTAVE's media controls can drive it
    m_client->mediaCommand(ScrcpyClient::MediaWatch, 1);
}

void PhoneMirrorManager::onDisconnected(const QString &disconnectReason)
{
    if (m_isStopping)
        return;
    QString reason = disconnectReason;
    const bool encoderFailed = m_fatalCause == QLatin1String("encoder")
                               && QDateTime::currentMSecsSinceEpoch() - m_fatalCauseAtMs < kFatalCauseWindowMs;
    m_fatalCause.clear();
    if (encoderFailed) {
        // The server is gone with its virtual display: nothing to reattach to
        m_persistScid.clear();
        if (m_softwareEncoderSerial != m_serial) {
            // Hardware encoder failed: once more with Google's software encoder
            qCWarning(lcPhoneMirror) << "The phone's video encoder failed; retrying with"
                                     << softwareEncoderFor(m_sessionSdk);
            m_softwareEncoderSerial = m_serial;
            resetPhoneState();
            resetMedia();
            m_ready = false;
            m_isStarting = false;
            m_attaching = false;
            setConnectionType(QString());
            emit isRunningChanged();
            QTimer::singleShot(300, this, &PhoneMirrorManager::startScrcpy);
            return;
        }
        // Not a link problem, so not something the view should keep retrying
        reason = QStringLiteral("The phone's video encoder failed, even in software mode. Try a smaller virtual "
                                "display size (Settings > Accessories > Phone Mirror), or mirror the phone screen.");
    }
    resetPhoneState();
    resetMedia();
    const bool wasReady = m_ready;
    m_ready = false;
    m_isStarting = false;
    if (m_attaching) {
        // The persisted server was gone (or the link is still down): start fresh
        m_attaching = false;
        m_persistScid.clear();
        qCInfo(lcPhoneMirror) << "Reattach failed (" << reason << "); starting a new session";
        emit isRunningChanged();
        QTimer::singleShot(0, this, &PhoneMirrorManager::startScrcpy);
        return;
    }
    if (wasReady && !m_activeDisplaySize.isEmpty() && m_client && !m_client->scid().isEmpty()) {
        // Link drop mid-stream: the server keeps the virtual display for kPersistMs
        m_persistScid = m_client->scid();
        m_persistPhoneId = m_sessionPhoneId;
        m_persistUntilMs = QDateTime::currentMSecsSinceEpoch() + ScrcpyClient::kPersistMs - 5000;
    }
    setConnectionType(QString());
    if (m_currentBitRate != 0) {
        m_currentBitRate = 0;
        emit currentBitRateChanged();
    }
    if (!wasReady && !reason.isEmpty() && !m_staleRetried && m_serial.contains(QLatin1Char(':'))) {
        // Wireless debugging takes a new port on every Wi-Fi rejoin, but adb
        // keeps listing the old ip:port as a device until it notices it is
        // dead, so the start can pick a stale entry. Drop it, look up the
        // current port over mDNS and start once more.
        m_staleRetried = true;
        m_isStarting = true;
        const QString stale = m_serial;
        const int gen = ++m_startGeneration;
        qCInfo(lcPhoneMirror) << "Start over Wi-Fi failed (" << reason << "); dropping" << stale << "and retrying";
        runWirelessJob([this, stale, gen]() {
            runAdbFull({QStringLiteral("disconnect"), stale}, 5000);
            const QString fresh = mdnsConnectAddressFor(hostOf(stale));
            const QString reached = !fresh.isEmpty() && adbConnect(fresh, nullptr) ? fresh : QString();
            QMetaObject::invokeMethod(this, [this, stale, reached, gen]() {
                if (gen != m_startGeneration)
                    return;   // stopped meanwhile
                m_isStarting = false;
                if (!reached.isEmpty() && reached != stale && m_wirelessAddress == stale) {
                    m_wirelessAddress = reached;
                    emit wirelessAddressChanged(reached);
                }
                startScrcpy();
            }, Qt::QueuedConnection);
        }, false);
        return;
    }
    m_staleRetried = false;
    emit isRunningChanged();
    if (reason.isEmpty())
        emit scrcpyStopped();
    else
        emit scrcpyError(reason);
}

void PhoneMirrorManager::stopScrcpy()
{
    qCDebug(lcPhoneMirror) << "Stopping phone mirror";
    ++m_startGeneration;   // a pending auto-Wi-Fi step does not start a session
    m_isStopping = true;
    m_isStarting = false;
    m_ready = false;
    m_attaching = false;
    m_staleRetried = false;
    m_persistScid.clear();
    m_activeDisplaySize.clear();
    m_startedAutoSize.clear();
    m_resizeTimer.stop();
    if (m_currentBitRate != 0) {
        m_currentBitRate = 0;
        emit currentBitRateChanged();
    }
    resetPhoneState();
    resetMedia();
    if (m_client) {
        ScrcpyClient *client = m_client;
        m_client = nullptr;
        client->stop();
        client->deleteLater();
        killStaleServer();
    }
    setConnectionType(QString());
    emit scrcpyStopped();
    emit isRunningChanged();
}

// ── phone screen / sleep ──────────────────────────────────────────────
// The phone-side MirrorKeeper (phone_server/.../control/MirrorKeeper.java)
// does the work: it watches the phone's power state and keyguard from inside
// the device, wakes it on a power press (the virtual display has no power
// group of its own and dozes with the phone), blanks the panel per the
// setting, and decides "in use" from the keyguard being dismissed. OCTAVE
// only sets the policy, takes the phone back, and shows the state.

// After a power press from the locked state the keeper wakes the phone but
// leaves its panel lit for this long; a keyguard dismissal in that window
// means the user wants their phone, otherwise the panel is blanked again.
static constexpr int kWakeGraceMs = 8000;

void PhoneMirrorManager::setPhoneScreenOff(bool off)
{
    if (off == m_phoneScreenOff)
        return;
    m_phoneScreenOff = off;
    sendKeeperPolicy();
}

void PhoneMirrorManager::sendKeeperPolicy()
{
    if (m_client && m_client->isRunning() && m_ready)
        m_client->setKeeper(true, m_phoneScreenOff, kWakeGraceMs);
}

void PhoneMirrorManager::wakePhone()
{
    if (m_serial.isEmpty() || m_adbPath.isEmpty())
        return;
    qCInfo(lcPhoneMirror) << "Waking phone";
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
    ScrcpyClient::hideConsoleWindow(proc);
    proc->start(m_adbPath, {QStringLiteral("-s"), m_serial, QStringLiteral("shell"), QStringLiteral("input"),
                            QStringLiteral("keyevent"), QString::number(ScrcpyClient::KeycodeWakeup)});
}

void PhoneMirrorManager::resumeMirroring()
{
    if (m_client && m_client->isRunning())
        m_client->takeBack();
}

void PhoneMirrorManager::onPhoneState(bool asleep, bool inUse, bool panelDark)
{
    if (asleep != m_phoneAsleep) {
        m_phoneAsleep = asleep;
        if (m_client)
            m_client->setHoldBlack(asleep);   // keep the last good frame on the dash
        qCInfo(lcPhoneMirror) << (asleep ? "Phone went to sleep; the keeper is waking it" : "Phone is awake again");
        emit phoneAsleepChanged(asleep);
    }
    if (inUse != m_phoneInUse) {
        m_phoneInUse = inUse;
        qCInfo(lcPhoneMirror) << (inUse ? "Phone unlocked by the user; leaving its screen alone"
                                        : "Phone handed back to the mirror");
        emit phoneInUseChanged(inUse);
    }
    m_panelDark = panelDark;
}

void PhoneMirrorManager::resetPhoneState()
{
    if (m_phoneInUse) {
        m_phoneInUse = false;
        emit phoneInUseChanged(false);
    }
    if (m_phoneAsleep) {
        m_phoneAsleep = false;
        emit phoneAsleepChanged(false);
    }
    m_panelDark = false;
}

void PhoneMirrorManager::onServerLog(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("New display: .*\\(id=(\\d+)\\)"));
    const auto m = re.match(line);
    if (m.hasMatch())
        m_vdisplayId = m.captured(1).toInt();
    const QString kind = classifyServerLine(line);
    if (kind == QLatin1String("encoder")) {
        // The server retries (smaller size) on its own; this only decides
        // what a disconnect right after it means
        m_fatalCause = kind;
        m_fatalCauseAtMs = QDateTime::currentMSecsSinceEpoch();
    } else if (!kind.isEmpty()) {
        addNotice(noticeText(kind));
    }
}

void PhoneMirrorManager::cleanup()
{
    stopScrcpy();
}

// ── Wi-Fi ─────────────────────────────────────────────────────────────
// adb itself carries the session over TCP: once the phone is listed by
// `adb devices` as "ip:port" (adb connect) or as an mDNS name (adb
// auto-connects phones it has paired with), the client's `adb -s <serial>`
// push / forward / shell work exactly as over USB. This code only gets the
// phone into that list and keeps it there.

void PhoneMirrorManager::setConnectionType(const QString &type)
{
    if (type == m_connectionType)
        return;
    m_connectionType = type;
    emit connectionTypeChanged();
}

QString PhoneMirrorManager::hostOf(const QString &address)
{
    QString host = address.trimmed();
    const int colon = host.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && host.indexOf(QLatin1Char(':')) == colon)
        host = host.left(colon);
    else if (host.startsWith(QLatin1Char('[')) && host.contains(QLatin1String("]:")))
        host = host.mid(1, host.indexOf(QLatin1String("]:")) - 1);
    return host;
}

QList<PhoneMirrorManager::MdnsService> PhoneMirrorManager::mdnsServices() const
{
    // "adb-R5CT…-AbCdEf  _adb-tls-connect._tcp  192.168.1.5:37199" per line
    QList<MdnsService> out;
    const QStringList lines = runAdbFull({QStringLiteral("mdns"), QStringLiteral("services")}, 5000)
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    for (const QString &line : lines) {
        const QStringList parts = line.trimmed().split(ws, Qt::SkipEmptyParts);
        if (parts.size() < 3 || !parts[1].startsWith(QLatin1String("_adb")))
            continue;
        out.append({parts[0], parts[1], parts[2]});
    }
    if (out.isEmpty())
        out = avahiMdnsServices();
    return out;
}

QList<PhoneMirrorManager::MdnsService> PhoneMirrorManager::avahiMdnsServices()
{
    // Debian/Ubuntu's arm64 adb is built without working mDNS (`adb mdns
    // services` is always empty), and Google ships platform-tools for x86-64
    // only, so on a Pi the phone is found through the system's Avahi instead.
    // Parsable resolved lines look like
    // "=;ap0;IPv4;adb-R3GL…-51sbEx;_adb-tls-connect._tcp;local;Android.local;10.42.0.30;36937;…"
    QList<MdnsService> out;
    const QString avahi = QStandardPaths::findExecutable(QStringLiteral("avahi-browse"));
    if (avahi.isEmpty())
        return out;
    for (const QString &type : {QStringLiteral("_adb-tls-connect._tcp"), QStringLiteral("_adb-tls-pairing._tcp")}) {
        QProcess proc;
        proc.start(avahi, {QStringLiteral("-rpt"), type});
        if (!proc.waitForFinished(4000)) {
            proc.kill();
            proc.waitForFinished(1000);
        }
        const QStringList lines = QString::fromUtf8(proc.readAllStandardOutput()).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QStringList f = line.split(QLatin1Char(';'));
            // IPv6 answers are link-local (need a scope id) and repeat the IPv4 ones
            if (f.size() < 9 || f[0] != QLatin1String("=") || f[2] != QLatin1String("IPv4"))
                continue;
            const MdnsService s{f[3], f[4], f[7] + QLatin1Char(':') + f[8]};
            const bool seen = std::any_of(out.cbegin(), out.cend(), [&](const MdnsService &o) {
                return o.name == s.name && o.type == s.type && o.address == s.address;
            });
            if (!seen)
                out.append(s);
        }
    }
    return out;
}

QString PhoneMirrorManager::mdnsConnectAddressFor(const QString &host) const
{
    for (const MdnsService &s : mdnsServices())
        if (s.type.contains(QLatin1String("_adb-tls-connect")) && hostOf(s.address) == host)
            return s.address;
    return {};
}

bool PhoneMirrorManager::adbConnect(const QString &address, QString *message) const
{
    const QString out = runAdbFull({QStringLiteral("connect"), address}, 10000);
    // adb connect exits 0 on failure too; "already connected to" counts as success
    const bool ok = out.contains(QLatin1String("connected to")) && !out.contains(QLatin1String("failed"))
                    && !out.contains(QLatin1String("cannot"));
    if (message)
        *message = out.isEmpty() ? QStringLiteral("No answer from %1").arg(address) : out.section(QLatin1Char('\n'), -1);
    return ok;
}

void PhoneMirrorManager::runWirelessJob(std::function<void()> job, bool ui)
{
    if (ui) {
        if (m_wirelessJobs++ == 0)
            emit wirelessBusyChanged();
    }
    m_wirelessPool.start([this, job = std::move(job), ui]() {
        job();
        if (ui) {
            QMetaObject::invokeMethod(this, [this]() {
                if (--m_wirelessJobs == 0)
                    emit wirelessBusyChanged();
            }, Qt::QueuedConnection);
        }
    });
}

// ── phone media ───────────────────────────────────────────────────────
// The server's PhoneMedia (phone_server/.../control/PhoneMedia.java) follows
// the phone's active media session and reports it; OCTAVE's media controls
// drive it through ScrcpyClient::mediaCommand while mediaActive is set.

void PhoneMirrorManager::onMediaState(const QVariantMap &state)
{
    const bool wasPlaying = m_mediaPlaying;
    const QString oldTrack = m_mediaApp + QLatin1Char('\n') + m_mediaTitle + QLatin1Char('\n') + m_mediaArtist;
    const bool hadTrack = m_mediaReported;
    m_mediaAvailable = state.value(QStringLiteral("has_session")).toBool();
    m_mediaPlaying = m_mediaAvailable && state.value(QStringLiteral("playing")).toBool();
    m_mediaCanSeek = state.value(QStringLiteral("can_seek")).toBool();
    m_mediaPositionMs = state.value(QStringLiteral("position_ms")).toLongLong();
    m_mediaPositionAtMs = QDateTime::currentMSecsSinceEpoch();
    m_mediaDuration = int(qMin<qint64>(state.value(QStringLiteral("duration_ms")).toLongLong(), INT_MAX));
    m_mediaApp = state.value(QStringLiteral("package")).toString();
    m_mediaTitle = state.value(QStringLiteral("title")).toString();
    m_mediaArtist = state.value(QStringLiteral("artist")).toString();
    m_mediaAlbum = state.value(QStringLiteral("album")).toString();
    emit mediaChanged();
    if (!hadTrack || oldTrack != m_mediaApp + QLatin1Char('\n') + m_mediaTitle + QLatin1Char('\n') + m_mediaArtist)
        emit mediaTrackChanged();
    // The phone is the media source while mirroring: it takes the controls
    // with its first report, and again whenever it starts playing after a
    // local track took them (releaseMedia)
    const bool firstReport = !m_mediaReported;
    m_mediaReported = true;
    if (!m_mediaActive && (firstReport || (m_mediaPlaying && !wasPlaying))) {
        qCInfo(lcPhoneMirror) << "OCTAVE's media controls drive the phone" << (m_mediaApp.isEmpty() ? QString() : m_mediaApp);
        setMediaActive(true);
    }
}

void PhoneMirrorManager::onMediaArt(const QByteArray &jpeg)
{
    QString url;
    if (!jpeg.isEmpty()) {
        const QString dir = SettingsManager::getAppDataDir() + QStringLiteral("/temp");
        QDir().mkpath(dir);
        // Alternate between two files: a new URL makes QML reload the image
        m_mediaArtSeq = (m_mediaArtSeq + 1) % 2;
        const QString path = dir + QStringLiteral("/phone_media_art_%1.jpg").arg(m_mediaArtSeq);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly) && f.write(jpeg) == jpeg.size())
            url = QUrl::fromLocalFile(path).toString();
        else
            qCWarning(lcPhoneMirror) << "Could not write phone album art to" << path;
    }
    if (url != m_mediaArtUrl) {
        m_mediaArtUrl = url;
        emit mediaArtChanged();
    }
}

void PhoneMirrorManager::onMediaQueue(const QVariantList &items, qint64 activeId, const QString &title)
{
    if (items == m_mediaQueueRaw && activeId == m_mediaQueueActiveId && title == m_mediaQueueTitle)
        return;
    m_mediaQueueRaw = items;
    m_mediaQueueActiveId = activeId;
    m_mediaQueueTitle = title;
    publishMediaQueue();
}

static QString queueArtDir()
{
    return SettingsManager::getAppDataDir() + QStringLiteral("/temp/phone_queue_art");
}

void PhoneMirrorManager::onMediaQueueArt(const QString &key, const QByteArray &jpeg)
{
    QString url;
    if (!jpeg.isEmpty()) {
        const QString dir = queueArtDir();
        QDir().mkpath(dir);
        const QString path = dir + QLatin1Char('/')
            + QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex())
            + QStringLiteral(".jpg");
        QFile f(path);
        if (f.open(QIODevice::WriteOnly) && f.write(jpeg) == jpeg.size())
            url = QUrl::fromLocalFile(path).toString();
        else
            qCWarning(lcPhoneMirror) << "Could not write phone queue art to" << path;
    }
    m_queueArt.insert(key, url);
    // Art arrives one image at a time right after a queue: publish once per batch
    m_queueArtTimer.start();
}

void PhoneMirrorManager::publishMediaQueue()
{
    m_queueArtTimer.stop();
    QVariantList items;
    items.reserve(m_mediaQueueRaw.size());
    for (const QVariant &v : std::as_const(m_mediaQueueRaw)) {
        QVariantMap item = v.toMap();
        item.insert(QStringLiteral("art"), m_queueArt.value(item.value(QStringLiteral("art_uri")).toString()));
        items.append(item);
    }
    m_mediaQueue = items;
    emit mediaQueueChanged();
}

void PhoneMirrorManager::resetMedia()
{
    const bool hadTrack = m_mediaReported;
    m_mediaReported = false;
    setMediaActive(false);
    if (m_mediaAvailable || m_mediaPlaying || !m_mediaTitle.isEmpty()) {
        m_mediaAvailable = m_mediaPlaying = m_mediaCanSeek = false;
        m_mediaTitle.clear();
        m_mediaArtist.clear();
        m_mediaAlbum.clear();
        m_mediaApp.clear();
        m_mediaDuration = 0;
        m_mediaPositionMs = 0;
        emit mediaChanged();
    }
    if (!m_mediaArtUrl.isEmpty()) {
        m_mediaArtUrl.clear();
        emit mediaArtChanged();
    }
    onMediaQueue({}, -1, {});
    if (!m_queueArt.isEmpty()) {
        m_queueArt.clear();
        QDir(queueArtDir()).removeRecursively();
    }
    if (hadTrack)
        emit mediaTrackChanged();   // the track is gone (fields already cleared)
}

void PhoneMirrorManager::setMediaActive(bool active)
{
    if (active == m_mediaActive)
        return;
    m_mediaActive = active;
    emit mediaActiveChanged(active);
}

int PhoneMirrorManager::mediaPosition() const
{
    qint64 pos = m_mediaPositionMs;
    if (m_mediaPlaying)
        pos += QDateTime::currentMSecsSinceEpoch() - m_mediaPositionAtMs;
    if (m_mediaDuration > 0)
        pos = qMin<qint64>(pos, m_mediaDuration);
    return int(qMax<qint64>(0, pos));
}

void PhoneMirrorManager::mediaPlayPause()
{
    if (m_client && m_ready)
        m_client->mediaCommand(ScrcpyClient::MediaPlayPause);
}

void PhoneMirrorManager::mediaPlay()
{
    if (m_client && m_ready)
        m_client->mediaCommand(ScrcpyClient::MediaPlay);
}

void PhoneMirrorManager::mediaPause()
{
    if (m_client && m_ready)
        m_client->mediaCommand(ScrcpyClient::MediaPause);
}

void PhoneMirrorManager::mediaNext()
{
    if (m_client && m_ready)
        m_client->mediaCommand(ScrcpyClient::MediaNext);
}

void PhoneMirrorManager::mediaPrevious()
{
    if (m_client && m_ready)
        m_client->mediaCommand(ScrcpyClient::MediaPrevious);
}

void PhoneMirrorManager::mediaSeek(int positionMs)
{
    if (!m_client || !m_ready)
        return;
    m_client->mediaCommand(ScrcpyClient::MediaSeek, qMax(0, positionMs));
    // Show the new position before the phone confirms it
    m_mediaPositionMs = qMax(0, positionMs);
    m_mediaPositionAtMs = QDateTime::currentMSecsSinceEpoch();
}

void PhoneMirrorManager::mediaSkipToQueueItem(qint64 id)
{
    if (m_client && m_ready)
        m_client->mediaCommand(ScrcpyClient::MediaSkipToQueueItem, id);
}

void PhoneMirrorManager::releaseMedia(bool pausePhone)
{
    if (!m_mediaActive)
        return;
    if (pausePhone && m_mediaPlaying)
        mediaPause();
    qCInfo(lcPhoneMirror) << "OCTAVE's media controls leave the phone" << (pausePhone ? "(phone paused)" : "");
    setMediaActive(false);
}

void PhoneMirrorManager::setWirelessAddress(const QString &address)
{
    const QString value = address.trimmed();
    if (value == m_wirelessAddress)
        return;
    m_wirelessAddress = value;
    emit wirelessAddressChanged(value);
    if (!value.isEmpty()) {
        m_lastAutoConnectMs = 0;
        maybeReconnectWireless();
    }
}

void PhoneMirrorManager::maybeReconnectWireless()
{
    static constexpr qint64 kAutoConnectIntervalMs = 5000;
    if (m_wirelessAddress.isEmpty() || m_adbPath.isEmpty() || m_wirelessJobs > 0 || m_autoConnectRunning)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastAutoConnectMs < kAutoConnectIntervalMs)
        return;
    m_lastAutoConnectMs = now;
    m_autoConnectRunning = true;
    const QString saved = m_wirelessAddress;
    runWirelessJob([this, saved]() {
        QString msg;
        bool ok = adbConnect(saved, &msg);
        QString reached = ok ? saved : QString();
        if (!ok) {
            // Wireless debugging picks a new port every time it is switched
            // on; the phone advertises the current one over mDNS.
            const QString fresh = mdnsConnectAddressFor(hostOf(saved));
            if (!fresh.isEmpty() && fresh != saved && adbConnect(fresh, &msg))
                reached = fresh;
        }
        if (!reached.isEmpty())
            qCInfo(lcPhoneMirror) << "Reconnected to the phone over Wi-Fi at" << reached;
        else
            qCDebug(lcPhoneMirror) << "Wi-Fi phone" << saved << "not reachable:" << msg;
        QMetaObject::invokeMethod(this, [this, saved, reached]() {
            m_autoConnectRunning = false;
            // The phone moved to a new port: remember that one
            if (!reached.isEmpty() && reached != saved && m_wirelessAddress == saved) {
                m_wirelessAddress = reached;
                emit wirelessAddressChanged(reached);
            }
        }, Qt::QueuedConnection);
    }, false);
}

QString PhoneMirrorManager::phoneWifiAddress(const QString &serial) const
{
    // "30: wlan0    inet 192.168.4.23/24 brd ... scope global wlan0". Wi-Fi
    // client interfaces first, then a hotspot the phone itself runs; mobile
    // data (rmnet*, ccmni*), VPNs and loopback never reach OCTAVE.
    static const QRegularExpression re(QStringLiteral("^\\d+:\\s+(\\S+)\\s+inet\\s+([0-9.]+)/"));
    const QStringList lines = runAdbFull({QStringLiteral("-s"), serial, QStringLiteral("shell"), QStringLiteral("ip"),
                                          QStringLiteral("-4"), QStringLiteral("-o"), QStringLiteral("addr"),
                                          QStringLiteral("show")}, 5000)
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QString hotspot;
    for (const QString &line : lines) {
        const auto m = re.match(line.trimmed());
        if (!m.hasMatch())
            continue;
        const QString iface = m.captured(1);
        if (iface.startsWith(QLatin1String("wlan")))
            return m.captured(2);
        if (hotspot.isEmpty() && (iface.startsWith(QLatin1String("swlan")) || iface.startsWith(QLatin1String("ap"))
                                  || iface.startsWith(QLatin1String("softap"))))
            hotspot = m.captured(2);
    }
    return hotspot;
}

QString PhoneMirrorManager::enableTcpip(const QString &serial) const
{
    const QString ip = phoneWifiAddress(serial);
    if (ip.isEmpty()) {
        qCInfo(lcPhoneMirror) << "Phone has no Wi-Fi address; not enabling adb over Wi-Fi";
        return {};
    }
    const QString port = QString::number(kTcpipPort);
    const QString current = runAdbFull({QStringLiteral("-s"), serial, QStringLiteral("shell"), QStringLiteral("getprop"),
                                        QStringLiteral("service.adb.tcp.port")}, 5000).trimmed();
    if (current != port) {
        // Restarts adbd on the phone: USB drops for a moment and comes back
        qCInfo(lcPhoneMirror) << "Enabling adb over Wi-Fi on the phone (port" << port << ")";
        int code = -1;
        const QString out = runAdbFull({QStringLiteral("-s"), serial, QStringLiteral("tcpip"), port}, 10000, &code);
        if (code != 0) {
            qCWarning(lcPhoneMirror) << "adb tcpip failed:" << out;
            return {};
        }
        QThread::msleep(1000);
        bool back = false;
        for (int i = 0; i < 16 && !back; ++i) {
            back = runAdbFull({QStringLiteral("-s"), serial, QStringLiteral("get-state")}, 2000).trimmed()
                   == QLatin1String("device");
            if (!back)
                QThread::msleep(500);
        }
        if (!back)
            qCWarning(lcPhoneMirror) << "Phone did not come back on USB after adb tcpip";
    }
    return ip + QLatin1Char(':') + port;
}

QString PhoneMirrorManager::connectPairedHost(const QString &host) const
{
    // The connect port differs from the pairing port; the phone advertises
    // it over mDNS. adb may already have auto-connected (then "already
    // connected" is the answer).
    QString msg;
    for (int i = 0; i < 16; ++i) {
        const QString connectAddr = mdnsConnectAddressFor(host);
        if (!connectAddr.isEmpty() && adbConnect(connectAddr, &msg))
            return connectAddr;
        QThread::msleep(500);
    }
    return {};
}

void PhoneMirrorManager::forgetWireless()
{
    const QString addr = m_wirelessAddress;
    if (m_connectionType == QLatin1String("wifi"))
        stopScrcpy();
    m_wirelessAddress.clear();
    emit wirelessAddressChanged(QString());
    if (addr.isEmpty())
        return;
    qCInfo(lcPhoneMirror) << "Forgetting the Wi-Fi phone at" << addr;
    runWirelessJob([this, addr]() {
        runAdbFull({QStringLiteral("disconnect"), addr}, 5000);
    }, false);
}

// ── QR pairing ──
// Android Studio's flow: the host shows WIFI:T:ADB;S:<name>;P:<password>;;
// the phone's "Pair device with QR code" scanner reads it and advertises an
// _adb-tls-pairing service under <name>; the host then runs
// `adb pair <ip:port> <password>` against it and connects as usual.

QString PhoneMirrorManager::qrPairingPayload(const QString &service, const QString &password)
{
    return QStringLiteral("WIFI:T:ADB;S:%1;P:%2;;").arg(service, password);
}

static QString randomToken(int length)
{
    static const char kChars[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    QString out;
    for (int i = 0; i < length; ++i)
        out += QLatin1Char(kChars[QRandomGenerator::system()->bounded(int(sizeof(kChars) - 1))]);
    return out;
}

void PhoneMirrorManager::startQrPairing()
{
    if (m_adbPath.isEmpty()) {
        emit wirelessResult(false, describeDeviceState(QStringLiteral("no-adb")));
        return;
    }
    if (adbToolsMajor(m_adbPath) < kAdbWirelessMinTools) {
        qCWarning(lcPhoneMirror) << "QR pairing: adb" << m_adbPath << "is too old to pair";
        emit wirelessResult(false, QStringLiteral("adb is too old for wireless pairing (needs platform-tools %1 or "
                                                  "newer). Install a newer adb, or pair over USB.")
                                       .arg(kAdbWirelessMinTools));
        return;
    }
    const QString service = QStringLiteral("octave-") + randomToken(8);
    const QString password = randomToken(12);
    const QByteArray payload = qrPairingPayload(service, password).toUtf8();
    const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(payload.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    QStringList rows;
    for (int y = 0; y < qr.getSize(); ++y) {
        QString row;
        for (int x = 0; x < qr.getSize(); ++x)
            row += qr.getModule(x, y) ? QLatin1Char('1') : QLatin1Char('0');
        rows << row;
    }
    m_qrRows = rows;
    emit qrPairingChanged();
    const int generation = ++m_qrGeneration;
    qCInfo(lcPhoneMirror) << "QR pairing: waiting for the phone to scan (service" << service << ")";

    runWirelessJob([this, service, password, generation]() {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + kQrPairingTimeoutMs;
        QString pairAddr;
        while (pairAddr.isEmpty() && m_qrGeneration == generation
               && QDateTime::currentMSecsSinceEpoch() < deadline) {
            for (const MdnsService &s : mdnsServices())
                if (s.name == service && s.type.contains(QLatin1String("_adb-tls-pairing")))
                    pairAddr = s.address;
            if (pairAddr.isEmpty())
                QThread::msleep(1000);
        }
        if (m_qrGeneration != generation)
            return;   // cancelled or restarted: the newer request reports
        bool ok = false;
        QString reached, detail;
        if (pairAddr.isEmpty()) {
            detail = QStringLiteral("The phone did not scan the code within %1 s.").arg(kQrPairingTimeoutMs / 1000);
        } else {
            const QString out = runAdbFull({QStringLiteral("pair"), pairAddr, password}, 20000);
            if (out.contains(QLatin1String("Successfully paired"))) {
                ok = true;
                reached = connectPairedHost(hostOf(pairAddr));
            } else {
                detail = out.isEmpty() ? QStringLiteral("no answer") : out.section(QLatin1Char('\n'), -1);
            }
        }
        QMetaObject::invokeMethod(this, [this, generation, ok, reached, detail, pairAddr]() {
            if (m_qrGeneration != generation)
                return;
            finishQrPairing();
            if (!ok) {
                qCWarning(lcPhoneMirror) << "QR pairing failed:" << detail;
                emit wirelessResult(false, pairAddr.isEmpty()
                    ? detail + QStringLiteral(" Check that the phone is on the same Wi-Fi network and try again.")
                    : QStringLiteral("Pairing failed. Try again. (%1)").arg(detail));
            } else if (reached.isEmpty()) {
                emit wirelessResult(false, QStringLiteral("Paired, but the phone did not accept a connection. Keep its "
                                                          "Wireless debugging screen open and try again."));
            } else {
                qCInfo(lcPhoneMirror) << "QR pairing: paired and connected over Wi-Fi at" << reached;
                setWirelessAddress(reached);
                emit wirelessResult(true, QStringLiteral("Paired and connected to %1 over Wi-Fi.").arg(reached));
            }
        }, Qt::QueuedConnection);
    }, true);
}

void PhoneMirrorManager::cancelQrPairing()
{
    if (m_qrRows.isEmpty())
        return;
    ++m_qrGeneration;   // the waiting job notices within a second and returns
    finishQrPairing();
}

void PhoneMirrorManager::finishQrPairing()
{
    if (m_qrRows.isEmpty())
        return;
    m_qrRows.clear();
    emit qrPairingChanged();
}

#endif // Q_OS_MOBILE
