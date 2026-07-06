// --- START OF FILE: HexaStudio/hexa_backend/HexaBackend.cpp ---
/**
 * @file HexaBackend.cpp
 * @brief Implements the new-product backend (BackendClient) on RDT::ClientState + RdtClient.
 *
 * The logic reproduces the E2E-validated shipping RobotService (see REQ_hexa_backend.md §3) with the
 * three RobotServiceAdapter pieces folded in (§4) and the program-mapper enrichment applied (§5).
 */
#include "HexaBackend.h"

#include "ProgramMapper.h"
#include "ClientState.h"
#include "RdtClient.h"
#include "DataTypes.h"
#include "PoseMath.h"
#include "RobotConfig.h"
#include "RdtProtocol.h"

#include <QDebug>
#include <QSettings>
#include <QStringList>

#include <algorithm>
#include <cmath>

using RDT::AxisSet;
using RDT::CartPose;

namespace hexa {

namespace {
// Studio-local (workstation) settings keys — identical to the shipping application, so an NG
// installation picks up the operator's existing endpoint configuration (adapter piece 2).
const QLatin1String kOrgName("HexaStudio");
const QLatin1String kAppName("HexaStudio");
const QLatin1String kKeyControllerIp("network/controllerIp");
const QLatin1String kKeyControllerPort("network/controllerPort");
const QLatin1String kDefaultControllerIp("127.0.0.1");

// Program-command command codes (mirror the editor authoring codes).
constexpr int kProgramCommandStart = 1;
constexpr int kProgramCommandPause = 2;
constexpr int kProgramCommandStop = 3;

// HAL transport defaults (mirror the shipping service).
constexpr uint16_t kDefaultHalPort = 30110;

} // namespace

// ---------------------------------------------------------------------------
// BackendRdtWorker
// ---------------------------------------------------------------------------
BackendRdtWorker::BackendRdtWorker(std::shared_ptr<RDT::ClientState> client_state,
                                   RdtTransportFactory transport_factory, QObject* parent)
    : QObject(parent), client_state_(std::move(client_state)),
      transport_factory_(std::move(transport_factory)) {}

// Defined here (not defaulted in the header) so unique_ptr<RdtClient> is destroyed where RdtClient is
// a complete type (RdtClient.h is included above).
BackendRdtWorker::~BackendRdtWorker() = default;

void BackendRdtWorker::start() {
    client_ = std::make_unique<RDT::RdtClient>(client_state_, transport_factory_);
    timer_ = new QTimer(this);
    timer_->setInterval(20);   // 50 Hz network pump
    connect(timer_, &QTimer::timeout, this, &BackendRdtWorker::tick);
    timer_->start();
}

void BackendRdtWorker::tick() {
    if (!client_) return;
    client_->update();
    if (client_->isConnected()) {
        client_->sendControlState();
    }
    if (last_connected_ != client_->isConnected()) {
        last_connected_ = client_->isConnected();
        emit connectionChanged(last_connected_);
    }
}

void BackendRdtWorker::connectToServer(const QString& ip, int port, bool auto_reconnect) {
    if (!client_) return;
    client_->connect(ip.toStdString(), static_cast<uint16_t>(port), auto_reconnect);
}

void BackendRdtWorker::disconnectFromServer() {
    if (!client_) return;
    client_->disconnect();
}

// ---------------------------------------------------------------------------
// HexaBackend — lifecycle
// ---------------------------------------------------------------------------
HexaBackend::HexaBackend(QObject* parent, RdtTransportFactory transport_factory)
    : BackendClient(parent) {
    // Meta types for queued connections / QVariant conversion.
    qRegisterMetaType<QVector<double>>("QVector<double>");

    // Shared components + network worker on its own thread.
    m_client_state = std::make_shared<RDT::ClientState>();
    m_rdtThread = new QThread(this);
    m_rdtWorker = new BackendRdtWorker(m_client_state, std::move(transport_factory));
    m_rdtWorker->moveToThread(m_rdtThread);

    connect(m_rdtThread, &QThread::started, m_rdtWorker, &BackendRdtWorker::start);
    connect(m_rdtThread, &QThread::finished, m_rdtWorker, &QObject::deleteLater);
    connect(m_rdtWorker, &BackendRdtWorker::connectionChanged, this, &HexaBackend::onConnectionChanged);
    m_rdtThread->start();

    // 50 Hz UI poll + publish.
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &HexaBackend::onUpdateTimer);
    m_updateTimer->start(20);

    m_status = HmiRobotStatus();
    m_config = HmiSystemConfig();
}

HexaBackend::~HexaBackend() {
    // Stop the worker thread cleanly before the shared state is torn down.
    if (m_rdtThread) {
        m_rdtThread->quit();
        m_rdtThread->wait();
    }
}

// ---------------------------------------------------------------------------
// Composition-root surface
// ---------------------------------------------------------------------------
void HexaBackend::publishInitialState() {
    emit configReceived(withWorkstationOverrides(m_config));
    emit halConfigChanged(m_config.halConfigCurrent);
    emit robotStateChanged(m_status);
}

