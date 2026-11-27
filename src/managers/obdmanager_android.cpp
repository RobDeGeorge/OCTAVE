// OBDManager on Android: its own ELM327 path over Bluetooth LE. Empty on
// every other platform. Parked for a phone: TODO/fix-up-leftovers.md folds it
// into OBDConnectionWorker.
#include "obdmanager.h"
#include "elm327protocol.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

// ===========================================================================
// Android BLE implementation
//
// On Android the QSerialPort/worker-thread flow is bypassed. The GATT link is
// owned by OctaveOBDBridge.java; the main thread polls its state code and
// notification queue on a 50 ms timer. ELM327 init runs as a state machine
// via QTimer between AT commands; PID polling is response-driven (next PID
// requested when the previous response arrives).
// ===========================================================================
#ifdef Q_OS_ANDROID

void OBDManager::logAndroid(const QString &line)
{
    qDebug().noquote() << "[OBD]" << line;
    emit connectionLogLineAppended(line);
    // Mirror to the existing detail text element on the OBD page so the user
    // sees the latest state transition without needing a new QML widget.
    setConnectionDetail(line);
}

QStringList OBDManager::listAndroidPairedDevices()
{
    // BluetoothAdapter.getBondedDevices() through the Java bridge, which
    // answers with an empty list until the runtime BLUETOOTH_CONNECT grant
    // exists (OctaveOBDBridge.connect() requests it on the first Connect
    // tap). Entries are "MAC|name"; names feed the adapter list rows.
    QStringList macs;
    m_androidDeviceNames.clear();
    QJniObject ctx = QNativeInterface::QAndroidApplication::context();
    if (!ctx.isValid())
        return macs;
    QJniObject jArr = QJniObject::callStaticObjectMethod(
        "org/octave/app/OctaveOBDBridge", "bondedDevices",
        "(Landroid/content/Context;)[Ljava/lang/String;", ctx.object<jobject>());
    if (!jArr.isValid())
        return macs;
    QJniEnvironment env;
    jobjectArray arr = jArr.object<jobjectArray>();
    const jsize n = env->GetArrayLength(arr);
    for (jsize i = 0; i < n; ++i) {
        jobject local = env->GetObjectArrayElement(arr, i);
        const QString entry = QJniObject(local).toString();
        env->DeleteLocalRef(local);
        const int sep = entry.indexOf(QLatin1Char('|'));
        const QString mac = (sep < 0 ? entry : entry.left(sep)).trimmed();
        if (mac.isEmpty())
            continue;
        macs.append(mac);
        const QString name = sep < 0 ? QString() : entry.mid(sep + 1).trimmed();
        if (!name.isEmpty())
            m_androidDeviceNames.insert(mac, name);
    }
    return macs;
}

void OBDManager::startAndroidConnection()
{
    QString address = getConfiguredPort();
    // A delayed BLE retry must not take over after Close or a demo selection.
    if (m_forceStopReconnect || OBDConnectionWorker::transportClaims(address))
        return;
    if (!checkPortExists(address)) {
        m_connected = false;
        setConnectionStatus(QStringLiteral("No MAC Address"));
        setConnectionDetail(
            QStringLiteral("Enter your ELM327 MAC in OBD Settings, then tap Connect"));
        setConnectionProgress(0);
        emit devicePresenceChanged(false);
        qDebug() << "[OBD] Android: no/invalid MAC configured:" << address;
        {
            QMutexLocker locker(&m_lock);
            m_isConnecting = false;
        }
        return;
    }

    cleanupAndroidConnection();

    m_btResponseBuffer.clear();
    m_androidElmInitialized = false;
    m_androidInitStep = 0;
    m_androidPolling = false;
    m_androidStaleCount = 0;
    m_androidSupportedPids.clear();
    m_bleLastJavaState = -1;

    setConnectionProgress(20);
    setConnectionStatus(QStringLiteral("Connecting BLE..."));
    logAndroid(QStringLiteral("BLE connect → %1 (via OctaveOBDBridge)").arg(address));

    // Hand off to the Java bridge — bypasses Qt's broken BLE permission gate.
    QJniObject ctx = QNativeInterface::QAndroidApplication::context();
    QJniObject jMac = QJniObject::fromString(address);
    jboolean ok = QJniObject::callStaticMethod<jboolean>(
        "org/octave/app/OctaveOBDBridge",
        "connect",
        "(Landroid/content/Context;Ljava/lang/String;)Z",
        ctx.object<jobject>(),
        jMac.object<jstring>());
    if (!ok) {
        QString err = QJniObject::callStaticObjectMethod(
            "org/octave/app/OctaveOBDBridge", "lastError",
            "()Ljava/lang/String;").toString();
        logAndroid(QStringLiteral("connect() failed: %1").arg(err));
        setConnectionStatus(QStringLiteral("BLE connect failed"));
        setConnectionProgress(0);
        QMutexLocker locker(&m_lock);
        m_isConnecting = false;
        return;
    }

    // Poll the Java bridge for state transitions and notification bytes.
    // 50 ms is fast enough that BLE notifications feel real-time without
    // burning CPU when idle.
    m_bleNotifyPoller.disconnect();
    m_bleNotifyPoller.setInterval(50);
    QObject::connect(&m_bleNotifyPoller, &QTimer::timeout,
                     this, &OBDManager::onAndroidBlePoll);
    m_bleNotifyPoller.start();
}

