/**
 * @file RobotService.h
 * @brief The central backend service for HMI-Controller communication.
 * @author HexaKinetica Team
 * @version 1.0
 *
 * This file defines the `RobotService` class, which acts as the Gateway and State Manager
 * for the HMI application. It implements the Singleton pattern to provide global access
 * to robot data from any UI panel.
 *
 * @details
 * **Architecture:**
 * - **Network Layer:** Manages a persistent TCP connection to the Robot Controller.
 * - **State Management:** Maintains the "Source of Truth" for the HMI (`m_status`).
 * - **Synchronization:** Implements a two-way sync protocol using transaction IDs and version counters
 *   to ensure configuration and programs are consistent between HMI and Controller.
 */

#ifndef ROBOTSERVICE_H
#define ROBOTSERVICE_H

#include <QObject>
#include <QTimer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QVector3D>
#include "BackendTypes.h"
#include "../../shared/RdtProtocol.h"
#include "../panels/left/ProgramData.h"

/**
 * @brief Singleton service managing the RDT protocol and robot state.
 *
 * The RobotService operates on the main thread (to simplify UI updates) but uses
 * non-blocking socket I/O. It periodically flushes the `ControlState` to the controller
 * and parses incoming `RobotStatus` packets.
 */
class RobotService : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Access the singleton instance.
     * @return Pointer to the global RobotService. Creates it if it doesn't exist.
     */
    static RobotService* instance();

    /**
     * @brief Get the last received robot status.
     * @return Constant reference to the status snapshot.
     */
    const HmiRobotStatus& getStatus() const { return m_status; }

    /**
     * @brief Get the currently active system configuration.
     * @return Constant reference to the config.
     */
    const HmiSystemConfig& getConfig() const { return m_config; }

    /**
     * @brief Get the currently loaded program structure.
     * @return Vector of program commands.
     */
    const QVector<ProgramCommand>& getCachedProgram() const { return m_cachedProgram; }

    /**
     * @brief Get the currently active trajectory path.
     * @return Trajectory data for 3D visualization.
     */
    const HmiTrajectoryData& getCachedTrajectory() const { return m_cachedTrajectory; }

public slots:
    // --- Control Commands ---

    /**
     * @brief Toggle between Simulation and Real Robot modes.
     * @param isReal True for Hardware control, False for Physics Simulation.
     */
    void setMode(bool isReal);

    /**
     * @brief Trigger or Reset the Emergency Stop.
     * @param active True to engage E-Stop, False to reset/acknowledge.
     */
    void setEStop(bool active);

    /**
     * @brief Set the global speed override factor.
     * @param percent Speed percentage (1-100).
     */
    void setSpeedOverride(int percent);

    /**
     * @brief Request a manual jog movement of a specific joint.
     * @param axis Axis index (0-5).
     * @param degrees Increment in degrees (positive or negative).
     */
    void jogJointIncremental(int axis, double degrees);

    /**
     * @brief Stop any active jogging operation immediately.
     */
    void stopJog();

    /**
     * @brief Command the robot to move to the hardcoded Home position.
     */
    void moveHome();

    // --- Program Execution ---

    /**
     * @brief Upload and immediately execute a program.
     * @param program The list of commands to run.
     */
    void startProgram(const QVector<ProgramCommand> &program);

    /**
     * @brief Upload a program to the controller without starting it.
     * @details Useful for validating the program and generating the trajectory visualization.
     * @param program The list of commands to upload.
     */
    void uploadProgram(const QVector<ProgramCommand> &program);

    /**
     * @brief Stop the currently running program.
     */
    void stopProgram();

    /**
     * @brief Pause the currently running program.
     */
    void pauseProgram();

    /**
     * @brief Set the program pointer for preview/debugging (Not fully implemented).
     * @param stepIndex Index of the step to highlight.
     */
    void setPreviewStep(int stepIndex);

    // --- Configuration & Context ---

    /**
     * @brief Force a re-synchronization of settings from the Controller.
     * @details Resets local version counters to 0.
     */
    void requestSettings();

    /**
     * @brief Apply new settings to the Controller.
     * @param newConfig The new configuration object.
     */
    void applySettings(const HmiSystemConfig &newConfig);

    /**
     * @brief Set the active coordinate systems for monitoring.
     * @param toolId ID of the tool to use for TCP calculation.
     * @param baseId ID of the base frame for relative coordinates.
     */
    void setMonitorContext(int toolId, int baseId);

    // --- Connection ---

    /**
     * @brief Manually initiate connection to the controller.
     * @param ip Target IP address.
     * @param port Target TCP port.
     */
    void connectToController(const QString &ip, int port);

    /**
     * @brief Disconnect from the controller.
     */
    void disconnectFromController();

signals:
    /**
     * @brief Emitted whenever a valid status packet is received and parsed.
     * @param status The new robot state.
     */
    void stateChanged(const HmiRobotStatus &status);

    /**
     * @brief Emitted when a new configuration is received from the controller.
     * @param config The system configuration.
     */
    void settingsReceived(const HmiSystemConfig &config);

    /**
     * @brief Emitted when a new trajectory path is received (usually after program upload).
     * @param data The 3D path data.
     */
    void trajectoryReceived(const HmiTrajectoryData &data);

    /**
     * @brief Emitted when a program is successfully loaded from the controller.
     * @param program The program structure.
     */
    void programLoaded(const QVector<ProgramCommand> &program);

    /**
     * @brief Emitted on critical system events or errors.
     * @param msg The message text.
     * @param isError True if it's an error, False if info.
     */
    void messageOccurred(const QString &msg, bool isError);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError socketError);

    /**
     * @brief Periodic timer slot to send the ControlState.
     * @details Runs at ~50Hz.
     */
    void onStateFlushTimer();

private:
    explicit RobotService(QObject *parent = nullptr);
    static RobotService* m_instance;

    // --- State Storage ---
    HmiRobotStatus m_status;
    HmiSystemConfig m_config;
    Rdt::ControlState m_controlState;

    /**
     * @brief Monotonically increasing counter for transaction IDs.
     * Ensures outgoing requests (like config updates) are unique and trackable.
     */
    uint32_t m_globalRequestId = 0;

    // --- Caches ---
    QVector<ProgramCommand> m_cachedProgram;
    HmiTrajectoryData m_cachedTrajectory;

    // --- Network ---
    QTcpSocket *m_socket;
    QByteArray m_receiveBuffer;
    QTimer *m_flushTimer;
    QTimer *m_reconnectTimer;

    // --- Internal Helpers ---
    void processJsonPacket(const QJsonObject &root);
    void handleStatusUpdate(const QJsonObject &payload);
    void flushControlState();

    void fillProgramData(const QVector<ProgramCommand> &src, Rdt::ProgramData &dst);

    /// @brief Generates the next unique request ID.
    uint32_t nextRequestId() { return ++m_globalRequestId; }
};

#endif // ROBOTSERVICE_H