void HexaBackend::notifyTcpSimulatorState(bool running) {
    emit tcpSimulatorStateChanged(running);
}

void HexaBackend::connectToController(const QString& ip, int port) {
    m_client_state->resetSyncState();
    if (!m_rdtWorker) return;
    QMetaObject::invokeMethod(m_rdtWorker, "connectToServer", Qt::QueuedConnection,
                              Q_ARG(QString, ip), Q_ARG(int, port), Q_ARG(bool, true));
}

void HexaBackend::disconnectFromController() {
    if (!m_rdtWorker) return;
    QMetaObject::invokeMethod(m_rdtWorker, "disconnectFromServer", Qt::QueuedConnection);
}

void HexaBackend::onConnectionChanged(bool connected) {
    m_isConnected = connected;
}

// ---------------------------------------------------------------------------
// Main poll: read ClientState, publish the push-only contract signals
// ---------------------------------------------------------------------------
void HexaBackend::onUpdateTimer() {
    if (m_status.top.isConnected != m_isConnected.load()) {
        m_status.top.isConnected = m_isConnected.load();
        emit messageOccurred(m_isConnected.load() ? "Connected to Controller"
                                                   : "Disconnected from Controller",
                             false);
    }

    // --- Config sync (version-tracked) ---
    const uint32_t current_config_version = m_client_state->getConfigVersion();
    if (m_lastKnownConfigVersion != current_config_version) {
        RDT::NetProtocol::RobotConfigData rdt_config;
        rdt_config.definition = m_client_state->getRobotDefinition();
        rdt_config.frames = m_client_state->getFrameSet();
        rdt_config.ipAddress = m_client_state->getConfigIpAddress();
        rdt_config.urdfPath = m_client_state->getConfigUrdfPath();
        rdt_config.realtimeBackend = m_client_state->getConfigRealtimeBackend();
        rdt_config.modelRootX = m_client_state->getConfigModelRootX();
        rdt_config.modelRootY = m_client_state->getConfigModelRootY();
        rdt_config.modelRootZ = m_client_state->getConfigModelRootZ();
        rdt_config.modelRootRxDeg = m_client_state->getConfigModelRootRxDeg();
        rdt_config.modelRootRyDeg = m_client_state->getConfigModelRootRyDeg();
        rdt_config.modelRootRzDeg = m_client_state->getConfigModelRootRzDeg();
        mapRdtConfigToHmi(rdt_config, m_config);

        m_lastKnownConfigVersion = current_config_version;
        // Absorbed adapter piece 2: publish with the workstation endpoint override merged in.
        emit configReceived(withWorkstationOverrides(m_config));
    }

    // --- Program sync (version-tracked, echo-aware) ---
    const uint32_t current_program_version = m_client_state->getProgramVersion();
    if (m_lastKnownProgramVersion != current_program_version) {
        const auto rdt_prog = m_client_state->getLoadedProgram();
        m_cachedProgram = program_mapper::toHmi(rdt_prog);
        m_lastKnownProgramVersion = current_program_version;
        if (m_localProgramEchoPending) {
            // Echo of our own uploadProgram(): cache/version are now in sync; do NOT re-emit
            // programLoaded (that would reset the editor tools menu mid-teach).
            m_localProgramEchoPending = false;
        } else {
            emit programLoaded(m_cachedProgram);
        }
    }

    // --- Trajectory preview sync (version-tracked; consumed by the viewport3d module) ---
    const uint32_t current_traj_version = m_client_state->getTrajectoryVersion();
    if (m_lastKnownTrajVersion != current_traj_version) {
        const auto rdt_traj = m_client_state->getTrajectoryPath();
        HmiTrajectoryData trajectory;
        trajectory.path.reserve(static_cast<int>(rdt_traj.points.size()));
        for (const auto& p : rdt_traj.points) {
            trajectory.path.append(QVector3D(p.x.value(), p.y.value(), p.z.value()));
        }
        trajectory.waypoints.reserve(static_cast<int>(rdt_traj.waypoints.size()));
        for (const auto& p : rdt_traj.waypoints) {
            trajectory.waypoints.append(QVector3D(p.x.value(), p.y.value(), p.z.value()));
        }
        m_lastKnownTrajVersion = current_traj_version;
        emit trajectoryReceived(trajectory);
    }

    // --- HAL config mirror (read every poll; the transport/homing state is not versioned) ---
    mapRdtHalConfigToHmi(m_client_state->getHalConfigCurrent(), m_config.halConfigCurrent);

    // --- File-operation responses ---
    const auto resp = m_client_state->getFileOperationResponse();
    if (resp.processedFileOpId > 0 && resp.processedFileOpId != m_lastKnownFileOpId) {
        m_lastKnownFileOpId = resp.processedFileOpId;
        if (!resp.message.empty()) {
            emit messageOccurred(QString::fromStdString(resp.message), !resp.success);
        }
        if (!resp.file_list.empty()) {
            QStringList list;
            list.reserve(static_cast<int>(resp.file_list.size()));
            for (const auto& f : resp.file_list) list.append(QString::fromStdString(f));
            emit remoteFileListReceived(list);
        }
        if (!resp.program.name.empty()) {
            const auto prog = program_mapper::toHmi(resp.program);
            m_cachedProgram = prog;
            emit programLoaded(prog);
            // Arm-on-load (audit F5): the controller's LOAD file-op only RETURNS the file - the armed
            // program and the trajectory preview would keep showing the PREVIOUS program until the
            // next edit or RUN. Re-uploading right here arms the sequencer, validates the program and
            // regenerates the preview immediately, so what the operator sees IS what is armed. The
            // resulting version echo is suppressed by uploadProgram's echo-pending flag.
            uploadProgram(prog);
        }
        // Program identity (audit A3): confirm the requested file name once its operation succeeded.
        if (resp.success) {
            if (!resp.program.name.empty() && !m_pendingLoadName.isEmpty()) {
                m_loadedProgramName = m_pendingLoadName;
            } else if (!m_pendingSaveName.isEmpty()) {
                m_loadedProgramName = m_pendingSaveName;
                // Save confirmed by the controller (the only program store): push the confirmation
                // so the editor clears its UNSAVED marker only on real success.
                emit programSaved(m_pendingSaveName);
            }
        }
        m_pendingLoadName.clear();
        m_pendingSaveName.clear();

        // Auto-refresh the library after a mutating operation (boss 2026-07-07: no manual REFRESH
        // button). The flag is armed ONLY by saveRemoteProgramFile/deleteRemoteProgramFile, so a
        // LIST response can never re-trigger itself; the mutating operation is finished at this
        // point, so the follow-up LIST cannot clobber a pending request.
        if (m_refreshListAfterFileOp) {
            m_refreshListAfterFileOp = false;
            requestRemoteFileList();
        }
    }

    // --- Status DTO assembly ---
    const auto cmd_pose = m_client_state->getCommandTrajectoryPoint();
    const auto fb_pose = m_client_state->getFeedbackTrajectoryPoint();

    m_status.motion.plannedJoints = axisSetToVector(cmd_pose.command.joint_target);
    m_status.motion.actualJoints = axisSetToVector(fb_pose.feedback.joint_actual);
    m_status.motion.realHardwareJoints = axisSetToVector(m_client_state->getRealHardwareJoints());
    m_status.motion.plannedTcp = cartPoseToVector(cmd_pose.command.cartesian_target);
    m_status.motion.actualTcp = cartPoseToVector(fb_pose.feedback.cartesian_actual);

    // Passive monitor: ACTUAL flange expressed in the MONITOR-selected Tool/Base.
    m_status.motion.monitorPose =
        calculateMonitorPose(fb_pose.feedback.cartesian_actual, m_monitorToolId, m_monitorBaseId);
    // Jog panel shows the COMMANDED target in the JOG Tool/Base frame (same shared math as monitor).
    m_status.motion.displayJoints = m_status.motion.plannedJoints;
    m_status.motion.displayTcp =
        calculateMonitorPose(cmd_pose.command.cartesian_target, m_jogToolId, m_jogBaseId);

    m_status.motion.speedOverride = m_client_state->getSpeedRatio();
    m_status.motion.isSimulated = m_client_state->isSimulated();
    m_status.motion.activeToolId = m_client_state->getActiveToolId();
    m_status.motion.activeBaseId = m_client_state->getActiveBaseId();
    m_status.motion.currentJogFrame = static_cast<int>(m_client_state->getCurrentJogFrame());
    m_status.motion.jogEnabled = m_client_state->getJogEnabled();

    const auto& diag = fb_pose.diagnostics;
    auto plannerToQString = [](RDT::PlannerStatus s) {
        switch (s) {
            case RDT::PlannerStatus::Ok: return QString("Ok");
            case RDT::PlannerStatus::Warning_Approximation: return QString("Warning: Approx");
            case RDT::PlannerStatus::Error_IK_Failed: return QString("Error: IK Failed");
            case RDT::PlannerStatus::Error_Singularity: return QString("Error: Singularity");
            case RDT::PlannerStatus::Error_Path_Invalid: return QString("Error: Path Invalid");
            default: return QString("Unknown (%1)").arg(static_cast<int>(s));
        }
    };
    auto safetyToQString = [](RDT::SafetyStatus s) {
        switch (s) {
            case RDT::SafetyStatus::Ok: return QString("Ok");
            case RDT::SafetyStatus::Warning_VelocityClamped: return QString("Warning: Vel Clamped");
            case RDT::SafetyStatus::Warning_AccelerationClamped: return QString("Warning: Acc Clamped");
            case RDT::SafetyStatus::Error_JointLimit: return QString("Error: Joint Limit");
            case RDT::SafetyStatus::Error_SelfCollision: return QString("Error: Collision");
            case RDT::SafetyStatus::Error_FollowingError: return QString("Error: Following");
            case RDT::SafetyStatus::Error_EStop_Active: return QString("Error: E-Stop");
            default: return QString("Unknown (%1)").arg(static_cast<int>(s));
        }
    };
    auto interpToQString = [](RDT::InterpolatorStatus s) {
        switch (s) {
            case RDT::InterpolatorStatus::Ok: return QString("Ok");
            case RDT::InterpolatorStatus::Warning_Drift: return QString("Warning: Drift");
            case RDT::InterpolatorStatus::Error_Queue_Underrun: return QString("Error: Queue Underrun");
            default: return QString("Unknown (%1)").arg(static_cast<int>(s));
        }
    };

    m_status.motion.plannerStatus = plannerToQString(diag.planner);
    m_status.motion.safetyStatus = safetyToQString(diag.safety);
    m_status.motion.interpolatorStatus = interpToQString(diag.interpolator);
    m_status.motion.failingAxisId = static_cast<int>(diag.failing_axis_id);

    switch (diag.hal) {
        case RDT::HalStatus::Ok: m_status.motion.halStatus = QStringLiteral("Ok"); break;
        case RDT::HalStatus::Warning_SyncLost:
            m_status.motion.halStatus = QStringLiteral("Warning: SyncLost"); break;
        case RDT::HalStatus::Error_DriveFault:
            m_status.motion.halStatus = QStringLiteral("Error: DriveFault"); break;
        case RDT::HalStatus::Error_CommunicationLost:
            m_status.motion.halStatus = QStringLiteral("Error: CommLost"); break;
        case RDT::HalStatus::NotConnected:
        default:
            m_status.motion.halStatus = QStringLiteral("NotConnected"); break;
    }

    m_status.top.isEStop = m_client_state->isEStopActive();
    m_status.top.isPowerOn = m_client_state->isPowerOn();
    m_status.top.activeErrors = QString::fromStdString(m_client_state->getSystemMessage());
    // Only "Jog ..." prefixed controller warnings belong in the jog panel notice line.
    m_status.motion.jogNotice =
        m_status.top.activeErrors.startsWith("Jog ") ? m_status.top.activeErrors : QString();
    m_status.top.hasError = m_client_state->hasError();
    m_status.top.mode = m_client_state->isSimulated() ? "SIM" : "REAL";
    m_status.top.networkLatency = m_client_state->getNetworkLatencyMs();

    const auto robot_mode = m_client_state->getRobotMode();
    m_status.prog.isRunning = (robot_mode == RDT::RobotMode::Running);
    m_status.prog.isPaused = (robot_mode == RDT::RobotMode::Paused);
    m_status.prog.currentRowIndex = static_cast<int>(cmd_pose.header.sequence_index);
    if (m_status.prog.currentRowIndex < 0) m_status.prog.currentRowIndex = 0;
    // The real controller file the program came from / went to; empty until any load or save
    // happened (an unnamed in-editor program is honest - no placeholder).
    m_status.prog.loadedProgramName = m_loadedProgramName;
    // P5 execution annotation: registers + last evaluated branch (REQ_program_sequencer.md §9).
    const auto regs = m_client_state->getProgramRegisters();
    m_status.prog.registers.resize(static_cast<int>(regs.size()));
    for (int i = 0; i < static_cast<int>(regs.size()); ++i) {
        m_status.prog.registers[i] = regs[static_cast<std::size_t>(i)];
    }
    m_status.prog.lastBranchLine = m_client_state->getLastBranchLine();
    m_status.prog.lastBranchTaken = m_client_state->getLastBranchTaken();
    m_status.motion.isMoving = m_status.prog.isRunning;
    m_status.motion.robotMoving = m_client_state->getRobotMoving();

    emit robotStateChanged(m_status);
    // Absorbed adapter piece 1: the contract is push-only; re-publish HAL config every poll so the
    // HAL panel never has to pull getConfig().halConfigCurrent.
    emit halConfigChanged(m_config.halConfigCurrent);
}