void OBDManager::cleanupAndroidConnection()
{
    m_androidInitTimer.stop();
    m_androidPollWatchdog.stop();
    m_bleNotifyPoller.stop();
    m_androidPolling = false;
    m_androidElmInitialized = false;
    QJniObject::callStaticMethod<void>(
        "org/octave/app/OctaveOBDBridge", "disconnect", "()V");
    m_btResponseBuffer.clear();
}

void OBDManager::onAndroidBlePoll()
{
    // 0) Forward the Java bridge's event/error lines into OCTAVE's log files
    //    (Chunk 5 of the observability plan: logcat is unreachable in the car).
    for (int i = 0; i < 50; ++i) {
        QJniObject jLine = QJniObject::callStaticObjectMethod(
            "org/octave/app/OctaveOBDBridge", "pollLog", "()Ljava/lang/String;");
        if (!jLine.isValid())
            break;
        const QString line = jLine.toString();
        if (line.isEmpty())
            break;
        const QString msg = line.mid(2);
        if (line.startsWith(QLatin1String("W|")))
            qCWarning(lcElm327) << "[BLE]" << msg;
        else
            qCInfo(lcElm327) << "[BLE]" << msg;
    }

    // 1) Drain any pending FFE1 notification bytes.
    while (true) {
        QJniObject jArr = QJniObject::callStaticObjectMethod(
            "org/octave/app/OctaveOBDBridge", "pollResponse", "()[B");
        if (!jArr.isValid()) break;
        jbyteArray rawArr = jArr.object<jbyteArray>();
        if (!rawArr) break;
        QJniEnvironment env;
        jsize len = env->GetArrayLength(rawArr);
        if (len <= 0) break;
        QByteArray chunk(len, 0);
        env->GetByteArrayRegion(rawArr, 0, len,
            reinterpret_cast<jbyte *>(chunk.data()));
        m_btResponseBuffer.feed(chunk);
        while (true) {
            auto resp = m_btResponseBuffer.getResponse();
            if (!resp.has_value()) break;
            processAndroidResponse(resp.value());
        }
    }

    // 2) Watch state transitions and react.
    jint state = QJniObject::callStaticMethod<jint>(
        "org/octave/app/OctaveOBDBridge", "stateCode", "()I");
    if (state == m_bleLastJavaState) return;

    // 0=DISCONNECTED 1=CONNECTING 2=DISCOVERING 3=READY 4=ERROR
    switch (state) {
    case 0: { // DISCONNECTED
        QString ev = QJniObject::callStaticObjectMethod(
            "org/octave/app/OctaveOBDBridge", "lastEvent",
            "()Ljava/lang/String;").toString();
        logAndroid(QStringLiteral("disconnected (%1)").arg(ev));
        bool wasConnected = m_connected;
        m_connected = false;
        m_androidElmInitialized = false;
        m_androidPolling = false;
        m_bleNotifyPoller.stop();
        m_androidPollWatchdog.stop();
        setVehicleSupport({}, false);
        setConnectionStatus(QStringLiteral("Disconnected"));
        setConnectionProgress(0);
        // A user disconnect clears m_connected in cleanupConnection() before
        // the bridge reports DISCONNECTED, so wasConnected means a real drop
        if (wasConnected)
            emit connectionLostUnexpectedly(ev.isEmpty() ? QStringLiteral("Bluetooth link lost") : ev);
        if (wasConnected && m_androidReconnectAttempts < m_androidMaxReconnect) {
            ++m_androidReconnectAttempts;
            int delay = qMin(2000 * m_androidReconnectAttempts, 10000);
            QTimer::singleShot(delay, this, &OBDManager::startAndroidConnection);
        }
        break;
    }
    case 2: { // DISCOVERING (GATT connected, services pending)
        logAndroid(QStringLiteral("GATT connected, discovering services…"));
        setConnectionProgress(50);
        setConnectionStatus(QStringLiteral("Discovering services..."));
        break;
    }
    case 3: { // READY (FFE0 + FFE1 + notifications enabled)
        logAndroid(QStringLiteral("FFE0/FFE1 ready, notifications enabled"));
        {
            QMutexLocker locker(&m_lock);
            m_isConnecting = false;
        }
        setConnectionProgress(70);
        setConnectionStatus(QStringLiteral("Initializing ELM327..."));

        // Don't re-save the MAC back to settings here — it was already
        // persisted when the user entered it. Saving the same value triggers
        // SettingsManager::obdBluetoothPortChanged → onSettingsPortChanged →
        // port-change debounce → force_connect → which would tear this very
        // session down mid-ELM-init.

        m_androidElmInitialized = false;
        m_androidInitStep = 0;
        m_btResponseBuffer.clear();

        m_androidInitTimer.disconnect();
        m_androidInitTimer.setSingleShot(true);
        QObject::connect(&m_androidInitTimer, &QTimer::timeout,
                         this, &OBDManager::onAndroidInitStep);

        QTimer::singleShot(500, this, &OBDManager::sendNextAndroidInitCommand);
        break;
    }
    case 4: { // ERROR
        QString err = QJniObject::callStaticObjectMethod(
            "org/octave/app/OctaveOBDBridge", "lastError",
            "()Ljava/lang/String;").toString();
        logAndroid(QStringLiteral("BLE error: %1").arg(err));
        setConnectionStatus(QStringLiteral("BLE error"));
        setConnectionProgress(0);
        m_bleNotifyPoller.stop();
        {
            QMutexLocker locker(&m_lock);
            m_isConnecting = false;
        }
        if (m_androidReconnectAttempts < m_androidMaxReconnect) {
            ++m_androidReconnectAttempts;
            int delay = qMin(2000 * m_androidReconnectAttempts, 10000);
            QTimer::singleShot(delay, this, &OBDManager::startAndroidConnection);
        }
        break;
    }
    default:
        break;
    }
    m_bleLastJavaState = state;
}

