// --- START OF FILE: HexaStudio/hal_control/bench/FakeHalController.cpp ---
#include "FakeHalController.h"

namespace hexa {

FakeHalController::FakeHalController(QObject* parent) : QObject(parent) {
    // Demo axis states cover the whole badge variety for design review and gate tests.
    m_halConfig.transportIp = QStringLiteral("127.0.0.1");
    m_halConfig.transportPort = 30110;
    m_halConfig.transportConnected = false;
    m_halConfig.controlOwnerId = 1;   // HexaMotion owns control
    for (int i = 0; i < 6; ++i) {
        HmiHalAxisConfig axis;
        axis.axisIndex = i;
        m_halConfig.axes.append(axis);
    }
    m_halConfig.axes[0].lastStatus = 3;   // Enabled
    m_halConfig.axes[1].lastStatus = 2;   // Ready
    m_halConfig.axes[2].lastStatus = 4;   // Fault
    m_halConfig.axes[2].lastErrorCode = 66;
    m_halConfig.axes[3].lastStatus = 5;   // Homing
    m_halConfig.axes[4].lastStatus = 1;   // Disabled
    m_halConfig.axes[5].lastStatus = 1;   // Disabled

    m_robotStatus.top.isConnected = true;      // bridge up: HAL commands allowed
    m_robotStatus.motion.isSimulated = false;  // REAL view (no sim warning by default)
    m_robotStatus.motion.actualJoints = {10.50, -42.30, 77.10, 0.00, 15.20, -3.30};
    m_robotStatus.motion.plannedJoints = m_robotStatus.motion.actualJoints;
    m_robotStatus.motion.halStatus = QStringLiteral("NotConnected");
}

// Deterministic state changes, pushed back like the real backend telemetry.
void FakeHalController::pushHal() {
    emit halConfigChanged(m_halConfig);
    emit robotStateChanged(m_robotStatus);
}

void FakeHalController::applyHalConfig(const HmiHalConfig& config) {
    // Mirror the enable/disable request into the axis states (Enabled<->Disabled), the way the
    // Motor Configurator acknowledges a config apply. Fault/homing axes keep their state.
    for (int i = 0; i < m_halConfig.axes.size() && i < config.axes.size(); ++i) {
        HmiHalAxisConfig& axis = m_halConfig.axes[i];
        axis.motorEnabled = config.axes[i].motorEnabled;
        const bool untouchable = (axis.lastStatus == 4) || (axis.lastStatus >= 5) ||
                                 (axis.lastErrorCode != 0);
        if (!untouchable) {
            axis.lastStatus = axis.motorEnabled ? 3 : 1;   // Enabled : Disabled
        }
    }
    emit message(QStringLiteral("HAL config applied"));
    pushHal();
}

void FakeHalController::connectHal(const QString& host, int port) {
    m_halConfig.transportIp = host;
    m_halConfig.transportPort = port;
    m_halConfig.transportConnected = true;
    m_robotStatus.motion.halStatus = QStringLiteral("Ok");
    emit message(QStringLiteral("HAL connected to %1:%2").arg(host).arg(port));
    pushHal();
}

void FakeHalController::disconnectHal() {
    m_halConfig.transportConnected = false;
    m_robotStatus.motion.halStatus = QStringLiteral("NotConnected");
    emit message(QStringLiteral("HAL disconnected"));
    pushHal();
}

void FakeHalController::halJog(int axisIndex, double stepDeg) {
    ++m_halJogCount;
    m_lastHalJogAxis = axisIndex;
    m_lastHalJogStep = stepDeg;
    if (axisIndex >= 0 && axisIndex < m_robotStatus.motion.actualJoints.size()) {
        m_robotStatus.motion.actualJoints[axisIndex] += stepDeg;
        m_robotStatus.motion.plannedJoints = m_robotStatus.motion.actualJoints;
    }
    emit message(QStringLiteral("HAL jog axis %1 by %2°").arg(axisIndex).arg(stepDeg));
    pushHal();
}

void FakeHalController::startHoming(int axisIndex) {
    m_halConfig.homingSequenceActive = true;
    m_halConfig.homingState = QStringLiteral("WaitingAxisComplete");
    m_halConfig.homingCurrentAxisId = (axisIndex < 0) ? 1 : axisIndex + 1;
    m_halConfig.homingCurrentIndex = 0;
    m_halConfig.homingAxisCount = (axisIndex < 0) ? 6 : 1;
    const int firstAxis = (axisIndex < 0) ? 0 : axisIndex;
    if (firstAxis < m_halConfig.axes.size()) {
        m_halConfig.axes[firstAxis].lastStatus = 5;   // Homing
    }
    emit message(axisIndex < 0 ? QStringLiteral("Homing sequence 1..6 started")
                               : QStringLiteral("Homing axis %1 started").arg(axisIndex + 1));
    pushHal();
}

void FakeHalController::setZeroAxis(int axisIndex) {
    if (axisIndex >= 0 && axisIndex < m_robotStatus.motion.actualJoints.size()) {
        m_robotStatus.motion.actualJoints[axisIndex] = 0.0;
        m_robotStatus.motion.plannedJoints = m_robotStatus.motion.actualJoints;
    }
    emit message(QStringLiteral("Set zero axis %1").arg(axisIndex + 1));
    pushHal();
}

void FakeHalController::setZeroAll() {
    m_robotStatus.motion.actualJoints.fill(0.0, 6);
    m_robotStatus.motion.plannedJoints = m_robotStatus.motion.actualJoints;
    emit message(QStringLiteral("Set zero ALL"));
    pushHal();
}

void FakeHalController::clearErrors() {
    for (HmiHalAxisConfig& axis : m_halConfig.axes) {
        axis.lastErrorCode = 0;
        if (axis.lastStatus == 4) axis.lastStatus = 1;   // Fault -> Disabled after clear
    }
    m_halConfig.lastIpcError.clear();
    emit message(QStringLiteral("Errors cleared (all axes)"));
    pushHal();
}

void FakeHalController::setJogArmed(bool armed) {
    m_jogArmed = armed;
    emit message(armed ? QStringLiteral("HAL jog armed") : QStringLiteral("HAL jog disarmed"));
    pushHal();
}

void FakeHalController::requestEStop() {
    m_robotStatus.top.isEStop = true;
    emit message(QStringLiteral("E-STOP submitted"));
    pushHal();
}

} // namespace hexa
// --- END OF FILE: HexaStudio/hal_control/bench/FakeHalController.cpp ---