// ---------------------------------------------------------------------------
// Control commands
// ---------------------------------------------------------------------------
void HexaBackend::setMode(bool isRealRobot) { m_client_state->setRequestedRealMode(isRealRobot); }

void HexaBackend::setSpeedOverride(int percent) {
    const double ratio = std::clamp(percent / 100.0, 0.0, 1.0);
    m_client_state->setRequestedSpeedOverride(ratio);
}

void HexaBackend::setEStop(bool active) {
    if (active) m_client_state->setRequestedEStop(true, false);
    else m_client_state->setRequestedEStop(false, true);
}

void HexaBackend::clearError() { m_client_state->requestClearError(); }

void HexaBackend::jogJointIncremental(int axisIndex, double increment) {
    if (!m_status.motion.jogEnabled) {
        return;
    }
    if (m_status.motion.isSimulated) {
        // SIM mode is for trajectory dry-runs only; manual jog is blocked.
        return;
    }
    double value = increment;
    if (m_jogFrame != RDT::NetProtocol::JogFrame::JOINT && axisIndex >= 0 && axisIndex < 6) {
        value = (axisIndex < 3) ? RDT::Millimeters(increment).value() : RDT::Degrees(increment).value();
    }
    m_client_state->requestJog(axisIndex, value);
}

void HexaBackend::jogJointIncrementalHal(int axisIndex, double stepDeg) {
    // The HAL overlay is a direct-hardware panel with its own jog-arm and must work in SIM as well as
    // REAL. It gates locally and arms via setJogEnabled, so we do NOT re-gate here.
    if (axisIndex < 0 || axisIndex >= static_cast<int>(RDT::ROBOT_AXES_COUNT)) {
        return;
    }
    m_client_state->requestJogWithContext(axisIndex, stepDeg, RDT::NetProtocol::JogFrame::JOINT,
                                          m_jogToolId, m_jogBaseId);
}

