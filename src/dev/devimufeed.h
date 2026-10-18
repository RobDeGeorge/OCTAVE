#ifndef OCTAVE_DEV_DEVIMUFEED_H
#define OCTAVE_DEV_DEVIMUFEED_H

// Synthetic IMU motion for dev mode. Drives the
// REAL berryIMU manager's signals from a GUI-thread timer so QML sees exactly
// what it would from hardware: a winding road tied to the simulated speed,
// lateral G from speed x turn rate, longitudinal G from the change in speed,
// body roll and pitch, and hills for altitude.
//
// Opt-in (sim_imu {"on": true}), so a dev run on a Pi with a real IMU is never
// masked. Turning it on stops the I2C worker; turning it off restarts it.

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class BerryIMUManager;

namespace OctaveDev {

class CarSim;

class DevImuFeed : public QObject
{
    Q_OBJECT

public:
    DevImuFeed(BerryIMUManager *imu, CarSim *sim, QObject *parent = nullptr);

    bool running() const { return m_timer.isActive(); }
    void setEnabled(bool on);
    QVariantMap status() const;

private slots:
    void tick();

private:
    static constexpr int kRateHz = 30;

    BerryIMUManager *m_imu;
    CarSim *m_sim;
    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_last = 0;
    double m_heading = 35.0;
    double m_altitude = 240.0;
    double m_speedMs = 0;
    double m_longG = 0;
    double m_latG = 0;
};

} // namespace OctaveDev

#endif // OCTAVE_DEV_DEVIMUFEED_H
