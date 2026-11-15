// OBDManager: what is polled. Demand from the pages and the black box, the
// poll list built from it, the readings coming back, and the vehicle scan
// that says which parameters this car has.
#include "obdmanager.h"
#include "parametercatalog.h"
#include "elm327protocol.h"

#include <QDateTime>
#include <QDebug>
#include <QMetaObject>

#include <algorithm>

// BerryIMU parameters ride through OBDParameterModel like PIDs but are fed by
// berryIMU, not the adapter -- demand for them never reaches the poll set.
static const QSet<QString> &imuParamIds()
{
    static const QSet<QString> ids = [] {
        const QStringList l = ParameterCatalog::imuIds();
        return QSet<QString>(l.cbegin(), l.cend());
    }();
    return ids;
}

QStringList OBDManager::withAliases(const QStringList &names)
{
    // An alias (IGNITION_TIMING) is supported wherever its target is
    QStringList out = names;
    for (const ParameterCatalog::Parameter &p : ParameterCatalog::all()) {
        if (!p.aliasOf.isEmpty() && names.contains(p.aliasOf) && !out.contains(p.id))
            out.append(p.id);
    }
    return out;
}

static const PidKey kRpmKey{1, 0x0C};

void OBDManager::setCustomPids(const QList<CustomPidRequest> &pids)
{
    m_customPids = pids;
}

QList<PidKey> OBDManager::customKeys(const QHash<QString, QSet<QString>> &demand) const
{
    QList<PidKey> keys;
    for (int i = 0; i < m_customPids.size(); ++i) {
        const QString &id = m_customPids.at(i).paramId;
        if (std::any_of(demand.cbegin(), demand.cend(), [&id](const QSet<QString> &ids) { return ids.contains(id); }))
            keys.append({kCustomPidMode, i});
    }
    return keys;
}

QList<PidKey> OBDManager::buildPidsToWatch() const
{
    return pidsToWatch(m_paramDemand, m_vehicleSupportedParameters) + customKeys(m_paramDemand);
}

QList<PidKey> OBDManager::pidsToWatch(const QHash<QString, QSet<QString>> &demand,
                                      const QStringList &vehicleSupported)
{
    QSet<QString> wanted;
    for (auto it = demand.constBegin(); it != demand.constEnd(); ++it)
        wanted.unite(it.value());
    wanted.subtract(imuParamIds());
    // An alias is polled as its target
    for (const QString &id : QSet<QString>(wanted)) {
        const ParameterCatalog::Parameter *p = ParameterCatalog::find(id);
        if (p && !p->aliasOf.isEmpty())
            wanted.insert(p->aliasOf);
    }

    // Filter to this session's scan once it produced a result; an unknown or
    // empty result filters nothing (the adapter answers NO DATA, which the
    // decode paths already ignore).
    const QSet<QString> supported(vehicleSupported.cbegin(), vehicleSupported.cend());
    auto ok = [&supported](const QString &id) { return supported.isEmpty() || supported.contains(id); };

    QList<PidKey> pids;
    const auto &table = ELM327Protocol::pidTable();
    for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
        const QString &cmdName = it.value().commandName;
        if (wanted.contains(cmdName) && ok(cmdName))
            pids.append(it.key());
    }
    // Heartbeat: always poll RPM (ELM_VOLTAGE when the car has no RPM) so the
    // data watchdog stays fed and the global RPM shift light works everywhere.
    const PidKey heartbeat = ok(QStringLiteral("RPM")) ? kRpmKey : kElmVoltageKey;
    if (!pids.contains(heartbeat))
        pids.append(heartbeat);

    // Multi-value PIDs: poll when any of their values is demanded
    const auto &ext = ELM327Protocol::extendedPidTable();
    for (auto it = ext.constBegin(); it != ext.constEnd(); ++it) {
        for (const ExtendedSignal &sig : it.value()) {
            if (wanted.contains(sig.paramId) && ok(sig.paramId)) {
                pids.append(it.key());
                break;
            }
        }
    }
    std::sort(pids.begin(), pids.end());  // deterministic poll order (by PID)
    return pids;
}