void HexaBackend::setJogEnabled(bool enabled) {
    // Jog arming is allowed regardless of SIM/REAL so the operator can arm jog from the HAL overlay
    // during Motor Configurator bring-up (mirrors the shipping service's MKS bring-up behaviour).
    m_client_state->requestJogEnable(enabled);
}

// ---------------------------------------------------------------------------
// Program execution
// ---------------------------------------------------------------------------
void HexaBackend::startProgram(const QVector<ProgramCommand>& program) {
    uploadProgram(program);   // always upload first to sync logic
    m_client_state->setRequestedProgramCommand(kProgramCommandStart);
}

void HexaBackend::pauseProgram() { m_client_state->setRequestedProgramCommand(kProgramCommandPause); }

void HexaBackend::resumeProgram() {
    // No re-upload: the controller resumes from the current step on Start (code 1) while it is in the
    // Paused exec state. Re-uploading would replace the paused program and reset the step pointer.
    m_client_state->setRequestedProgramCommand(kProgramCommandStart);
}

void HexaBackend::stopProgram() { m_client_state->setRequestedProgramCommand(kProgramCommandStop); }

void HexaBackend::uploadProgram(const QVector<ProgramCommand>& program) {
    const RDT::NetProtocol::ProgramDataStruct rdt_prog = program_mapper::toRdt(program);
    m_cachedProgram = program;
    // The controller bumps its program version after applying this update; the next poll reads that
    // bump back as an echo. Mark it so onUpdateTimer syncs silently instead of re-emitting.
    m_localProgramEchoPending = true;
    m_client_state->requestProgramUpdate(rdt_prog);
}

