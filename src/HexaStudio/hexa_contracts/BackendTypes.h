/**
 * @file BackendTypes.h
 * @brief Defines the Data Transfer Objects (DTOs) for the HMI-Controller communication layer.
 *
 * NOTE: This file is preserved to maintain a stable interface for the UI panels,
 * decoupling them from the raw RDT protocol structures.
 */

#ifndef BACKENDTYPES_H
#define BACKENDTYPES_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QMetaType>
#include <QVector3D>
#include <QMap>
#include <QVariant>

// =============================================================================
// FAST PATH (Telemetry & Status)
// =============================================================================

struct HmiTopStatus {
    bool isConnected;
    bool isEStop;
    bool isPowerOn;
    bool hasError = false;
    QString mode;
    QString activeErrors;
    double cpuLoad;
    double controllerTemp;
    double networkLatency;
};

struct HmiMotionStatus {
    QVector<double> actualJoints;
    QVector<double> actualTcp;
    QVector<double> plannedJoints;       // COMMANDED joints -> SOLID robot (Convention B)
    QVector<double> realHardwareJoints;  // PHYSICAL robot joints -> GHOST (Convention B)
    QVector<double> plannedTcp;
    QVector<double> monitorPose;
    QVector<double> displayJoints;
    QVector<double> displayTcp;
    bool isMoving;
    bool robotMoving;   // RT manager Moving (program OR jog) — drives jog-button gating
    bool isSimulated;
    double speedOverride;
    int activeToolId;
    int activeBaseId;
    int currentJogFrame = 0;
    bool jogEnabled = false;
    QString jogNotice;  // Transient jog warning for the jog panel (e.g. "axis limit / unreachable").

    // Diagnostics
    QString plannerStatus;
    QString safetyStatus;
    QString interpolatorStatus;
    QString halStatus;
    int failingAxisId;
};

struct HmiProgramStatus {
    bool isRunning;
    bool isPaused;
    QString loadedProgramName;
    int currentRowIndex;
    // P5 execution annotation: the sequencer's live register file (the loop counters) and the
    // last evaluated IF (step index + whether it jumped). -1 = no IF evaluated this run.
    QVector<int> registers;
    int lastBranchLine = -1;
    bool lastBranchTaken = false;
};

struct HmiRobotStatus {
    quint64 timestamp;
    HmiTopStatus top;
    HmiMotionStatus motion;
    HmiProgramStatus prog;

    HmiRobotStatus() {
        timestamp = 0;
        top.isConnected = false;
        top.isEStop = false;
        top.isPowerOn = false;
        top.mode = "T1";
        top.cpuLoad = 0.0;
        top.controllerTemp = 0.0;
        top.networkLatency = 0.0;

        motion.actualJoints.fill(0.0, 6);
        motion.actualTcp.fill(0.0, 6);
        motion.plannedJoints.fill(0.0, 6);
        motion.realHardwareJoints.fill(0.0, 6);
        motion.plannedTcp.fill(0.0, 6);
        motion.monitorPose.fill(0.0, 6);
        motion.displayJoints.fill(0.0, 6);
        motion.displayTcp.fill(0.0, 6);
        motion.isMoving = false;
        motion.robotMoving = false;
        motion.isSimulated = true;
        motion.speedOverride = 0.5;
        motion.activeToolId = 0;
        motion.activeBaseId = 0;
        motion.currentJogFrame = 0;
        motion.jogEnabled = false;

        prog.isRunning = false;
        prog.isPaused = false;
        prog.currentRowIndex = -1;
    }
};

// =============================================================================
// SLOW PATH (Configuration & Data)
// =============================================================================

struct HmiToolData {
    int id = 0;
    QString name = "Tool";
    QVector<double> offset = {0,0,0,0,0,0};
};

struct HmiBaseData {
    int id = 0;
    QString name = "Base";
    QVector<double> offset = {0,0,0,0,0,0};
};

struct HmiNetworkConfig {
    QString controllerIp = "127.0.0.1";
    int controllerPort = 30002;
    QString hmiIp = "127.0.0.1";
};

struct HmiAxisLimit {
    int axisIndex = 0;
    double minDeg = -180.0;
    double maxDeg = 180.0;
};

struct HmiHalAxisConfig {
    int axisIndex = 0;
    bool motorEnabled = false;
    bool softLimitsEnabled = false;
    double softLimitMinDeg = -180.0;
    double softLimitMaxDeg = 180.0;
    bool velocityLimitEnabled = false;
    double velocityLimitDegPerSec = 100.0;
    int lastStatus = 0;
    int lastErrorCode = 0;
};

enum class HmiHalTransportCommand : int {
    None = 0,
    Connect = 1,
    Disconnect = 2
};

struct HmiHalConfig {
    uint32_t requestId = 0;
    QVector<HmiHalAxisConfig> axes;
    HmiHalTransportCommand transportCommand = HmiHalTransportCommand::None;
    QString transportIp = "127.0.0.1";
    int transportPort = 30110;
    bool transportConnected = false;

    // Read-only mirror of the backend's homing-sequence status (owned by the Motor
    // Configurator). Per-axis homing phase is shown separately from each axis lastStatus.
    bool homingSequenceActive = false;
    QString homingState;            // "Idle"/"WaitingAxisActive"/"WaitingAxisComplete"/"Completed"/"Aborted"
    int homingCurrentIndex = 0;
    int homingAxisCount = 0;
    int homingCurrentAxisId = 0;
    QString homingDiagnostic;

    // Active motion owner reported by the backend (0 = UI/local, 1 = HexaMotion).
    int controlOwnerId = 1;

    // Last application-level error reported by the Motor Configurator (sticky; empty = none).
    QString lastIpcError;
};

struct HmiSystemConfig {
    QVector<HmiToolData> tools;
    QVector<HmiBaseData> bases;
    HmiNetworkConfig network;
    QVector<HmiAxisLimit> axisLimits;
    HmiHalConfig halConfigCmd;
    HmiHalConfig halConfigCurrent;
    QString robotUrdfPath;
    double modelRootX = 0.0;
    double modelRootY = 0.0;
    double modelRootZ = 0.0;
    // Physical mounting transform; default identity. The Z-up->Y-up render convention is a
    // fixed visual transform in PanelView3D, not part of this. Keep in sync with RobotConfigData.
    double modelRootRxDeg = 0.0;
    double modelRootRyDeg = 0.0;
    double modelRootRzDeg = 0.0;
    // Realtime HAL backend the controller drives ("sim"/"udp"/"mks_tcp"). Selected in Settings;
    // applied by the controller on its next start. Keep in sync with RobotConfigData.realtimeBackend.
    QString realtimeBackend = "sim";
};

struct HmiTrajectoryData {
    QVector<QVector3D> path;
    QVector<QVector3D> waypoints;
    bool isEmpty() const { return path.isEmpty() && waypoints.isEmpty(); }
};

// Register types for Qt Signal/Slot system
Q_DECLARE_METATYPE(HmiRobotStatus)
Q_DECLARE_METATYPE(HmiSystemConfig)
Q_DECLARE_METATYPE(QVector<QVector3D>)
Q_DECLARE_METATYPE(HmiTrajectoryData)

#endif // BACKENDTYPES_H
