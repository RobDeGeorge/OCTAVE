// OBDManager: diagnostics. Diagnostic mode, trouble codes, the MIL, freeze
// frame, the VIN and the listen-only bus monitor.
#include "obdmanager.h"
#include "dtcdescriptions.h"
#include "elm327protocol.h"

#include <QDateTime>
#include <QDebug>
#include <QMetaObject>
#include <QVariantMap>

// ---------------------------------------------------------------------------
// Diagnostic mode
// ---------------------------------------------------------------------------

void OBDManager::enter_diagnostic_mode()
{
    if (m_diagnosticMode) {
        qDebug() << "[OBD] Already in diagnostic mode";
        return;
    }
    if (!m_connected) {
        qDebug() << "[OBD] Cannot enter diagnostic mode -- not connected";
        return;
    }

    qDebug() << "[OBD] Entering diagnostic mode -- stopping polling...";

    m_dataWatchdogTimer.stop();

    // Stop polling on worker thread
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stopPolling", Qt::QueuedConnection);

    m_diagnosticMode = true;

    qDebug() << "[OBD] Diagnostic mode active";
}

void OBDManager::exit_diagnostic_mode()
{
    if (!m_diagnosticMode) {
        qDebug() << "[OBD] Not in diagnostic mode";
        return;
    }

    qDebug() << "[OBD] Exiting diagnostic mode -- performing full reconnect...";

    m_diagnosticMode = false;

    // Full reconnect to restore clean state
    bool wasConnected = m_connected;
    if (wasConnected) {
        m_connected = false;
        setConnectionStatus(QStringLiteral("Reconnecting"));
        setConnectionDetail(QStringLiteral("Reconnecting after diagnostic mode..."));

        m_isConnecting = false;

        m_connectionAttempts = 0;
        m_reconnectingAfterDiagnostic = true;
        QTimer::singleShot(1000, this, &OBDManager::delayedReconnectAfterDiagnostic);
    }
}

void OBDManager::delayedReconnectAfterDiagnostic()
{
    qDebug() << "[OBD] Executing delayed reconnect after diagnostic mode...";
    if (m_diagnosticMode) {
        qDebug() << "[OBD] Back in diagnostic mode, skipping reconnect";
        m_reconnectingAfterDiagnostic = false;
        return;
    }

    m_isConnecting = false;

    cleanupConnection();
    m_reconnectingAfterDiagnostic = true;
    startConnection();
}

bool OBDManager::is_diagnostic_mode() const
{
    return m_diagnosticMode;
}

// ---------------------------------------------------------------------------
// DTC reading / clearing -- delegate to worker thread
// ---------------------------------------------------------------------------

void OBDManager::read_dtc()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot read DTCs -- not connected";
        emit dtcCodesChanged(QVariantList());
        return;
    }
    invokeWorker("doReadDtc");
}

void OBDManager::read_current_dtc()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot read current DTCs -- not connected";
        return;
    }
    invokeWorker("doReadCurrentDtc");
}

void OBDManager::read_status()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot read status -- not connected";
        return;
    }
    invokeWorker("doReadStatus");
}

void OBDManager::clear_dtc()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot clear DTCs -- not connected";
        emit dtcClearResult(false, QStringLiteral("Not connected to vehicle"));
        return;
    }
    invokeWorker("doClearDtc");
}

void OBDManager::read_freeze_frame()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot read freeze frame -- not connected";
        return;
    }
    invokeWorker("doReadFreezeFrame");
}

void OBDManager::read_vin()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot read VIN -- not connected";
        return;
    }
#ifdef Q_OS_ANDROID
    if (!m_worker) {
        m_androidVinRequested = true;
        return;
    }
#endif
    invokeWorker("doReadVin");
}

// ---------------------------------------------------------------------------
// Diagnostic data getters
// ---------------------------------------------------------------------------

