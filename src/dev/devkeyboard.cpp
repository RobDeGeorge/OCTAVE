#include "devkeyboard.h"

#include "../sim/carsim.h"

namespace OctaveDev {

void DevKeyboardController::handle_key(int key, bool pressed)
{
    if (pressed) {
        m_held.insert(key);
        onPressed(key);
    } else {
        m_held.remove(key);
    }
    // Only the held keys drive throttle/brake; re-applying them on every key
    // would cancel Space's rev straight away.
    static const QSet<int> continuous{Qt::Key_W, Qt::Key_Up, Qt::Key_S, Qt::Key_Down};
    if (continuous.contains(key))
        updateContinuous();
}

void DevKeyboardController::onPressed(int key)
{
    switch (key) {
    case Qt::Key_Space: m_sim->revEngine(70); break;
    case Qt::Key_E: m_sim->toggleEngine(); break;
    case Qt::Key_Q: m_sim->downshift(); break;
    case Qt::Key_R: m_sim->upshift(); break;
    case Qt::Key_F1: m_sim->simulateColdStart(); break;
    case Qt::Key_F2: m_sim->simulateOverheating(); break;
    case Qt::Key_F3: m_sim->simulateLowFuel(); break;
    case Qt::Key_F4: m_sim->resetToNormal(); break;
    default:
        if (key >= Qt::Key_1 && key <= Qt::Key_6)
            m_sim->setGear(key - Qt::Key_0);
        break;
    }
}

void DevKeyboardController::updateContinuous()
{
    m_sim->setThrottle(m_held.contains(Qt::Key_W) || m_held.contains(Qt::Key_Up) ? 100 : 0);
    m_sim->setBrake(m_held.contains(Qt::Key_S) || m_held.contains(Qt::Key_Down) ? 100 : 0);
}

} // namespace OctaveDev
