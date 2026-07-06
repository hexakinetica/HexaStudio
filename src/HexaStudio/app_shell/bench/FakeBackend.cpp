// --- START OF FILE: HexaStudio/app_shell/bench/FakeBackend.cpp ---
#include "FakeBackend.h"

#include <cmath>

namespace hexa {

namespace {
constexpr int kTickIntervalMs = 300;
constexpr int kTicksPerProgramRow = 3;   // program execution pace on the bench
} // namespace

FakeBackend::FakeBackend(QObject* parent) : BackendClient(parent) {
    // --- Demo configuration: a commissioned cell (every panel page has real data) ---
    HmiToolData t0; t0.id = 0; t0.name = QStringLiteral("TOOL0");
    HmiToolData t1; t1.id = 1; t1.name = QStringLiteral("GRIPPER"); t1.offset = {0, 0, 120, 0, 0, 0};
    m_config.tools = {t0, t1};
    HmiBaseData b0; b0.id = 0; b0.name = QStringLiteral("WORLD");
    HmiBaseData b1; b1.id = 1; b1.name = QStringLiteral("TABLE"); b1.offset = {500, 0, 0, 0, 0, 0};
    m_config.bases = {b0, b1};
    m_config.axisLimits = {
        HmiAxisLimit{0, -170.0, 170.0}, HmiAxisLimit{1, -120.0, 120.0},
        HmiAxisLimit{2, -150.0, 150.0}, HmiAxisLimit{3, -180.0, 180.0},
        HmiAxisLimit{4, -120.0, 120.0}, HmiAxisLimit{5, -350.0, 350.0},
    };
    m_config.network.controllerIp = QStringLiteral("192.168.0.10");
    m_config.network.controllerPort = 30002;
    // Real bench URDF + the production root transform (hexacore_config.json: modelRootRxDeg = 90),
    // so the integrated viewport renders the actual robot offline through the SAME config path the
    // live controller uses.
#ifdef APP_SHELL_DEMO_URDF
    m_config.robotUrdfPath = QString::fromUtf8(APP_SHELL_DEMO_URDF);
#else
    m_config.robotUrdfPath = QStringLiteral("models/hexa_arm.urdf");
#endif
    m_config.modelRootRxDeg = 90.0;

    HmiHalConfig& hal = m_config.halConfigCurrent;
    hal.transportIp = QStringLiteral("127.0.0.1");
    hal.transportPort = 30110;
    hal.transportConnected = false;
    hal.controlOwnerId = 1;
    for (int i = 0; i < 6; ++i) {
        HmiHalAxisConfig axis;
        axis.axisIndex = i;
        axis.lastStatus = 2;   // Ready
        hal.axes.append(axis);
    }

    // --- Demo status: bridge up, REAL mode so jog/HAL are armed for the shell demo ---
    m_status.top.isConnected = true;
    m_status.top.isPowerOn = true;
    m_status.top.mode = QStringLiteral("REAL");
    m_status.motion.isSimulated = false;
    m_status.motion.plannerStatus = QStringLiteral("Ok");
    m_status.motion.safetyStatus = QStringLiteral("Ok");
    m_status.motion.interpolatorStatus = QStringLiteral("Ok");
    m_status.motion.halStatus = QStringLiteral("NotConnected");
    m_status.motion.failingAxisId = -1;
    m_status.top.cpuLoad = 23.0;
    m_status.top.controllerTemp = 41.0;
    m_status.top.networkLatency = 1.2;

    // --- Demo controller storage ---
    QVector<ProgramCommand> demo;
    ProgramCommand home(CommandType::Motion, QStringLiteral("PTP"), QStringLiteral("Home"));
    home.params[QStringLiteral("Speed")] = 100;
    home.params[QStringLiteral("Zone")] = QStringLiteral("FINE");
    home.params[QStringLiteral("Joints")] =
        QVariant::fromValue(QVector<double>{0, 0, 0, 0, 0, 0});
    demo.append(home);
    ProgramCommand pick(CommandType::Motion, QStringLiteral("PTP"), QStringLiteral("Pick"));
    pick.params[QStringLiteral("Speed")] = 50;
    pick.params[QStringLiteral("Zone")] = QStringLiteral("FINE");
    pick.params[QStringLiteral("Joints")] =
        QVariant::fromValue(QVector<double>{45, -30, 60, 0, 30, 0});
    demo.append(pick);
    m_remoteFiles.insert(QStringLiteral("demo_cycle"), demo);

    m_tick.setInterval(kTickIntervalMs);
    connect(&m_tick, &QTimer::timeout, this, &FakeBackend::onTick);
    m_tick.start();
}

void FakeBackend::publishAll() {
    emit configReceived(m_config);
    emit halConfigChanged(m_config.halConfigCurrent);
    emit tcpSimulatorStateChanged(false);
    // Deliberately NO trajectory, ever: this fake does not simulate the planner (honesty boundary,
    // see FakeBackend.h). Previews are reviewed on the live stack only.
    pushStatus();
}

void FakeBackend::setTcpSimulatorRunning(bool running) {
    emit tcpSimulatorStateChanged(running);
    emit messageOccurred(running ? QStringLiteral("TCP HAL simulator started")
                                 : QStringLiteral("TCP HAL simulator stopped"), false);
}

void FakeBackend::pushStatus() {
    emit robotStateChanged(m_status);
}

void FakeBackend::pushHalConfig() {
    emit halConfigChanged(m_config.halConfigCurrent);
}

void FakeBackend::echoJogTarget(int axisIndex, double deltaDeg) {
    // Wiring echo only: the commanded target (displayJoints - the jog panel's DESIRED value) moves,
    // proving the intent crossed the seam. Planned/actual joints deliberately do NOT move - this
    // fake has no planner/HAL; motion is reviewed on the live stack (honesty boundary).
    if (axisIndex < 0 || axisIndex >= m_status.motion.displayJoints.size()) return;
    m_status.motion.displayJoints[axisIndex] += deltaDeg;
}

// ---------------------------------------------------------------------------
// Top bar / global
// ---------------------------------------------------------------------------

void FakeBackend::setMode(bool isRealRobot) {
    m_status.motion.isSimulated = !isRealRobot;
    m_status.top.mode = isRealRobot ? QStringLiteral("REAL") : QStringLiteral("SIM");
    emit messageOccurred(QStringLiteral("Mode set to %1").arg(m_status.top.mode), false);
    pushStatus();
}

void FakeBackend::setSpeedOverride(int percent) {
    m_status.motion.speedOverride = static_cast<double>(percent) / 100.0;
    emit messageOccurred(QStringLiteral("Speed override %1%").arg(percent), false);
    pushStatus();
}

void FakeBackend::setEStop(bool active) {
    m_status.top.isEStop = active;
    if (active) {
        m_status.prog.isRunning = false;
        m_status.prog.isPaused = false;
        m_status.motion.robotMoving = false;
        m_status.motion.isMoving = false;
    }
    emit messageOccurred(active ? QStringLiteral("E-STOP engaged")
                                : QStringLiteral("E-Stop released"), active);
    pushStatus();
}

void FakeBackend::clearError() {
    m_status.top.hasError = false;
    m_status.top.activeErrors.clear();
    m_status.motion.failingAxisId = -1;
    m_status.motion.safetyStatus = QStringLiteral("Ok");
    for (HmiHalAxisConfig& axis : m_config.halConfigCurrent.axes) {
        axis.lastErrorCode = 0;
        if (axis.lastStatus == 4) axis.lastStatus = 1;   // Fault -> Disabled after clear
    }
    m_config.halConfigCurrent.lastIpcError.clear();
    emit messageOccurred(QStringLiteral("Error latches cleared"), false);
    pushHalConfig();
    pushStatus();
}

// ---------------------------------------------------------------------------
// Jog
// ---------------------------------------------------------------------------

void FakeBackend::jogJointIncremental(int axisIndex, double increment) {
    // The controller is the final safety arbiter: a jog while disarmed is refused loudly.
    if (!m_status.motion.jogEnabled) {
        emit messageOccurred(QStringLiteral("Jog refused: jog is not armed"), true);
        return;
    }
    echoJogTarget(axisIndex, increment);
    pushStatus();
}

void FakeBackend::setJogEnabled(bool enabled) {
    m_status.motion.jogEnabled = enabled;
    pushStatus();
}

void FakeBackend::setJogMode(int mode) {
    m_status.motion.currentJogFrame = mode;
    pushStatus();
}

void FakeBackend::setJogContext(int toolId, int baseId) {
    m_status.motion.activeToolId = toolId;
    m_status.motion.activeBaseId = baseId;
    pushStatus();
}

void FakeBackend::setMonitorContext(int toolId, int baseId) {
    // Translation-only demo composition (real frame algebra stays controller-side).
    QVector<double> toolOffset(6, 0.0);
    for (const HmiToolData& tool : m_config.tools) {
        if (tool.id == toolId) { toolOffset = tool.offset; break; }
    }
    QVector<double> baseOffset(6, 0.0);
    for (const HmiBaseData& base : m_config.bases) {
        if (base.id == baseId) { baseOffset = base.offset; break; }
    }
    QVector<double> pose = m_status.motion.actualTcp;
    for (int i = 0; i < 3 && i < pose.size(); ++i) {
        pose[i] = m_status.motion.actualTcp[i] + toolOffset[i] - baseOffset[i];
    }
    m_status.motion.monitorPose = pose;
    pushStatus();
}

// ---------------------------------------------------------------------------
// Program execution + controller storage
// ---------------------------------------------------------------------------

void FakeBackend::startProgram(const QVector<ProgramCommand>& program) {
    if (m_status.top.isEStop) {
        emit messageOccurred(QStringLiteral("Program refused: E-Stop is engaged"), true);
        return;
    }
    m_activeProgram = program;
    m_status.prog.isRunning = !program.isEmpty();
    m_status.prog.isPaused = false;
    m_status.prog.currentRowIndex = program.isEmpty() ? -1 : 0;
    // Row-flow echo only (editor highlight follows); no motion claim - the fake has no planner.
    m_programTicksPerRow = kTicksPerProgramRow;
    emit messageOccurred(QStringLiteral("Program started (%1 commands)").arg(program.size()),
                         false);
    pushStatus();
}

void FakeBackend::pauseProgram() {
    m_status.prog.isPaused = true;
    m_status.motion.isMoving = false;
    pushStatus();
}

void FakeBackend::resumeProgram() {
    if (m_status.prog.isRunning) {
        m_status.prog.isPaused = false;
        m_status.motion.isMoving = true;
    }
    pushStatus();
}

void FakeBackend::stopProgram() {
    m_status.prog.isRunning = false;
    m_status.prog.isPaused = false;
    m_status.prog.currentRowIndex = -1;
    m_status.motion.isMoving = false;
    pushStatus();
}

void FakeBackend::uploadProgram(const QVector<ProgramCommand>& program) {
    emit messageOccurred(QStringLiteral("Program uploaded (%1 commands)").arg(program.size()),
                         false);
}

void FakeBackend::requestRemoteFileList() {
    emit remoteFileListReceived(QStringList(m_remoteFiles.keys()));
}

void FakeBackend::loadRemoteProgramFile(const QString& filename) {
    if (!m_remoteFiles.contains(filename)) {
        emit messageOccurred(QStringLiteral("Remote program '%1' not found").arg(filename), true);
        return;
    }
    m_status.prog.loadedProgramName = filename;
    emit programLoaded(m_remoteFiles.value(filename));
    pushStatus();
}

void FakeBackend::saveRemoteProgramFile(const QString& filename,
                                        const QVector<ProgramCommand>& program) {
    m_remoteFiles.insert(filename, program);
    m_status.prog.loadedProgramName = filename;
    emit messageOccurred(QStringLiteral("Remote program '%1' saved").arg(filename), false);
    emit programSaved(filename);
    emit remoteFileListReceived(QStringList(m_remoteFiles.keys()));
    pushStatus();
}

void FakeBackend::deleteRemoteProgramFile(const QString& filename) {
    m_remoteFiles.remove(filename);
    emit messageOccurred(QStringLiteral("Remote program '%1' deleted").arg(filename), false);
    emit remoteFileListReceived(QStringList(m_remoteFiles.keys()));
}

// ---------------------------------------------------------------------------
// System configuration
// ---------------------------------------------------------------------------

void FakeBackend::applySettings(const HmiSystemConfig& newConfig) {
    // The settings overlay does not edit the HAL runtime mirror: preserve it across the apply.
    const HmiHalConfig halCurrent = m_config.halConfigCurrent;
    m_config = newConfig;
    m_config.halConfigCurrent = halCurrent;
    emit messageOccurred(QStringLiteral("Configuration applied (IP %1:%2)")
                             .arg(newConfig.network.controllerIp)
                             .arg(newConfig.network.controllerPort), false);
    emit configReceived(m_config);   // backend re-publish after the controller acknowledge
}

// ---------------------------------------------------------------------------
// HAL runtime
// ---------------------------------------------------------------------------

void FakeBackend::connectHalEndpoint(const QString& host, int port) {
    m_config.halConfigCurrent.transportIp = host;
    m_config.halConfigCurrent.transportPort = port;
    m_config.halConfigCurrent.transportConnected = true;
    m_status.motion.halStatus = QStringLiteral("Ok");
    emit messageOccurred(QStringLiteral("HAL connected to %1:%2").arg(host).arg(port), false);
    pushHalConfig();
    pushStatus();
}

void FakeBackend::disconnectHalEndpoint() {
    m_config.halConfigCurrent.transportConnected = false;
    m_status.motion.halStatus = QStringLiteral("NotConnected");
    emit messageOccurred(QStringLiteral("HAL disconnected"), false);
    pushHalConfig();
    pushStatus();
}

void FakeBackend::jogJointIncrementalHal(int axisIndex, double stepDeg) {
    echoJogTarget(axisIndex, stepDeg);
    pushStatus();
}

void FakeBackend::startHomingSequence(int axisIndex) {
    HmiHalConfig& hal = m_config.halConfigCurrent;
    hal.homingSequenceActive = true;
    hal.homingState = QStringLiteral("WaitingAxisComplete");
    hal.homingCurrentAxisId = (axisIndex < 0) ? 1 : axisIndex + 1;
    hal.homingCurrentIndex = 0;
    hal.homingAxisCount = (axisIndex < 0) ? 6 : 1;
    const int firstAxis = (axisIndex < 0) ? 0 : axisIndex;
    if (firstAxis < hal.axes.size()) {
        hal.axes[firstAxis].lastStatus = 5;   // Homing
    }
    emit messageOccurred(axisIndex < 0
                             ? QStringLiteral("Homing sequence 1..6 started")
                             : QStringLiteral("Homing axis %1 started").arg(axisIndex + 1),
                         false);
    pushHalConfig();
    pushStatus();
}

void FakeBackend::masterAxis(int axisIndex) {
    if (axisIndex >= 0 && axisIndex < m_status.motion.actualJoints.size()) {
        m_status.motion.actualJoints[axisIndex] = 0.0;
        m_status.motion.plannedJoints = m_status.motion.actualJoints;
        m_status.motion.displayJoints = m_status.motion.actualJoints;
    }
    emit messageOccurred(QStringLiteral("Axis %1 mastered (set zero)").arg(axisIndex + 1), false);
    pushStatus();
}

void FakeBackend::setZeroAll() {
    m_status.motion.actualJoints.fill(0.0, 6);
    m_status.motion.plannedJoints = m_status.motion.actualJoints;
    m_status.motion.displayJoints = m_status.motion.actualJoints;
    emit messageOccurred(QStringLiteral("Set zero ALL"), false);
    pushStatus();
}

void FakeBackend::applyHalConfig(const HmiHalConfig& halConfig) {
    HmiHalConfig& hal = m_config.halConfigCurrent;
    for (int i = 0; i < hal.axes.size() && i < halConfig.axes.size(); ++i) {
        HmiHalAxisConfig& axis = hal.axes[i];
        axis.motorEnabled = halConfig.axes[i].motorEnabled;
        const bool untouchable = (axis.lastStatus == 4) || (axis.lastStatus >= 5) ||
                                 (axis.lastErrorCode != 0);
        if (!untouchable) {
            axis.lastStatus = axis.motorEnabled ? 3 : 1;   // Enabled : Disabled
        }
    }
    emit messageOccurred(QStringLiteral("HAL config applied"), false);
    pushHalConfig();
}

// ---------------------------------------------------------------------------
// Deterministic tick: program stepping, jog motion pulse, stats jitter
// ---------------------------------------------------------------------------

void FakeBackend::onTick() {
    ++m_tickCount;
    bool changed = false;

    // Program execution stepping.
    if (m_status.prog.isRunning && !m_status.prog.isPaused) {
        if (--m_programTicksPerRow <= 0) {
            m_programTicksPerRow = kTicksPerProgramRow;
            ++m_status.prog.currentRowIndex;
            if (m_status.prog.currentRowIndex >= m_activeProgram.size()) {
                m_status.prog.isRunning = false;
                m_status.prog.currentRowIndex = -1;
                m_status.motion.isMoving = false;
                emit messageOccurred(QStringLiteral("Program finished"), false);
            }
            changed = true;
        }
    }

    // Deterministic stats jitter (reproducible renders, alive STATS page).
    const double t = static_cast<double>(m_tickCount);
    m_status.top.cpuLoad = 23.0 + 6.0 * std::sin(t * 0.7);
    m_status.top.controllerTemp = 41.0 + 1.5 * std::sin(t * 0.23);
    m_status.top.networkLatency = 1.2 + 0.6 * std::fabs(std::sin(t * 1.1));
    changed = true;

    if (changed) pushStatus();
}

} // namespace hexa
// --- END OF FILE: HexaStudio/app_shell/bench/FakeBackend.cpp ---
