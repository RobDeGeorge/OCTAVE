#include "simulator.h"

#include "carsim.h"
#include "imufeed.h"
#include "simelm327.h"

#include "../managers/obdmanager.h"
#include "../service/servicepairing.h"
#include "../service/streamhub.h"

#include <QFile>
#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcSim, "octave.sim")

using namespace OctaveSim;

Simulator::Simulator(QObject *parent)
    : QObject(parent)
{
}

Simulator::~Simulator() = default;

void Simulator::start(OBDManager *obd, BerryIMUManager *imu, ServicePairing *pairing, bool imuOn)
{
    if (m_car)
        return;
    // The real OBDManager talks to a simulated ELM327: the protocol
    // parsing, demand-driven polling, vehicle scan and VIN decode all run
    // as they would in the car
    m_car = new CarSim(this);
    const std::shared_ptr<CarState> state = m_car->state();
    // Added beside any other transport (the browser demo's Web Bluetooth
    // one is registered first); start() runs once per run
    OBDConnectionWorker::addCustomTransport(
        [](const QString &port) { return port == QLatin1String(kSimPort); },
        [state](const QString &, QObject *parent) -> QIODevice * { return new SimElm327Device(state, parent); });
    if (obd)
        obd->setPortOverride(QString::fromLatin1(kSimPort));
    if (imu) {
        m_imu = new ImuFeed(imu, m_car, this);
        if (imuOn)
            m_imu->setEnabled(true);
    }
    m_pairing = pairing;
    qCInfo(lcSim) << "Simulated car on" << kSimPort << (m_imu && m_imu->running() ? "with IMU motion" : "");
    emit activeChanged();
}

bool Simulator::setThrottle(double percent)
{
    if (!m_car)
        return false;
    m_car->setThrottle(percent);
    return true;
}

bool Simulator::setBrake(double percent)
{
    if (!m_car)
        return false;
    m_car->setBrake(percent);
    return true;
}

bool Simulator::rev(double intensity)
{
    if (!m_car)
        return false;
    m_car->revEngine(intensity > 0 ? intensity : 50.0);
    return true;
}

bool Simulator::setEngineOn(bool on)
{
    if (!m_car)
        return false;
    m_car->setEngineOn(on);
    return true;
}

bool Simulator::toggleEngine()
{
    if (!m_car)
        return false;
    m_car->toggleEngine();
    return true;
}

bool Simulator::setGear(int gear)
{
    if (!m_car)
        return false;
    m_car->setGear(gear);
    return true;
}

bool Simulator::upshift()
{
    if (!m_car)
        return false;
    m_car->upshift();
    return true;
}

bool Simulator::downshift()
{
    if (!m_car)
        return false;
    m_car->downshift();
    return true;
}

bool Simulator::setSpeed(double mph)
{
    if (!m_car)
        return false;
    m_car->setSpeed(mph);
    return true;
}

bool Simulator::setRpm(double rpm)
{
    if (!m_car)
        return false;
    m_car->setRpm(rpm);
    return true;
}

bool Simulator::setRealistic(bool on)
{
    if (!m_car)
        return false;
    m_car->setRealisticMode(on);
    return true;
}

bool Simulator::scenario(const QString &name)
{
    if (!m_car)
        return false;
    if (name == QLatin1String("cold"))
        m_car->simulateColdStart();
    else if (name == QLatin1String("hot"))
        m_car->simulateOverheating();
    else if (name == QLatin1String("low_fuel"))
        m_car->simulateLowFuel();
    else
        m_car->resetToNormal();
    return true;
}

bool Simulator::setDropouts(bool on)
{
    if (!m_car)
        return false;
    m_car->setSimulateDropouts(on);
    return true;
}

bool Simulator::dropoutNow()
{
    if (!m_car)
        return false;
    m_car->triggerDropoutNow();
    return true;
}

bool Simulator::setFailureRate(double rate)
{
    if (!m_car)
        return false;
    m_car->setFailureRate(rate);
    return true;
}

bool Simulator::setLinkUp(bool up)
{
    if (!m_car)
        return false;
    m_car->setLinkUp(up);
    return true;
}

bool Simulator::setImu(bool on)
{
    if (!m_imu)
        return false;
    m_imu->setEnabled(on);
    return true;
}

bool Simulator::setVideo(bool on)
{
    if (!m_car)
        return false;
    if (!on) {
        if (m_videoTimer)
            m_videoTimer->stop();
        qCInfo(lcSim) << "test video off";
        return true;
    }
    if (m_videoUnits.isEmpty()) {
        // tests/data/stream-test.h264 (tools/stream/make_test_h264.sh), compiled in
        QFile f(QStringLiteral(":/sim/stream-test.h264"));
        if (!f.open(QIODevice::ReadOnly)) {
            qCWarning(lcSim) << "no test video in this build";
            return false;
        }
        m_videoUnits = OctaveService::H264::accessUnits(f.readAll());
    }
    if (!m_videoTimer) {
        m_videoTimer = new QTimer(this);
        m_videoTimer->setTimerType(Qt::PreciseTimer);
        m_videoTimer->setInterval(33);
        connect(m_videoTimer, &QTimer::timeout, this, &Simulator::videoTick);
    }
    m_videoTimer->start();
    qCInfo(lcSim) << "test video on:" << m_videoUnits.size() << "frames looped into phoneMirrorManager.video";
    return true;
}

void Simulator::videoTick()
{
    if (m_videoUnits.isEmpty())
        return;
    // As ScrcpyClient hands them over: parameter sets as a config packet,
    // then the picture with its key flag
    const QByteArray &unit = m_videoUnits.at(int(m_videoFrame % m_videoUnits.size()));
    QByteArray config, picture;
    bool key = false;
    for (const QByteArray &nal : OctaveService::H264::nalUnits(unit)) {
        const int type = quint8(nal.at(0)) & 0x1F;
        (type == 7 || type == 8 ? config : picture) += QByteArray("\x00\x00\x00\x01", 4) + nal;
        key = key || type == 5;
    }
    auto &hub = OctaveService::StreamHub::instance();
    const QString name = QStringLiteral("phoneMirrorManager.video");
    if (!config.isEmpty())
        hub.publishH264(name, config, 0, false, true);
    hub.publishH264(name, picture, m_videoFrame * 1000000 / 30, key, false);
    ++m_videoFrame;
}

QVariantMap Simulator::pairingCode()
{
    if (!m_car || !m_pairing)
        return {};
    m_pairing->startPairing();
    return {{QStringLiteral("code"), m_pairing->pairingCode()},
            {QStringLiteral("secondsLeft"), m_pairing->pairingSecondsLeft()}};
}

QVariantMap Simulator::status() const
{
    if (!m_car)
        return {};
    QVariantMap st = m_car->status();
    if (m_imu)
        st.insert(QStringLiteral("imu"), m_imu->status());
    st.insert(QStringLiteral("video"), m_videoTimer && m_videoTimer->isActive());
    return st;
}