// ---------------------------------------------------------------------------
// Remote file operations (Controller Storage)
// ---------------------------------------------------------------------------
void HexaBackend::requestRemoteFileList() {
    if (!m_isConnected.load()) { emit messageOccurred("Not connected", true); return; }
    m_client_state->requestListFiles();
}

void HexaBackend::loadRemoteProgramFile(const QString& filename) {
    if (!m_isConnected.load()) { emit messageOccurred("Not connected", true); return; }
    m_pendingLoadName = filename;   // confirmed into m_loadedProgramName by the file-op response
    m_client_state->requestLoadFile(filename.toStdString());
}

void HexaBackend::saveRemoteProgramFile(const QString& filename,
                                        const QVector<ProgramCommand>& program) {
    if (!m_isConnected.load()) { emit messageOccurred("Not connected", true); return; }
    m_pendingSaveName = filename;   // confirmed into m_loadedProgramName by the file-op response
    m_refreshListAfterFileOp = true;   // the library changed: auto-refresh once the response lands
    const RDT::NetProtocol::ProgramDataStruct rdt_prog = program_mapper::toRdt(program);
    m_client_state->requestSaveFile(filename.toStdString(), rdt_prog);
}

void HexaBackend::deleteRemoteProgramFile(const QString& filename) {
    if (!m_isConnected.load()) { emit messageOccurred("Not connected", true); return; }
    m_refreshListAfterFileOp = true;   // the library changed: auto-refresh once the response lands
    m_client_state->requestDeleteFile(filename.toStdString());
}

// ---------------------------------------------------------------------------
// Configuration & context
// ---------------------------------------------------------------------------
void HexaBackend::applySettings(const HmiSystemConfig& newConfig) {
    // Absorbed adapter piece 2: persist the workstation endpoint, forward the settings, and reconnect
    // ONLY when the endpoint actually changed (the exact shipping MainWindow apply flow).
    QSettings settings(kOrgName, kAppName);
    const QString oldIp = settings.value(kKeyControllerIp, QString()).toString();
    const int oldPort = settings.value(kKeyControllerPort, 0).toInt();

    QString newIp = newConfig.network.controllerIp;
    if (newIp.isEmpty()) newIp = kDefaultControllerIp;
    settings.setValue(kKeyControllerIp, newIp);
    settings.setValue(kKeyControllerPort, newConfig.network.controllerPort);

    m_config = newConfig;
    emit configReceived(withWorkstationOverrides(m_config));

    const RDT::NetProtocol::RobotConfigData rdt_config = mapHmiConfigToRdt(newConfig);
    m_client_state->requestConfigUpdate(rdt_config);

    if (newIp != oldIp || newConfig.network.controllerPort != oldPort) {
        disconnectFromController();
        connectToController(newIp, newConfig.network.controllerPort);
    }
}

void HexaBackend::applyHalConfig(const HmiHalConfig& halConfig) {
    HmiHalConfig updatedConfig = halConfig;
    updatedConfig.requestId = ++m_halRequestId;
    m_config.halConfigCmd = updatedConfig;
    m_client_state->requestHalConfig(mapHmiHalConfigToRdt(updatedConfig));
}

