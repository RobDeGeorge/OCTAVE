#ifndef OCTAVE_DEV_CARSIM_H
#define OCTAVE_DEV_CARSIM_H

// Simulated car for `octave --dev`. Dev mode keeps the real OBDManager and
// points it at a simulated ELM327 adapter (simelm327.h), so the real protocol parsing, demand-driven polling,
// vehicle scan and VIN decode all run against this model.
//
// CarSim lives on the GUI thread and steps the physics at 60 Hz. The adapter
// lives on the OBD worker thread and reads the shared CarState under its
// mutex. The state is held by shared_ptr so a worker thread still finishing
// at shutdown never reads a destroyed object.

#include <QMutex>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <memory>

namespace OctaveDev {

// Everything the adapter reports. Copied out whole under the mutex.
struct CarValues {
    bool engineOn = true;
    double rpm = 800.0;
    double speedMph = 0.0;
    double throttle = 0.0;      // pedal, 0-100
    double brake = 0.0;         // 0-100
    int gear = 1;
    double engineLoad = 15.0;
    double maf = 8.0;           // g/s
    double map = 35.0;          // kPa
    double timing = 12.0;       // deg
    double afr = 14.7;
    double o2 = 0.45;           // V
    double coolant = 90.0;      // C
    double oil = 95.0;          // C
    double voltage = 14.2;      // V
    double intakeTemp = 30.0;   // C
    double fuelLevel = 72.0;    // %
    double stft = 1.2;          // %
    double ltft = 2.5;          // %
    double fuelPressure = 380.0;  // kPa
    double runTimeS = 0.0;      // since the engine started
    double odometerKm = 388226.0;
    QStringList storedDtcs{QStringLiteral("P0300"), QStringLiteral("P0420")};
    QStringList pendingDtcs{QStringLiteral("P0300")};
    QString freezeFrameDtc = QStringLiteral("P0420");
    QString vin = QStringLiteral("1J4FA49S03P123456");   // 2003 Jeep
};

// Link behaviour the adapter applies on top of the values.
struct LinkSettings {
    bool realistic = true;      // Bluetooth-ish latency + 2 % NO DATA
    double failureRate = 0.02;
    qint64 downUntilMs = 0;     // simulated Bluetooth dropout ends at (msecs since epoch)
};

class CarState
{
public:
    CarValues values() const { QMutexLocker l(&m_lock); return m_values; }
    LinkSettings link() const { QMutexLocker l(&m_lock); return m_link; }
    template <typename F> void updateValues(F &&f) { QMutexLocker l(&m_lock); f(m_values); }
    template <typename F> void updateLink(F &&f) { QMutexLocker l(&m_lock); f(m_link); }
    bool linkDown() const;

private:
    mutable QMutex m_lock;
    CarValues m_values;
    LinkSettings m_link;
};

class CarSim : public QObject
{
    Q_OBJECT

public:
    explicit CarSim(QObject *parent = nullptr);

    std::shared_ptr<CarState> state() const { return m_state; }

    // Controls (GUI thread): the keyboard, the dev panel and the sim_*
    // command-server actions all land here.
    void setThrottle(double value);
    void setBrake(double value);
    void revEngine(double intensity = 50.0);
    void toggleEngine();
    void setEngineOn(bool on);
    void setGear(int gear);
    void upshift();
    void downshift();
    void setSpeed(double mph);
    void setRpm(double rpm);
    void simulateColdStart();
    void simulateOverheating();
    void simulateLowFuel();
    void resetToNormal();
    // Realistic link (latency, NO DATA drops) vs a fast, clean one
    void setRealisticMode(bool enabled);
    void setFailureRate(double rate);
    // Random 2-4 s Bluetooth dropouts every 90-180 s
    void setSimulateDropouts(bool enabled);
    void triggerDropoutNow();
    // Adapter unplugged (false) until plugged back in: no vehicle at all
    void setLinkUp(bool up);

    // sim_status command result
    QVariantMap status() const;

private slots:
    void step();

private:
    void scheduleDropout();

    std::shared_ptr<CarState> m_state;
    QTimer m_physics;
    QTimer m_revRelease;
    QTimer m_dropout;
    bool m_dropouts = false;
    // Scenario targets the running engine holds temps around
    double m_coolantBase = 90.0;
    double m_oilBase = 95.0;
};

// Routes the dev overlay's key events into the simulator (W/S throttle and
// brake while held, Space rev, E engine, Q/R shift, 1-6 gear, F1-F4 scenarios).
// QML context property `devKeyboardController`.
class DevKeyboardController : public QObject
{
    Q_OBJECT

public:
    explicit DevKeyboardController(CarSim *sim, QObject *parent = nullptr)
        : QObject(parent), m_sim(sim) {}

    Q_INVOKABLE void handle_key(int key, bool pressed);

private:
    void onPressed(int key);
    void updateContinuous();

    CarSim *m_sim;
    QSet<int> m_held;
};

} // namespace OctaveDev

#endif // OCTAVE_DEV_CARSIM_H