void OBDManager::setParameterDemand(const QString &consumer, const QStringList &paramIds)
{
    QSet<QString> ids;
    for (const QString &id : paramIds) {
        if (!id.isEmpty())
            ids.insert(id);
    }
    if (ids.isEmpty()) {
        if (m_paramDemand.remove(consumer) == 0)
            return;
    } else {
        if (m_paramDemand.value(consumer) == ids)
            return;
        m_paramDemand.insert(consumer, ids);
    }
    m_demandDebounce.start();
}

void OBDManager::setBackgroundDemand(const QString &consumer, const QStringList &paramIds)
{
    QSet<QString> ids;
    for (const QString &id : paramIds) {
        if (!id.isEmpty())
            ids.insert(id);
    }
    if (ids.isEmpty()) {
        if (m_backgroundDemand.remove(consumer) == 0)
            return;
    } else {
        if (m_backgroundDemand.value(consumer) == ids)
            return;
        m_backgroundDemand.insert(consumer, ids);
    }
    m_demandDebounce.start();
}

QList<PidKey> OBDManager::backgroundPidsToWatch(const QHash<QString, QSet<QString>> &demand,
                                                const QList<PidKey> &foreground,
                                                const QStringList &vehicleSupported)
{
    if (demand.isEmpty())
        return {};
    QList<PidKey> pids = pidsToWatch(demand, vehicleSupported);
    // pidsToWatch() always appends the heartbeat; the foreground list has it
    const QSet<QString> supported(vehicleSupported.cbegin(), vehicleSupported.cend());
    const PidKey heartbeat = (supported.isEmpty() || supported.contains(QStringLiteral("RPM")))
                                 ? kRpmKey : kElmVoltageKey;
    pids.removeAll(heartbeat);
    for (const PidKey &fg : foreground)
        pids.removeAll(fg);
    return pids;
}

QList<PidKey> OBDManager::buildBackgroundPids(const QList<PidKey> &foreground) const
{
    QList<PidKey> pids = backgroundPidsToWatch(m_backgroundDemand, foreground, m_vehicleSupportedParameters);
    for (const PidKey &key : customKeys(m_backgroundDemand)) {
        if (!foreground.contains(key))
            pids.append(key);
    }
    return pids;
}

void OBDManager::setVehicleSupport(const QStringList &names, bool complete)
{
    QStringList sorted = withAliases(names);
    // The scan can't see custom PIDs: the user's file says this car has them
    if (!sorted.isEmpty()) {
        for (const CustomPidRequest &p : m_customPids) {
            if (!sorted.contains(p.paramId))
                sorted.append(p.paramId);
        }
    }
    sorted.sort();
    if (sorted == m_vehicleSupportedParameters && complete == m_vehicleScanComplete)
        return;
    m_vehicleSupportedParameters = sorted;
    m_vehicleScanComplete = complete;
    emit vehicleSupportedParametersChanged();
}

void OBDManager::applyPollList()
{
    // Swap the poll list in place, no reconnect.
    if (!m_connected) {
        qDebug() << "[OBD] Poll set changed -- applied on next connection";
        return;
    }
    if (m_diagnosticMode) {
        // Polling is deliberately paused; exit_diagnostic_mode() reconnects
        // and rebuilds the list from the current demand anyway.
        qDebug() << "[OBD] Poll set changed -- applied when diagnostic mode exits";
        return;
    }
#ifdef Q_OS_ANDROID
    if (!m_worker) {
        rebuildAndroidPollList();
        qDebug() << "[OBD] Poll set refreshed live," << m_androidEnabledPids.size() << "PIDs";
        if (m_androidPolling && !m_androidEnabledPids.isEmpty() && !m_androidPollWatchdog.isActive())
            pollNextAndroidPid();
        return;
    }
#endif
    if (!m_worker)
        return;
    const QList<PidKey> pids = buildPidsToWatch();
    const QList<PidKey> background = buildBackgroundPids(pids);
    m_hasActiveWatchers = !pids.isEmpty() || !background.isEmpty();
    qDebug() << "[OBD] Poll set refreshed live," << pids.size() << "PIDs +" << background.size() << "background";
    OBDConnectionWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, pids, background]() { worker->updatePidsToWatch(pids, background); },
                              Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// Readings from the worker
// ---------------------------------------------------------------------------

void OBDManager::onWorkerDataReceived(const QString &signalName, float value)
{
    m_lastDataReceived = QDateTime::currentMSecsSinceEpoch();
    emitParameterSignal(signalName, value);
}