void HexaBackend::connectHalEndpoint(const QString& host, int port) {
    HmiHalConfig updatedConfig = m_config.halConfigCurrent;
    updatedConfig.transportCommand = HmiHalTransportCommand::Connect;
    updatedConfig.transportIp = host.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : host.trimmed();
    updatedConfig.transportPort = port > 0 ? port : static_cast<int>(kDefaultHalPort);
    applyHalConfig(updatedConfig);
}

void HexaBackend::disconnectHalEndpoint() {
    HmiHalConfig updatedConfig = m_config.halConfigCurrent;
    updatedConfig.transportCommand = HmiHalTransportCommand::Disconnect;
    applyHalConfig(updatedConfig);
}

void HexaBackend::setMonitorContext(int toolId, int baseId) {
    m_monitorToolId = toolId;
    m_monitorBaseId = baseId;
}

void HexaBackend::setJogMode(int mode) {
    RDT::NetProtocol::JogFrame frame = RDT::NetProtocol::JogFrame::JOINT;
    if (mode == 1) frame = RDT::NetProtocol::JogFrame::WORLD;
    else if (mode == 2) frame = RDT::NetProtocol::JogFrame::TOOL;
    m_jogFrame = frame;
    m_client_state->setJogContext(frame, m_jogToolId, m_jogBaseId);
}

void HexaBackend::setJogContext(int toolId, int baseId) {
    m_jogToolId = toolId;
    m_jogBaseId = baseId;
    m_client_state->setJogContext(m_jogFrame, m_jogToolId, m_jogBaseId);
}

// ---------------------------------------------------------------------------
// Calibration / homing
// ---------------------------------------------------------------------------
void HexaBackend::masterAxis(int axisIndex) {
    if (axisIndex >= 0 && axisIndex < static_cast<int>(RDT::ROBOT_AXES_COUNT)) {
        qInfo() << "Requesting MASTERING for axis" << axisIndex;
        m_client_state->requestMasterAxis(axisIndex);
    }
}

void HexaBackend::setZeroAll() {
    qInfo() << "Requesting SET ZERO ALL (atomic)";
    m_client_state->requestMasterAllAxes();
}

void HexaBackend::startHomingSequence(int axisIndex) {
    qInfo() << "Requesting HOMING sequence for axis" << axisIndex;
    m_client_state->requestHoming(axisIndex);
}

// ---------------------------------------------------------------------------
// Math helpers
// ---------------------------------------------------------------------------
QVector<double> HexaBackend::axisSetToVector(const RDT::AxisSet& axisSet) {
    QVector<double> result;
    result.reserve(6);
    for (size_t i = 0; i < RDT::ROBOT_AXES_COUNT; ++i) {
        result.append(axisSet[static_cast<RDT::AxisId>(i)].position.value());
    }
    return result;
}

QVector<double> HexaBackend::cartPoseToVector(const RDT::CartPose& pose) {
    QVector<double> result;
    result.reserve(6);
    result.append(pose.x.value());
    result.append(pose.y.value());
    result.append(pose.z.value());
    result.append(pose.rx.value());
    result.append(pose.ry.value());
    result.append(pose.rz.value());
    return result;
}

RDT::CartPose HexaBackend::qVectorToCartPose(const QVector<double>& v) {
    RDT::CartPose pose{};
    if (v.size() > 0) pose.x = RDT::Millimeters(v[0]);
    if (v.size() > 1) pose.y = RDT::Millimeters(v[1]);
    if (v.size() > 2) pose.z = RDT::Millimeters(v[2]);
    if (v.size() > 3) pose.rx = RDT::Degrees(v[3]);
    if (v.size() > 4) pose.ry = RDT::Degrees(v[4]);
    if (v.size() > 5) pose.rz = RDT::Degrees(v[5]);
    return pose;
}

QVector<double> HexaBackend::calculateMonitorPose(const RDT::CartPose& flangePoseWorld,
                                                  int toolId, int baseId) {
    const auto frames = m_client_state->getFrameSet();

    RDT::CartPose toolOffset{};   // identity by default
    for (const auto& t : frames.tools) {
        if (t.id == toolId) { toolOffset = t.offset; break; }
    }
    RDT::CartPose baseOffset{};   // identity by default
    for (const auto& b : frames.bases) {
        if (b.id == baseId) { baseOffset = b.offset; break; }
    }
    // T_tcp_in_base = (T_base)^-1 * T_flange_world * T_tool
    const RDT::CartPose tcpInBase = RDT::pose_math::tcpInFrame(flangePoseWorld, toolOffset, baseOffset);
    return cartPoseToVector(tcpInBase);
}

// ---------------------------------------------------------------------------
// Workstation endpoint override (absorbed adapter piece 2)
// ---------------------------------------------------------------------------
HmiSystemConfig HexaBackend::withWorkstationOverrides(const HmiSystemConfig& config) const {
    HmiSystemConfig merged = config;
    QSettings settings(kOrgName, kAppName);
    merged.network.controllerIp =
        settings.value(kKeyControllerIp, merged.network.controllerIp).toString();
    merged.network.controllerPort =
        settings.value(kKeyControllerPort, merged.network.controllerPort).toInt();
    return merged;
}

