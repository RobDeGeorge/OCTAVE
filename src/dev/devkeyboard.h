#ifndef OCTAVE_DEV_DEVKEYBOARD_H
#define OCTAVE_DEV_DEVKEYBOARD_H

#include <QObject>
#include <QSet>

namespace OctaveSim { class CarSim; }

namespace OctaveDev {

using OctaveSim::CarSim;

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

#endif // OCTAVE_DEV_DEVKEYBOARD_H
