#ifndef OCTAVE_SIM_SIMULATOR_H
#define OCTAVE_SIM_SIMULATOR_H

// The simulator for frontend authors (`octave --sim`, also implied by
// `--dev`): a simulated car behind the real OBDManager (CarSim + a fake
// ELM327 on port sim://elm327), IMU motion driven through the real
// berryIMU manager, a test H.264 loop into the phone's video stream, and
// pairing codes without the Settings page. Built in every configuration,
// release and app store included, and inert unless start() is called
// (main.cpp does for --sim / --dev; DemoDrive does when the adapter is selected).
//
// Served as the `sim` object (AppCore exposes it in every build, `active`
// false unless this run is simulated). Experimental: not covered by the
// protocol version (docs/api/README.md, Versioning). Every control returns
// false and does nothing when the run is not simulated. The dev tooling's
// sim_* actions (src/dev/devruntime.cpp) are aliases for these methods.
//
// The throwaway config `--sim` runs on is prepared by simconfig.h.

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

class BerryIMUManager;
class OBDManager;
class QTimer;
class ServicePairing;

namespace OctaveSim {
class CarSim;
class ImuFeed;
}

// A manager like the others (global namespace, its class name in docs/api)
class Simulator : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("octave.experimental", "*")
    // Service permissions (src/service/servicescopes.h): driving the car
    // is vehicle; minting a pairing code lets a client add devices, so full
    Q_CLASSINFO("octave.scope", "vehicle")
    Q_CLASSINFO("octave.scope.read", "status")
    Q_CLASSINFO("octave.scope.full", "pairingCode")
    // Whether the simulator has been initialized; obdManager.simulated
    // identifies whether its adapter is currently selected.
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit Simulator(QObject *parent = nullptr);
    ~Simulator() override;

    // Points `obd` at the simulated adapter (the saved port setting is left
    // alone) and makes `imu` drivable. `imuOn`: start the IMU motion now
    // (otherwise setImu turns it on). Once per run.
    void start(OBDManager *obd, BerryIMUManager *imu, ServicePairing *pairing, bool imuOn);
    bool active() const { return m_car != nullptr; }
    OctaveSim::CarSim *car() const { return m_car; }

    // Pedals, 0-100
    Q_INVOKABLE bool setThrottle(double percent);
    Q_INVOKABLE bool setBrake(double percent);
    // A blip of the throttle, 0-100 (50 when 0)
    Q_INVOKABLE bool rev(double intensity);
    Q_INVOKABLE bool setEngineOn(bool on);
    Q_INVOKABLE bool toggleEngine();
    // 1-6
    Q_INVOKABLE bool setGear(int gear);
    Q_INVOKABLE bool upshift();
    Q_INVOKABLE bool downshift();
    // Jump straight to a value; the physics carries on from there
    Q_INVOKABLE bool setSpeed(double mph);
    Q_INVOKABLE bool setRpm(double rpm);
    // Bluetooth-like latency and a 2 % NO DATA rate (on at start)
    Q_INVOKABLE bool setRealistic(bool on);
    // "cold", "hot", "low_fuel", or anything else for normal
    Q_INVOKABLE bool scenario(const QString &name);
    // Link faults: random dropouts, one now, the NO DATA rate (0-1), the
    // adapter unplugged (false) or back (true)
    Q_INVOKABLE bool setDropouts(bool on);
    Q_INVOKABLE bool dropoutNow();
    Q_INVOKABLE bool setFailureRate(double rate);
    Q_INVOKABLE bool setLinkUp(bool up);
    // The simulated IMU motion (G-force, compass, tilt, altitude)
    Q_INVOKABLE bool setImu(bool on);
    // The phone's picture without a phone: a test H.264 clip (640x360 for
    // 2 s, then 1280x720, so clients see a config change) looped into the
    // phoneMirrorManager.video stream at 30 fps
    Q_INVOKABLE bool setVideo(bool on);
    // Shows a pairing code as the Settings page would: {code, secondsLeft},
    // {} when not simulated. For a networked frontend under development
    // (connect it untrusted through --service-bind, fetch the code over
    // loopback here)
    Q_INVOKABLE QVariantMap pairingCode();
    // The car's values and link settings, plus `imu` and `video`; {} when
    // not simulated
    Q_INVOKABLE QVariantMap status() const;

signals:
    void activeChanged();

private:
    void videoTick();

    OctaveSim::CarSim *m_car = nullptr;
    OctaveSim::ImuFeed *m_imu = nullptr;
    QPointer<ServicePairing> m_pairing;
    QTimer *m_videoTimer = nullptr;
    QList<QByteArray> m_videoUnits;
    qint64 m_videoFrame = 0;
};

#endif // OCTAVE_SIM_SIMULATOR_H
