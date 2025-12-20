/**
 * @file BackendTypes.h
 * @brief Defines the Data Transfer Objects (DTOs) for the HMI-Controller communication layer.
 * @author HexaKinetica Team
 * @version 1.0
 *
 * This file contains the C++ structures used to bridge the gap between the raw
 * JSON protocol (RdtProtocol) and the Qt-based UI. These structures use Qt types
 * (QVector, QString) to allow easy integration with Qt's meta-object system
 * (Signals/Slots).
 *
 * @note The architecture strictly separates "Fast Path" data (Telemetry) from
 * "Slow Path" data (Configuration/Transactions).
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
// Frequency: High (~20Hz - 100Hz).
// Semantics: Snapshot-based. Old data is discarded.
// =============================================================================

/**
 * @brief High-level system indicators.
 * Contains global flags regarding connectivity, safety, and hardware health.
 * Used primarily by PanelTop.
 */
struct HmiTopStatus {
    /// @brief True if TCP connection to Controller is active.
    bool isConnected;

    /// @brief True if Hardware or Software Emergency Stop is engaged.
    bool isEStop;

    /// @brief True if servo motors are powered and brakes are released.
    bool isPowerOn;

    /// @brief Current operation mode (e.g., "T1" (Manual), "AUTO", "EXT").
    QString mode;

    /// @brief Concatenated string of active error codes or messages. Empty if healthy.
    QString activeErrors;

    /// @brief Controller battery level (0-100%).
    int batteryLevel;

    // --- Telemetry ---

    /// @brief Controller CPU usage (0.0 - 100.0%).
    double cpuLoad;

    /// @brief Main CPU/Drive temperature in Celsius.
    double controllerTemp;

    /// @brief Round-trip time (RTT) in milliseconds.
    double networkLatency;
};

/**
 * @brief Kinematic state of the robot.
 * Critical for the 3D visualization and coordinate monitoring.
 * Used by PanelView3D and PanelRight.
 */
struct HmiMotionStatus {
    // --- HIL (Hardware-in-the-Loop) Fields ---

    /**
     * @brief Actual joint angles from motor encoders.
     * @details Represents the physical reality. Used to render the "Solid" robot model.
     * Units: Degrees.
     * Size: 6.
     */
    QVector<double> actualJoints;

    /**
     * @brief Actual Cartesian pose of the TCP.
     * @details Calculated via Forward Kinematics (FK) on the Controller side.
     * Format: {X, Y, Z, A, B, C}. Units: mm, degrees.
     */
    QVector<double> actualTcp;

    /**
     * @brief Target joint angles from the Motion Planner.
     * @details Represents where the robot *wants* to be. Used to render the "Ghost" robot model.
     * During jogging, this updates immediately, while `actualJoints` lags behind due to physics.
     * Units: Degrees.
     */
    QVector<double> plannedJoints;

    /**
     * @brief Target Cartesian pose from the Motion Planner.
     * Format: {X, Y, Z, A, B, C}. Units: mm, degrees.
     */
    QVector<double> plannedTcp;

    /**
     * @brief Pose relative to the currently selected User Frame (Base).
     * Used for the coordinate display in PanelRight.
     */
    QVector<double> monitorPose;

    // --- UI Helper Fields ---

    /// @brief Helper for binding to UI graphs (usually mirrors plannedJoints).
    QVector<double> displayJoints;

    /// @brief Helper for binding to UI graphs (usually mirrors monitorPose).
    QVector<double> displayTcp;

    /// @brief True if the trajectory generator is currently executing a motion.
    bool isMoving;

    /// @brief True if the Controller is in Physics Simulation mode (Mock physics).
    bool isSimulated;

    /// @brief Global speed scaling factor (0.0 - 1.0).
    double speedOverride;

    /// @brief ID of the currently active Tool (TCP).
    int activeToolId;

    /// @brief ID of the currently active Base (User Frame).
    int activeBaseId;
};

/**
 * @brief State of the Program Interpreter.
 * Used by PanelLeft.
 */
struct HmiProgramStatus {
    /// @brief True if a program is currently executing.
    bool isRunning;

    /// @brief True if execution is suspended (can be resumed).
    bool isPaused;