void OBDManager::writeAndroidBytes(const QByteArray &data)
{
    if (m_bleLastJavaState != 3) {
        qDebug() << "[OBD] Android: write while not READY (state="
                 << m_bleLastJavaState << "), dropping" << data;
        return;
    }
    // BLE default ATT MTU is 23 bytes (20 byte payload). Chunk to be safe.
    constexpr int CHUNK = 20;
    QJniEnvironment env;
    for (int i = 0; i < data.size(); i += CHUNK) {
        QByteArray slice = data.mid(i, CHUNK);
        jbyteArray jArr = env->NewByteArray(slice.size());
        env->SetByteArrayRegion(jArr, 0, slice.size(),
            reinterpret_cast<const jbyte *>(slice.constData()));
        QJniObject::callStaticMethod<jboolean>(
            "org/octave/app/OctaveOBDBridge", "write", "([B)Z", jArr);
        env->DeleteLocalRef(jArr);
    }
}

void OBDManager::sendNextAndroidInitCommand()
{
    const auto &cmds = ELM327Protocol::initCommands();
    if (m_androidInitStep >= cmds.size()) {
        m_androidElmInitialized = true;
        qDebug() << "[OBD] Android: ELM327 init complete, querying supported PIDs";
        setConnectionProgress(80);
        setConnectionStatus(QStringLiteral("Querying PIDs..."));
        queryAndroidSupportedPids();
        return;
    }
    const InitCommand &c = cmds[m_androidInitStep];
    qDebug() << "[OBD] Android: init"
             << (m_androidInitStep + 1) << "/" << cmds.size()
             << ":" << c.command.trimmed();
    writeAndroidBytes(c.command);
    ++m_androidInitStep;
    m_androidInitTimer.start(c.timeoutMs);
}

