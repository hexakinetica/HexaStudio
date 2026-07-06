// --- START OF FILE: HexaStudio/jog_control/bench/FakeJogController.cpp ---
#include "FakeJogController.h"

namespace hexa {

FakeJogController::FakeJogController(QObject* parent) : QObject(parent) {
    m_status.actualJoints  = QVector<double>(6, 0.0);
    m_status.actualTcp     = QVector<double>{300.0, 0.0, 400.0, 0.0, 0.0, 0.0};
    m_status.plannedJoints = m_status.actualJoints;
    m_status.plannedTcp    = m_status.actualTcp;
    m_status.displayJoints = m_status.actualJoints;
    m_status.displayTcp    = m_status.actualTcp;
    m_status.isSimulated   = false; // REAL so the bench allows jogging
    m_status.isMoving      = false;
    m_status.robotMoving   = false;
    m_status.jogEnabled    = false;
    m_status.speedOverride = 0.5;
    m_status.activeToolId  = 0;
    m_status.activeBaseId  = 0;
    m_status.currentJogFrame = 0;

    // Demo tools/bases for the context selectors; also the source for the name->id mapping and the
    // demo monitor-pose composition.
    HmiToolData t0; t0.id = 0; t0.name = QStringLiteral("TOOL0");
    HmiToolData t1; t1.id = 1; t1.name = QStringLiteral("GRIPPER"); t1.offset = {0, 0, 120, 0, 0, 0};
    m_tools = {t0, t1};
    HmiBaseData b0; b0.id = 0; b0.name = QStringLiteral("WORLD");
    HmiBaseData b1; b1.id = 1; b1.name = QStringLiteral("TABLE"); b1.offset = {500, 0, 0, 0, 0, 0};
    m_bases = {b0, b1};

    // Demo joint limits (deg), shaped like a typical 6-axis arm; jog clamps against these so the
    // panel's at-limit highlight and the limit notice can be exercised offline.
    m_axisLimits = {
        HmiAxisLimit{0, -170.0, 170.0},
        HmiAxisLimit{1, -120.0, 120.0},
        HmiAxisLimit{2, -150.0, 150.0},
        HmiAxisLimit{3, -180.0, 180.0},
        HmiAxisLimit{4, -120.0, 120.0},
        HmiAxisLimit{5, -350.0, 350.0},
    };

    refreshMonitorPose(); // initial pose for the default tool/base context

    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(kMotionSettleMs);
    connect(&m_settleTimer, &QTimer::timeout, this, &FakeJogController::endMotion);
}

HmiSystemConfig FakeJogController::demoConfig() const {
    HmiSystemConfig cfg;
    cfg.tools = m_tools;
    cfg.bases = m_bases;
    cfg.axisLimits = m_axisLimits;
    return cfg;
}

int FakeJogController::toolIdByName(const QString& name) const {
    for (const HmiToolData& tool : m_tools) {
        if (tool.name == name) return tool.id;
    }
    return 0;
}

int FakeJogController::baseIdByName(const QString& name) const {
    for (const HmiBaseData& base : m_bases) {
        if (base.name == name) return base.id;
    }
    return 0;
}

void FakeJogController::requestJog(int axisIndex, double increment) {
    // Apply the step to the vector the panel actually displays for the current frame: joints in JOINT
    // mode, TCP in WORLD/TOOL. Otherwise the desired-value display would sit dead in Cartesian jog.
    if (m_status.currentJogFrame == 0) {
        if (axisIndex >= 0 && axisIndex < m_status.actualJoints.size()) {
            double target = m_status.actualJoints[axisIndex] + increment * kJogStepScale;
            // Clamp at the demo joint limits, the way the real controller refuses to cross them;
            // a clamped step raises the transient limit notice the panel shows.
            m_status.jogNotice.clear();
            if (axisIndex < m_axisLimits.size()) {
                const HmiAxisLimit& limit = m_axisLimits[axisIndex];
                if (target < limit.minDeg) {
                    target = limit.minDeg;
                    m_status.jogNotice = QStringLiteral("Axis A%1 at limit").arg(axisIndex + 1);
                } else if (target > limit.maxDeg) {
                    target = limit.maxDeg;
                    m_status.jogNotice = QStringLiteral("Axis A%1 at limit").arg(axisIndex + 1);
                }
            }
            m_status.actualJoints[axisIndex] = target;
            m_status.displayJoints = m_status.actualJoints;
        }
    } else {
        if (axisIndex >= 0 && axisIndex < m_status.actualTcp.size()) {
            m_status.actualTcp[axisIndex] += increment * kJogStepScale;
            m_status.displayTcp = m_status.actualTcp;
            refreshMonitorPose();
        }
    }
    emit message(QStringLiteral("JOG axis %1 by %2").arg(axisIndex).arg(increment));
    beginMotion();
}

void FakeJogController::setJogContext(int toolId, int baseId) {
    m_status.activeToolId = toolId;
    m_status.activeBaseId = baseId;
    emit message(QStringLiteral("JOG context: tool %1, base %2").arg(toolId).arg(baseId));
    pushStatus();
}

void FakeJogController::setMonitorContext(int toolId, int baseId) {
    m_monitorToolId = toolId;
    m_monitorBaseId = baseId;
    refreshMonitorPose();
    emit message(QStringLiteral("Monitor context: tool %1, base %2").arg(toolId).arg(baseId));
    pushStatus();
}

// Demo monitor pose, TRANSLATION ONLY: pose = actualTcp + toolOffset - baseOffset (orientation passed
// through). Deliberately NOT the real frame algebra - that lives controller-side in
// RobotService::calculateMonitorPose on shared RDT::pose_math, and the bench must not duplicate
// kinematics. This is just enough for the tool/base selection to visibly change the displayed pose.
void FakeJogController::refreshMonitorPose() {
    QVector<double> toolOffset(6, 0.0);
    for (const HmiToolData& tool : m_tools) {
        if (tool.id == m_monitorToolId) { toolOffset = tool.offset; break; }
    }
    QVector<double> baseOffset(6, 0.0);
    for (const HmiBaseData& base : m_bases) {
        if (base.id == m_monitorBaseId) { baseOffset = base.offset; break; }
    }
    QVector<double> pose = m_status.actualTcp;
    for (int i = 0; i < 3 && i < pose.size(); ++i) {
        pose[i] = m_status.actualTcp[i] + toolOffset[i] - baseOffset[i];
    }
    m_status.monitorPose = pose;
}

void FakeJogController::setJogEnabled(bool enabled) {
    m_status.jogEnabled = enabled;
    emit message(enabled ? QStringLiteral("JOG enabled") : QStringLiteral("JOG disabled"));
    pushStatus();
}

void FakeJogController::setJogMode(int mode) {
    m_status.currentJogFrame = mode;
    pushStatus();
}

void FakeJogController::goHome() {
    m_status.actualJoints.fill(0.0, 6);
    m_status.displayJoints = m_status.actualJoints;
    emit message(QStringLiteral("GO HOME"));
    beginMotion();
}

void FakeJogController::beginMotion() {
    m_status.isMoving = true;
    m_status.robotMoving = true;
    pushStatus();
    m_settleTimer.start();
}

void FakeJogController::endMotion() {
    m_status.isMoving = false;
    m_status.robotMoving = false;
    pushStatus();
}

void FakeJogController::pushStatus() {
    emit statusChanged(m_status);
}

} // namespace hexa
// --- END OF FILE: HexaStudio/jog_control/bench/FakeJogController.cpp ---
