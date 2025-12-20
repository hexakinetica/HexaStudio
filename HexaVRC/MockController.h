/**
 * @file MockController.h
 * @brief Core logic of the Robot Controller Emulator.
 * @author HexaKinetica Team
 * @version 1.0
 *
 * This file defines the `MockController` class, which simulates the behavior
 * of a real industrial robot controller. It integrates the Network Bridge,
 * Kinematics Math, Configuration Management, and Program Execution.
 */

#ifndef MOCKCONTROLLER_H
#define MOCKCONTROLLER_H

#include "../shared/RdtProtocol.h"
#include "ConfigManager.h"
#include "HmiBridge.h"
#include "MockMath.h"
#include <vector>
#include <array>
#include <thread>
#include <atomic>

/**
 * @brief Logic for executing a robot program step-by-step.
 *
 * Handles linear interpolation between program steps based on the
 * defined speed and the global speed override.
 */
struct ProgramExecutor {
    std::vector<Rdt::ProgramStep> steps;
    int currentStepIndex = -1;
    bool isRunning = false;

    /// @brief Interpolation progress (0.0 to 1.0) within the current step.
    double interpolationFactor = 0.0;

    /// @brief Joint angles at the start of the current step.
    std::array<double, 6> startJoints = {0};

    void load(const Rdt::ProgramData& prog);
    void start();
    void stop();
    void pause();

    /**
     * @brief Advances the execution logic.
     * @param dt Delta time in seconds.
     * @param speedOverride Global speed factor (0.0 - 1.0).
     * @param state The motion state to update (modifies plannedJoints).
     */
    void update(double dt, double speedOverride, Rdt::MotionState& state);
};

/**
 * @brief The Main Controller Class.
 *
 * @details
 * **Responsibilities:**
 * - **Physics Loop:** Runs at 250Hz. Simulates joint movement (Jogging/Program).
 * - **Safety:** Handles E-Stop logic.
 * - **State Management:** Maintains the authoritative `RobotStatus`.
 * - **Persistency:** Loads/Saves `mock_config.json` via `ConfigManager`.
 * - **HMI Sync:** Processes `ControlState` and broadcasts `RobotStatus`.
 */
class MockController {
public:
    MockController();
    ~MockController();

    /**
     * @brief Starts the main simulation loop.
     * @details This method blocks until the application is terminated.
     */
    void run();

private:
    /**
     * @brief Updates robot physics (movement interpolation).
     * @param dt Time step in seconds.
     */
    void updatePhysics(double dt);

    /**
     * @brief Processes input from the HMI.
     * @param input The control packet received from HMI.
     */
    void handleInput(const Rdt::ControlState& input);

    /**
     * @brief Updates simulated sensors (Temp, CPU, Latency).
     */
    void updateMonitorData(double dt);

    /**
     * @brief Pre-calculates the trajectory path for the loaded program.
     * @details Generates dense points for visualization on the HMI.
     */
    void generateTrajectory(const Rdt::ProgramData& prog);

    void moveHome(double dt);

    /**
     * @brief Helper for smooth joint movement towards a target.
     * @param dt Delta time.
     * @param speedRatio Movement speed factor.
     */
    void moveTowardsTarget(double dt, double speedRatio);

    // --- Components ---
    HmiBridge m_bridge;
    ConfigManager m_configMgr;
    ProgramExecutor m_executor;

    // --- State ---
    Rdt::RobotStatus m_status;
    Rdt::ControlState m_lastInput;
    Rdt::ProgramData m_loadedProgram;

    // --- Physics Internal State ---
    std::array<double, 6> m_targetJoints; ///< Target for manual jogging.
    bool m_isMovingHome = false;
    double m_simTime = 0.0;

    // --- Versioning for Sync ---
    uint32_t m_configVer = 1;
    uint32_t m_trajVer = 1;
    uint32_t m_progVer = 1;

    // Transaction IDs (Deduping)
    uint32_t m_lastProcessedJogId = 0;
    uint32_t m_lastProcessedConfigReqId = 0;
    uint32_t m_lastProcessedProgramReqId = 0;

    const std::array<double, 6> home = {0.0, -90.0, 90.0, 0.0, 0.0, 0.0};
    const double JOG_SPEED_DEG_S = 60.0;
};

#endif // MOCKCONTROLLER_H