QVariantList OBDManager::get_dtc_codes() const { return m_dtcCodes; }
int OBDManager::get_dtc_count() const { return m_dtcCount; }
bool OBDManager::get_mil_status() const { return m_milStatus; }
QVariantList OBDManager::get_freeze_frame() const { return m_freezeFrameDtcs; }

// ---------------------------------------------------------------------------
// Results from the worker
// ---------------------------------------------------------------------------

void OBDManager::onWorkerVin(const QString &vin)
{
    setVin(vin);
}

void OBDManager::setVin(const QString &vin)
{
    const VinInfo info = ELM327Protocol::decodeVin(vin);
    if (info.vin == m_vin.vin)
        return;
    m_vin = info;
    if (info.vin.isEmpty())
        qInfo() << "[OBD] vehicle did not report a VIN";
    else
        qInfo() << "[OBD] VIN" << info.vin << "-" << info.modelYear << info.make;
    emit vinChanged(info.vin);
}

void OBDManager::onWorkerDtcResult(const QStringList &codes)
{
    QVariantList dtcList;
    for (const QString &code : codes) {
        QVariantMap item;
        item[QStringLiteral("code")] = code;
        item[QStringLiteral("description")] = DtcDescriptions::describe(code);
        dtcList.append(item);
    }
    m_dtcCodes = dtcList;
    m_dtcCount = dtcList.size();
    emit dtcCodesChanged(dtcList);
    emit dtcCountChanged(m_dtcCount);
}

void OBDManager::onWorkerDtcCleared(bool success, const QString &message)
{
    if (success) {
        m_dtcCodes.clear();
        m_dtcCount = 0;
        m_milStatus = false;
        emit dtcCodesChanged(QVariantList());
        emit dtcCountChanged(0);
        emit milStatusChanged(false);
    }
    emit dtcClearResult(success, message);
}

void OBDManager::onWorkerMilStatus(bool mil, int dtcCount)
{
    m_milStatus = mil;
    m_dtcCount = dtcCount;
    emit milStatusChanged(mil);
    emit dtcCountChanged(dtcCount);
}

void OBDManager::onWorkerFreezeFrame(const QStringList &codes, const QVariantMap &values)
{
    // The frame's values by parameter id (paramStore's ids), native units
    QVariantMap byId;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        byId.insert(m_signalToParamId.value(it.key(), it.key()), it.value());
    QVariantList list;
    for (const QString &code : codes) {
        QVariantMap item;
        item[QStringLiteral("code")] = code;
        item[QStringLiteral("description")] = DtcDescriptions::describe(code);
        item[QStringLiteral("values")] = byId;
        list.append(item);
    }
    m_freezeFrameDtcs = list;
    emit freezeFrameChanged(list);
}

// ---------------------------------------------------------------------------
// Bus monitor
// ---------------------------------------------------------------------------

bool OBDManager::canMonitorBus() const
{
    return m_connected && m_worker;
}

bool OBDManager::startBusMonitor(const QString &filter)
{
    if (!canMonitorBus())
        return false;
    OBDConnectionWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, filter]() { worker->doStartBusMonitor(filter); },
                              Qt::QueuedConnection);
    return true;
}

void OBDManager::stopBusMonitor()
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "doStopBusMonitor", Qt::QueuedConnection);
}

void OBDManager::onWorkerBusFrames(const QList<BusFrame> &frames, int overruns)
{
    if (m_busSink)
        m_busSink->busFrames(frames, overruns);
}

void OBDManager::onWorkerBusMonitor(bool active, const QString &note)
{
    if (active != m_busMonitoring) {
        m_busMonitoring = active;
        // No readings arrive while the adapter listens: that is not a stale
        // connection
        if (active) {
            m_dataWatchdogTimer.stop();
        } else if (m_connected && !m_diagnosticMode) {
            m_lastDataReceived = QDateTime::currentMSecsSinceEpoch();
            m_dataWatchdogTimer.start();
        }
    }
    if (m_busSink)
        m_busSink->busMonitorChanged(active, note);
}