void OBDManager::onAndroidInitStep()
{
    // Init-step timeout: send the next command regardless of response.
    sendNextAndroidInitCommand();
}

void OBDManager::queryAndroidSupportedPids()
{
    writeAndroidBytes(QByteArrayLiteral("0100\r"));
    QTimer::singleShot(2000, this, &OBDManager::finalizeAndroidConnection);
}

void OBDManager::finalizeAndroidConnection()
{
    m_connected = true;
    m_derived.connected(QDateTime::currentMSecsSinceEpoch());
    m_androidReconnectAttempts = 0;
    qDebug() << "[OBD] Android: connected with"
             << m_androidSupportedPids.size() << "supported PIDs";
    setConnectionStatus(QStringLiteral("Connected"));
    setConnectionProgress(100);
    emit devicePresenceChanged(true);

    // Build poll list from settings (same logic as desktop) intersected with
    // what the vehicle reports supporting via mode 01 PID 00/20/40/60.
    rebuildAndroidPollList();

    m_androidPollIndex = 0;
    m_androidVinRequested = true;
    m_androidPolling = true;
    m_androidStaleCount = 0;

    m_androidPollWatchdog.disconnect();
    m_androidPollWatchdog.setSingleShot(true);
    QObject::connect(&m_androidPollWatchdog, &QTimer::timeout,
                     this, &OBDManager::onAndroidPollWatchdog);

    pollNextAndroidPid();
}

void OBDManager::rebuildAndroidPollList()
{
    QList<PidKey> pids = buildPidsToWatch();
    QList<PidKey> background = buildBackgroundPids(pids);
    // Custom PIDs are polled by the worker only; this path has none
    auto standard = [](QList<PidKey> &keys) {
        keys.removeIf([](const PidKey &k) { return k.first == kCustomPidMode; });
    };
    standard(pids);
    standard(background);
    // If the vehicle reported a supported set, filter to only those PIDs
    // (mode-1 PID number is the second of the pair; ELM_VOLTAGE is the
    // adapter's own ATRV and always available).
    if (!m_androidSupportedPids.isEmpty()) {
        auto filter = [this](const QList<PidKey> &in) {
            QList<PidKey> filtered;
            for (const PidKey &k : in) {
                if (k == kElmVoltageKey || m_androidSupportedPids.contains(k.second))
                    filtered.append(k);
            }
            return filtered;
        };
        pids = filter(pids);
        background = filter(background);
    }
    if (pids.isEmpty())
        pids.append(kElmVoltageKey);  // heartbeat filtered out: ATRV always answers
    m_androidEnabledPids = pids;
    m_androidBackgroundPids = background;
    if (m_androidPollIndex >= m_androidEnabledPids.size())
        m_androidPollIndex = 0;
    if (m_androidBackgroundIndex >= m_androidBackgroundPids.size())
        m_androidBackgroundIndex = 0;
    m_hasActiveWatchers = !m_androidEnabledPids.isEmpty();
}

void OBDManager::pollNextAndroidPid()
{
    if (!m_connected || !m_androidPolling)
        return;
    if (m_androidVinRequested) {
        // One VIN read per connection (or per read_vin()), in the normal
        // request/response rhythm so it never interleaves with a PID reply
        m_androidVinRequested = false;
        m_androidAwaitingVin = true;
        writeAndroidBytes(QByteArrayLiteral("0902\r"));
        m_androidPollWatchdog.start(2000);
        return;
    }
    if (m_androidEnabledPids.isEmpty())
        return;
    PidKey pid;
    if (m_androidBackgroundNext && !m_androidBackgroundPids.isEmpty()) {
        // One background PID after each full pass of the foreground list
        m_androidBackgroundNext = false;
        if (m_androidBackgroundIndex >= m_androidBackgroundPids.size())
            m_androidBackgroundIndex = 0;
        pid = m_androidBackgroundPids[m_androidBackgroundIndex++];
    } else {
        pid = m_androidEnabledPids[m_androidPollIndex];
        m_androidPollIndex = (m_androidPollIndex + 1) % m_androidEnabledPids.size();
        if (m_androidPollIndex == 0)
            m_androidBackgroundNext = true;
    }
    // ELM_VOLTAGE is the adapter's "ATRV": text reply, parsed separately.
    m_androidAwaitingElmVoltage = (pid == kElmVoltageKey);
    if (m_androidAwaitingElmVoltage)
        writeAndroidBytes(ELM327Protocol::elmVoltageRequest());
    else
        writeAndroidBytes(ELM327Protocol::formatPidRequest(pid.first, pid.second));
    m_androidPollWatchdog.start(500);
}