// ---------------------------------------------------------------------------
// Config mappers
// ---------------------------------------------------------------------------
void HexaBackend::mapRdtConfigToHmi(const RDT::NetProtocol::RobotConfigData& rdt_config,
                                    HmiSystemConfig& hmi_config) {
    hmi_config.network.controllerIp = QString::fromStdString(rdt_config.ipAddress);
    hmi_config.robotUrdfPath = QString::fromStdString(rdt_config.urdfPath);
    hmi_config.realtimeBackend = QString::fromStdString(
        rdt_config.realtimeBackend.empty() ? std::string("sim") : rdt_config.realtimeBackend);
    hmi_config.modelRootX = rdt_config.modelRootX;
    hmi_config.modelRootY = rdt_config.modelRootY;
    hmi_config.modelRootZ = rdt_config.modelRootZ;
    hmi_config.modelRootRxDeg = rdt_config.modelRootRxDeg;
    hmi_config.modelRootRyDeg = rdt_config.modelRootRyDeg;
    hmi_config.modelRootRzDeg = rdt_config.modelRootRzDeg;

    hmi_config.tools.clear();
    for (const auto& rdt_tool : rdt_config.frames.tools) {
        HmiToolData hmi_tool;
        hmi_tool.id = rdt_tool.id;
        hmi_tool.name = QString::fromStdString(rdt_tool.name);
        hmi_tool.offset = cartPoseToVector(rdt_tool.offset);
        hmi_config.tools.append(hmi_tool);
    }

    hmi_config.bases.clear();
    for (const auto& rdt_base : rdt_config.frames.bases) {
        HmiBaseData hmi_base;
        hmi_base.id = rdt_base.id;
        hmi_base.name = QString::fromStdString(rdt_base.name);
        hmi_base.offset = cartPoseToVector(rdt_base.offset);
        hmi_config.bases.append(hmi_base);
    }

    hmi_config.axisLimits.clear();
    for (size_t i = 0; i < RDT::ROBOT_AXES_COUNT; ++i) {
        HmiAxisLimit hmi_limit;
        hmi_limit.axisIndex = static_cast<int>(i);
        hmi_limit.minDeg = rdt_config.definition.axis_limits[i].first.value();
        hmi_limit.maxDeg = rdt_config.definition.axis_limits[i].second.value();
        hmi_config.axisLimits.append(hmi_limit);
    }
}

RDT::NetProtocol::RobotConfigData HexaBackend::mapHmiConfigToRdt(const HmiSystemConfig& hmi_config) {
    RDT::NetProtocol::RobotConfigData rdt_config;
    // Keep the controller connection endpoint (Studio-local) decoupled from the server realtime
    // endpoint: preserve the server-side realtime IP from synced config state.
    std::string realtime_ip = m_client_state ? m_client_state->getConfigIpAddress() : std::string{};
    if (realtime_ip.empty()) {
        realtime_ip = hmi_config.network.controllerIp.toStdString();
    }
    rdt_config.ipAddress = realtime_ip;
    rdt_config.urdfPath = hmi_config.robotUrdfPath.toStdString();
    rdt_config.realtimeBackend = hmi_config.realtimeBackend.isEmpty()
                                     ? std::string("sim")
                                     : hmi_config.realtimeBackend.toStdString();
    rdt_config.modelRootX = hmi_config.modelRootX;
    rdt_config.modelRootY = hmi_config.modelRootY;
    rdt_config.modelRootZ = hmi_config.modelRootZ;
    rdt_config.modelRootRxDeg = hmi_config.modelRootRxDeg;
    rdt_config.modelRootRyDeg = hmi_config.modelRootRyDeg;
    rdt_config.modelRootRzDeg = hmi_config.modelRootRzDeg;

    for (const auto& hmi_tool : hmi_config.tools) {
        RDT::ToolData rdt_tool;
        rdt_tool.id = hmi_tool.id;
        rdt_tool.name = hmi_tool.name.toStdString();
        rdt_tool.offset = qVectorToCartPose(hmi_tool.offset);
        rdt_config.frames.tools.push_back(rdt_tool);
    }
    for (const auto& hmi_base : hmi_config.bases) {
        RDT::ToolData rdt_base;
        rdt_base.id = hmi_base.id;
        rdt_base.name = hmi_base.name.toStdString();
        rdt_base.offset = qVectorToCartPose(hmi_base.offset);
        rdt_config.frames.bases.push_back(rdt_base);
    }
    for (size_t i = 0; i < RDT::ROBOT_AXES_COUNT; ++i) {
        if (i < static_cast<size_t>(hmi_config.axisLimits.size())) {
            rdt_config.definition.axis_limits[i].first = RDT::Degrees(hmi_config.axisLimits[i].minDeg);
            rdt_config.definition.axis_limits[i].second = RDT::Degrees(hmi_config.axisLimits[i].maxDeg);
        }
    }
    return rdt_config;
}

