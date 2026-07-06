// --- START OF FILE: HexaStudio/overlays/bench/FakeOverlaysController.cpp ---
#include "FakeOverlaysController.h"

namespace hexa {

FakeOverlaysController::FakeOverlaysController(QObject* parent) : QObject(parent) {
    // Demo config shaped like a commissioned cell: two tools, two bases, per-axis limits, a network
    // endpoint and a robot-visual root transform - every editor page has real data to show.
    HmiToolData t0; t0.id = 0; t0.name = QStringLiteral("TOOL0");
    HmiToolData t1; t1.id = 1; t1.name = QStringLiteral("GRIPPER"); t1.offset = {0, 0, 120, 0, 0, 0};
    m_config.tools = {t0, t1};

    HmiBaseData b0; b0.id = 0; b0.name = QStringLiteral("WORLD");
    HmiBaseData b1; b1.id = 1; b1.name = QStringLiteral("TABLE"); b1.offset = {500, 0, 0, 0, 0, 0};
    m_config.bases = {b0, b1};

    m_config.axisLimits = {
        HmiAxisLimit{0, -170.0, 170.0},
        HmiAxisLimit{1, -120.0, 120.0},
        HmiAxisLimit{2, -150.0, 150.0},
        HmiAxisLimit{3, -180.0, 180.0},
        HmiAxisLimit{4, -120.0, 120.0},
        HmiAxisLimit{5, -350.0, 350.0},
    };

    m_config.network.controllerIp = QStringLiteral("192.168.0.10");
    m_config.network.controllerPort = 30002;

    m_config.robotUrdfPath = QStringLiteral("models/hexa_arm.urdf");
    m_config.modelRootRxDeg = -90.0;   // typical Y-up export mapped to the world Z-up frame

    // --- Diagnostics demo baseline: healthy system, HAL link idle (NotConnected must render as
    // NEUTRAL on the panel - the shipping overlay painted it green). ---
    m_robotStatus.top.isConnected = true;
    m_robotStatus.top.hasError = false;
    m_robotStatus.top.activeErrors = QString();
    m_robotStatus.motion.plannerStatus = QStringLiteral("Ok");
    m_robotStatus.motion.safetyStatus = QStringLiteral("Ok");
    m_robotStatus.motion.interpolatorStatus = QStringLiteral("Ok");
    m_robotStatus.motion.halStatus = QStringLiteral("NotConnected");
    m_robotStatus.motion.failingAxisId = -1;
}

void FakeOverlaysController::applyConfig(const HmiSystemConfig& config) {
    m_lastApplied = config;
    m_config = config;
    m_hasApplied = true;
    emit message(QStringLiteral("Config applied (IP %1:%2, %3 tools, %4 bases)")
                     .arg(config.network.controllerIp)
                     .arg(config.network.controllerPort)
                     .arg(config.tools.size())
                     .arg(config.bases.size()));
    // Echo back, the way the real backend re-publishes settings after the controller acknowledge.
    emit configReceived(m_config);
}

// ---------------------------------------------------------------------------
// Diagnostics seam: deterministic state changes, pushed like the real status stream
// ---------------------------------------------------------------------------

void FakeOverlaysController::pushStatus() {
    emit robotStateChanged(m_robotStatus);
}

void FakeOverlaysController::clearError() {
    m_robotStatus.top.hasError = false;
    m_robotStatus.top.activeErrors.clear();
    m_robotStatus.motion.failingAxisId = -1;
    m_robotStatus.motion.safetyStatus = QStringLiteral("Ok");
    emit message(QStringLiteral("Error latches cleared"));
    pushStatus();
}

void FakeOverlaysController::toggleEStop() {
    m_robotStatus.top.isEStop = !m_robotStatus.top.isEStop;
    emit message(m_robotStatus.top.isEStop ? QStringLiteral("E-STOP engaged")
                                           : QStringLiteral("E-Stop released"));
    pushStatus();
}

void FakeOverlaysController::injectHalWarning() {
    m_robotStatus.motion.halStatus = QStringLiteral("Warning: SyncLost");
    emit message(QStringLiteral("Injected: HAL SyncLost warning"));
    pushStatus();
}

void FakeOverlaysController::injectSafetyFault() {
    m_robotStatus.motion.safetyStatus = QStringLiteral("Error(AxisLimit)");
    m_robotStatus.top.hasError = true;
    m_robotStatus.top.activeErrors = QStringLiteral("Axis limit exceeded");
    m_robotStatus.motion.failingAxisId = 2;   // axis A3
    emit message(QStringLiteral("Injected: safety fault on axis A3"));
    pushStatus();
}

void FakeOverlaysController::recoverAll() {
    m_robotStatus.top.hasError = false;
    m_robotStatus.top.activeErrors.clear();
    m_robotStatus.top.isEStop = false;
    m_robotStatus.motion.failingAxisId = -1;
    m_robotStatus.motion.plannerStatus = QStringLiteral("Ok");
    m_robotStatus.motion.safetyStatus = QStringLiteral("Ok");
    m_robotStatus.motion.interpolatorStatus = QStringLiteral("Ok");
    m_robotStatus.motion.halStatus = QStringLiteral("Ok");
    emit message(QStringLiteral("Recovered: all subsystems Ok"));
    pushStatus();
}

} // namespace hexa
// --- END OF FILE: HexaStudio/overlays/bench/FakeOverlaysController.cpp ---