void OBDManager::onWorkerExtendedData(const QString &paramId, float value)
{
    m_lastDataReceived = QDateTime::currentMSecsSinceEpoch();
    emit obdParameterChanged(paramId, value);
    emit parameterSampled(paramId, value);
}

void OBDManager::refresh_values()
{
    // Force an immediate poll cycle (no-op if not connected or if already polling)
    if (m_connected && m_worker)
        QMetaObject::invokeMethod(m_worker, "onPollTimer", Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// Vehicle scan
// ---------------------------------------------------------------------------

void OBDManager::scan_vehicle()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot scan -- not connected";
        emit scanProgressChanged(0, QStringLiteral("Not connected to vehicle"));
        return;
    }
    if (m_isScanning) {
        qDebug() << "[OBD] Scan already in progress";
        return;
    }
    m_isScanning = true;
    emit scanProgressChanged(0, QStringLiteral("Starting vehicle scan..."));

#ifdef Q_OS_ANDROID
    if (!m_worker) {
        // No worker thread here: the supported-PID bitmaps were read during
        // connect (queryAndroidSupportedPids), so answer from that cache.
        emit scanOutputChanged(QStringLiteral("[SCAN] Starting vehicle PID scan..."));
        if (m_androidSupportedPids.isEmpty()) {
            emit scanOutputChanged(QStringLiteral("[WARN] No supported PIDs returned from vehicle"));
            emit scanProgressChanged(100, QStringLiteral("No supported PIDs found"));
            onWorkerScanComplete(QStringList());
            return;
        }
        emit scanOutputChanged(QStringLiteral("[INFO] Vehicle reports %1 supported PIDs")
                                   .arg(m_androidSupportedPids.size()));
        const QStringList names = ELM327Protocol::supportedCommandNames(m_androidSupportedPids);
        for (const QString &n : names)
            emit scanOutputChanged(QStringLiteral("[OK] %1").arg(n));
        emit scanOutputChanged(QStringLiteral("[DONE] Scan complete!"));
        onWorkerScanComplete(names);
        return;
    }
#endif
    if (!invokeWorker("doScanVehicle")) {
        m_isScanning = false;
        emit scanProgressChanged(0, QStringLiteral("Scan unavailable"));
    }
}

bool OBDManager::is_scanning() const
{
    return m_isScanning;
}

QStringList OBDManager::get_all_parameter_names() const
{
    // Every single-value command name (python-obd naming), from the decoder
    // table; the multi-value 0x61+ ids are not included
    static const QStringList names = [] {
        QStringList l;
        for (const PidEntry &e : ELM327Protocol::pidTable())
            l.append(e.commandName);
        l.sort();
        return l;
    }();
    return names;
}

QVariantList OBDManager::get_supported_commands() const
{
    QVariantList result;
    for (const QString &cmd : m_supportedCommands)
        result.append(cmd);
    return result;
}

void OBDManager::enable_scanned_parameters(const QVariantList &paramNames)
{
    if (!m_settingsManager) return;
    for (const QVariant &v : paramNames)
        m_settingsManager->save_obd_parameter_enabled(v.toString(), true);
    qDebug() << "[OBD] Enabled" << paramNames.size() << "scanned parameters";
}

void OBDManager::enable_all_supported()
{
    if (!m_supportedCommands.isEmpty()) {
        QVariantList list;
        for (const QString &s : m_supportedCommands)
            list.append(s);
        enable_scanned_parameters(list);
    }
}

void OBDManager::onWorkerScanProgress(int progress, const QString &message)
{
    emit scanProgressChanged(progress, message);
}

void OBDManager::onWorkerScanComplete(const QStringList &supported)
{
    m_supportedCommands = supported;
    m_isScanning = false;
    setVehicleSupport(supported, true);
    applyPollList();  // the supported set now filters the poll list

    QVariantList varList;
    for (const QString &s : supported)
        varList.append(s);

    emit supportedCommandsChanged(varList);
    emit scanCompleteChanged(varList);
    emit scanProgressChanged(100, QStringLiteral("Found %1 supported parameters").arg(supported.size()));

    // Persist scan results
    if (m_settingsManager) {
        m_settingsManager->save_supported_obd_parameters(varList);
    }

    qDebug() << "[OBD] Scan complete:" << supported.size() << "supported parameters";
}

void OBDManager::onWorkerScanOutput(const QString &line)
{
    emit scanOutputChanged(line);
}