    /// @brief Name of the loaded program file.
    QString loadedProgramName;

    /// @brief Current line index (Program Pointer) being executed. -1 if stopped.
    int currentRowIndex;
};

/**
 * @brief The Aggregator DTO.
 * This is the main object passed via the `RobotService::stateChanged` signal.
 * It represents a complete, atomic snapshot of the robot state at `timestamp`.
 */
struct HmiRobotStatus {
    /// @brief Time of packet generation on the Controller (Unix Epoch ms).
    quint64 timestamp;

    HmiTopStatus top;
    HmiMotionStatus motion;
    HmiProgramStatus prog;

    /// @brief Default constructor initializes fields to safe defaults.
    HmiRobotStatus() {
        timestamp = 0;
        top.isConnected = false;
        top.isEStop = false;
        top.isPowerOn = false;
        top.mode = "T1";
        top.batteryLevel = 100;
        top.cpuLoad = 0.0;
        top.controllerTemp = 0.0;
        top.networkLatency = 0.0;

        motion.actualJoints.fill(0.0, 6);
        motion.actualTcp.fill(0.0, 6);
        motion.plannedJoints.fill(0.0, 6);
        motion.plannedTcp.fill(0.0, 6);
        motion.monitorPose.fill(0.0, 6);
        motion.displayJoints.fill(0.0, 6);
        motion.displayTcp.fill(0.0, 6);
        motion.isMoving = false;
        motion.isSimulated = true;
        motion.speedOverride = 0.5;

        prog.isRunning = false;
        prog.isPaused = false;
        prog.currentRowIndex = -1;
    }
};

// =============================================================================
// SLOW PATH (Configuration & Data)
// Frequency: On Demand (Events).
// Semantics: Transactional. Must be acknowledged.
// =============================================================================

/**
 * @brief Definition of a Tool Center Point (TCP).
 * Qt-compatible version of Rdt::ToolData.
 */
struct HmiToolData {
    int id = 0;
    QString name = "Tool";
    /// @brief Offset relative to Flange {X, Y, Z, Rx, Ry, Rz}.
    QVector<double> offset = {0,0,0,0,0,0};
};

/**
 * @brief Definition of a User Coordinate System (Base).
 * Qt-compatible version of Rdt::ToolData (Base context).
 */
struct HmiBaseData {
    int id = 0;
    QString name = "Base";
    /// @brief Offset relative to World {X, Y, Z, Rx, Ry, Rz}.
    QVector<double> offset = {0,0,0,0,0,0};
};

/**
 * @brief Network connection parameters.
 */
struct HmiNetworkConfig {
    QString controllerIp = "127.0.0.1";
    int controllerPort = 30002;
    QString hmiIp = "127.0.0.1";
};

/**
 * @brief Software limits for a single joint.
 */
struct HmiAxisLimit {
    int axisIndex = 0;
    /// @brief Soft limit minimum (Degrees).
    double minDeg = -180.0;
    /// @brief Soft limit maximum (Degrees).
    double maxDeg = 180.0;
    /// @brief Maximum velocity (Degrees/sec).
    double maxVelDegS = 100.0;
};

/**
 * @brief The Master Configuration object.
 * Synced from Controller to HMI on connection.
 */
struct HmiSystemConfig {
    QVector<HmiToolData> tools;
    QVector<HmiBaseData> bases;
    HmiNetworkConfig network;
    QVector<HmiAxisLimit> axisLimits;
};

/**
 * @brief Pre-calculated trajectory for visualization.
 * @details Received when the program changes. Used by PanelView3D.
 */
struct HmiTrajectoryData {
    /// @brief Dense list of points for drawing the continuous line (LineStrip).
    QVector<QVector3D> path;

    /// @brief Sparse list of points representing command targets (Points).
    QVector<QVector3D> waypoints;

    bool isEmpty() const { return path.isEmpty() && waypoints.isEmpty(); }
};

// Register types for Qt Signal/Slot system
Q_DECLARE_METATYPE(HmiRobotStatus)
Q_DECLARE_METATYPE(HmiSystemConfig)
Q_DECLARE_METATYPE(QVector<QVector3D>)
Q_DECLARE_METATYPE(HmiTrajectoryData)

#endif // BACKENDTYPES_H