void OBDManager::onAndroidPollWatchdog()
{
    if (!m_androidPolling || !m_connected)
        return;
    ++m_androidStaleCount;
    if (m_androidStaleCount >= 10) {
        qDebug() << "[OBD] Android: data stale 5s, flushing buffer";
        m_btResponseBuffer.clear();
        m_androidStaleCount = 0;
    }
    pollNextAndroidPid();
}

void OBDManager::processAndroidResponse(const QString &response)
{
    if (response.isEmpty()) return;

    if (!m_androidElmInitialized) {
        qDebug() << "[OBD] Android: init response:" << response;
        return;
    }

    m_androidStaleCount = 0;

    if (m_androidAwaitingVin) {
        m_androidAwaitingVin = false;
        setVin(ELM327Protocol::parseVin(m_btResponseBuffer.lastLines()));
        if (m_androidPolling)
            pollNextAndroidPid();
        return;
    }

    if (m_androidAwaitingElmVoltage) {
        m_androidAwaitingElmVoltage = false;
        if (auto volts = ELM327Protocol::parseElmVoltage(response)) {
            emitParameterSignal(QStringLiteral("elmVoltageChanged"), static_cast<float>(*volts));
            m_lastDataReceived = QDateTime::currentMSecsSinceEpoch();
        }
        if (m_androidPolling)
            pollNextAndroidPid();
        return;
    }

    auto parsed = ELM327Protocol::parseResponse(response);
    if (!parsed.has_value()) {
        if (m_androidPolling)
            pollNextAndroidPid();
        return;
    }

    if (parsed->mode == 1 &&
        (parsed->pid == 0x00 || parsed->pid == 0x20 || parsed->pid == 0x40 ||
         parsed->pid == 0x60 || parsed->pid == 0x80 || parsed->pid == 0xA0 ||
         parsed->pid == 0xC0)) {
        const auto pids = ELM327Protocol::parseSupportedPids(parsed->dataBytes);
        for (int p : pids)
            m_androidSupportedPids.insert(p + parsed->pid);
        // Bit 32 of each bitmap announces the next range (0120, 0140, 0160):
        // chain the request during the connect-time query so the supported
        // set covers the extended PIDs (0x21-0x5E) too, like the desktop
        // worker's querySupportedPids().
        if (!m_connected && pids.contains(32) && parsed->pid < 0xC0)
            writeAndroidBytes(ELM327Protocol::formatPidRequest(1, parsed->pid + 0x20));
        if (m_androidPolling)
            pollNextAndroidPid();
        return;
    }

    if (ELM327Protocol::extendedPidTable().contains({parsed->mode, parsed->pid})) {
        const auto ext = ELM327Protocol::parseResponseLines(m_btResponseBuffer.lastLines(),
                                                            parsed->mode, parsed->pid);
        if (ext.has_value()) {
            // Same two signals as the desktop worker path (onWorkerExtendedData):
            // the black box and the value store listen to parameterSampled
            for (const auto &v : ELM327Protocol::decodeExtendedPid(ext->mode, ext->pid, ext->dataBytes)) {
                emit obdParameterChanged(v.first, static_cast<float>(v.second));
                publishSample(v.first, static_cast<float>(v.second));
            }
            m_lastDataReceived = QDateTime::currentMSecsSinceEpoch();
        }
        if (m_androidPolling)
            pollNextAndroidPid();
        return;
    }

    auto decoded = ELM327Protocol::decodePid(parsed->mode, parsed->pid, parsed->dataBytes);
    if (decoded.has_value()) {
        emitParameterSignal(decoded->signalName, static_cast<float>(decoded->value));
        m_lastDataReceived = QDateTime::currentMSecsSinceEpoch();
    }

    if (m_androidPolling)
        pollNextAndroidPid();
}

#endif // Q_OS_ANDROID
