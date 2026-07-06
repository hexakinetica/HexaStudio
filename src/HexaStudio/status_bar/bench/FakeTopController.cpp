// --- START OF FILE: HexaStudio/status_bar/bench/FakeTopController.cpp ---
#include "FakeTopController.h"

#include <cmath>

namespace hexa {

FakeTopController::FakeTopController(QObject* parent) : QObject(parent) {
    m_status.isConnected = true;
    m_status.isEStop = false;
    m_status.isPowerOn = true;
    m_status.hasError = false;
    m_status.mode = QStringLiteral("SIM");
    m_status.activeErrors = QStringLiteral("System Ready");
    m_status.cpuLoad = 23.0;
    m_status.controllerTemp = 41.0;
    m_status.networkLatency = 1.2;

    m_jitterTimer.setInterval(kJitterIntervalMs);
    connect(&m_jitterTimer, &QTimer::timeout, this, &FakeTopController::onJitterTick);
    m_jitterTimer.start();
}

HmiSystemConfig FakeTopController::demoConfig() const {
    HmiSystemConfig cfg;
    cfg.network.controllerIp = QStringLiteral("192.168.0.10");
    return cfg;
}

void FakeTopController::setMode(bool isRealRobot) {
    m_status.mode = isRealRobot ? QStringLiteral("REAL") : QStringLiteral("SIM");
    emit message(QStringLiteral("Mode set to %1").arg(m_status.mode));
    pushStatus();
}

void FakeTopController::setSpeedOverride(int percent) {
    m_speedPercent = percent;
    emit message(QStringLiteral("Speed override %1%").arg(percent));
    pushStatus();
}

void FakeTopController::setEStop(bool active) {
    m_status.isEStop = active;
    emit message(active ? QStringLiteral("E-STOP ACTIVATED") : QStringLiteral("E-Stop reset"));
    pushStatus();
}

void FakeTopController::setMoving(bool moving) {
    m_isMoving = moving;
    pushStatus();
}

void FakeTopController::setConnected(bool connected) {
    m_status.isConnected = connected;
    pushStatus();
}

// Deterministic (sine-based, no randomness) wiggle so repeated bench runs and renders are
// reproducible while the monitor still looks alive.
void FakeTopController::onJitterTick() {
    ++m_tick;
    const double t = static_cast<double>(m_tick);
    m_status.cpuLoad = 23.0 + 6.0 * std::sin(t * 0.7);
    m_status.controllerTemp = 41.0 + 1.5 * std::sin(t * 0.23);
    m_status.networkLatency = 1.2 + 0.6 * std::fabs(std::sin(t * 1.1));
    pushStatus();
}

void FakeTopController::pushStatus() {
    emit statusChanged(m_status, m_isMoving);
}

} // namespace hexa
// --- END OF FILE: HexaStudio/status_bar/bench/FakeTopController.cpp ---