// ---------------------------------------------------------------------------
// HAL config mappers
// ---------------------------------------------------------------------------
void HexaBackend::mapRdtHalConfigToHmi(const RDT::NetProtocol::HalConfigState& rdt_hal,
                                       HmiHalConfig& hmi_hal) {
    hmi_hal.requestId = rdt_hal.appliedRequestId;
    hmi_hal.transportCommand = HmiHalTransportCommand::None;
    hmi_hal.transportIp = QString::fromStdString(rdt_hal.transport_ip);
    if (hmi_hal.transportIp.isEmpty()) {
        hmi_hal.transportIp = QStringLiteral("127.0.0.1");
    }
    hmi_hal.transportPort = rdt_hal.transport_port > 0 ? static_cast<int>(rdt_hal.transport_port)
                                                       : static_cast<int>(kDefaultHalPort);
    hmi_hal.transportConnected = rdt_hal.transport_connected;

    hmi_hal.homingSequenceActive = rdt_hal.homing.sequence_active;
    hmi_hal.homingState = QString::fromStdString(rdt_hal.homing.state);
    hmi_hal.homingCurrentIndex = rdt_hal.homing.current_index;
    hmi_hal.homingAxisCount = rdt_hal.homing.axis_count;
    hmi_hal.homingCurrentAxisId = rdt_hal.homing.current_axis_id;
    hmi_hal.homingDiagnostic = QString::fromStdString(rdt_hal.homing.diagnostic);
    hmi_hal.controlOwnerId = rdt_hal.control_owner_id;
    hmi_hal.lastIpcError = QString::fromStdString(rdt_hal.last_ipc_error);

    hmi_hal.axes.clear();
    hmi_hal.axes.reserve(static_cast<int>(RDT::ROBOT_AXES_COUNT));
    for (size_t i = 0; i < RDT::ROBOT_AXES_COUNT; ++i) {
        HmiHalAxisConfig axis;
        axis.axisIndex = static_cast<int>(i);
        axis.motorEnabled = rdt_hal.axes[i].motor_enabled;
        axis.softLimitsEnabled = rdt_hal.axes[i].soft_limits_enabled;
        axis.softLimitMinDeg = rdt_hal.axes[i].soft_limit_min.value();
        axis.softLimitMaxDeg = rdt_hal.axes[i].soft_limit_max.value();
        axis.velocityLimitEnabled = rdt_hal.axes[i].velocity_limit_enabled;
        axis.velocityLimitDegPerSec = rdt_hal.axes[i].velocity_limit.value();
        axis.lastStatus = rdt_hal.axes[i].last_status;
        axis.lastErrorCode = rdt_hal.axes[i].last_error_code;
        hmi_hal.axes.append(axis);
    }
}

RDT::NetProtocol::HalConfigCommand HexaBackend::mapHmiHalConfigToRdt(const HmiHalConfig& hmi_hal) {
    RDT::NetProtocol::HalConfigCommand rdt_hal;
    rdt_hal.requestId = hmi_hal.requestId;
    switch (hmi_hal.transportCommand) {
        case HmiHalTransportCommand::Connect:
            rdt_hal.transport_command = RDT::HalTransportCommand::Connect; break;
        case HmiHalTransportCommand::Disconnect:
            rdt_hal.transport_command = RDT::HalTransportCommand::Disconnect; break;
        case HmiHalTransportCommand::None:
        default:
            rdt_hal.transport_command = RDT::HalTransportCommand::None; break;
    }
    rdt_hal.transport_ip = hmi_hal.transportIp.trimmed().isEmpty()
                               ? std::string("127.0.0.1")
                               : hmi_hal.transportIp.trimmed().toStdString();
    rdt_hal.transport_port = hmi_hal.transportPort > 0 ? static_cast<uint16_t>(hmi_hal.transportPort)
                                                       : kDefaultHalPort;
    for (int i = 0; i < hmi_hal.axes.size() && i < static_cast<int>(RDT::ROBOT_AXES_COUNT); ++i) {
        const auto& src = hmi_hal.axes[i];
        auto& dst = rdt_hal.axes[static_cast<size_t>(i)];
        dst.update_mask = RDT::NetProtocol::HalConfigUpdate_MotorEnabled |
                          RDT::NetProtocol::HalConfigUpdate_SoftLimits |
                          RDT::NetProtocol::HalConfigUpdate_VelocityLimit;
        dst.motor_enabled = src.motorEnabled;
        dst.soft_limits_enabled = src.softLimitsEnabled;
        dst.soft_limit_min = RDT::Degrees(src.softLimitMinDeg);
        dst.soft_limit_max = RDT::Degrees(src.softLimitMaxDeg);
        dst.velocity_limit_enabled = src.velocityLimitEnabled;
        dst.velocity_limit = RDT::DegreesPerSecond(src.velocityLimitDegPerSec);
    }
    return rdt_hal;
}

} // namespace hexa
// --- END OF FILE: HexaStudio/hexa_backend/HexaBackend.cpp ---
