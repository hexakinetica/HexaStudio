/**
 * @file MockController.cpp
 * @brief Implementation of the Controller Logic.
 */

#include "MockController.h"
#include "MockJsonHelpers.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <chrono>
#include <algorithm> // for std::max, std::min

using namespace Rdt;
using json = nlohmann::json;

// =============================================================================
// ProgramExecutor Implementation
// =============================================================================

void ProgramExecutor::load(const ProgramData& prog) {
    steps = prog.steps;
    currentStepIndex = -1;
    isRunning = false;
    std::cout << "[EXEC] Program Loaded: " << prog.name << " (" << steps.size() << " steps)" << std::endl;
}

void ProgramExecutor::start() {
    if (steps.empty()) return;
    currentStepIndex = -1;
    isRunning = true;
}

void ProgramExecutor::stop() { isRunning = false; currentStepIndex = -1; }
void ProgramExecutor::pause() { isRunning = false; }

/**
 * @brief Updates the planned joints based on the current program step.
 * Uses simple linear interpolation (Lerp) between steps.
 */
void ProgramExecutor::update(double dt, double speedOverride, MotionState& state) {
    if (!isRunning) return;

    // Initialization logic for the first step or transition
    if (currentStepIndex == -1) {
        currentStepIndex = 0;
        interpolationFactor = 0.0;
        startJoints = state.plannedJoints;
    }

    if (currentStepIndex >= steps.size()) {
        stop();
        return;
    }

    auto& step = steps[currentStepIndex];

    // Calculate step speed based on program speed + global override
    // Base speed factor: 1.0 (arbitrary scale for simulation)
    double stepSpeed = (step.speed / 100.0) * speedOverride * 1.0;
    if (stepSpeed < 0.01) stepSpeed = 0.01;

    interpolationFactor += stepSpeed * dt;

    if (interpolationFactor >= 1.0) {
        // Step finished, snap to target
        state.plannedJoints = step.targetJoints;
        currentStepIndex++;
        interpolationFactor = 0.0;
        startJoints = state.plannedJoints;
    } else {
        // Interpolate
        for(int i=0; i<6; ++i) {
            state.plannedJoints[i] = startJoints[i] + (step.targetJoints[i] - startJoints[i]) * interpolationFactor;
        }
    }
}

// =============================================================================
// MockController Implementation
// =============================================================================

MockController::MockController() : m_configMgr("mock_config.json") {
    // Initialize State
    m_status.sys.isPowerOn = true;
    m_status.sys.isEStop = false;

    m_status.motion.actualJoints = home;
    m_status.motion.plannedJoints = home;
    m_targetJoints = home; // Initialize target to home
    m_status.motion.isSimulated = true;

    // Load Config
    m_status.config = m_configMgr.load();
    m_configVer = 1;
    m_trajVer = 1;
    m_progVer = 1;

    // Setup Bridge Callbacks
    m_bridge.setConfigProvider([this]() { return m_status.config; });
    m_bridge.setControlStateHandler([this](const ControlState& cs) { this->handleInput(cs); });
    m_bridge.setProgramUploadHandler([this](const ProgramData& prog) {
        m_loadedProgram = prog;
        m_executor.load(prog);
        generateTrajectory(prog);
        m_progVer++;
    });
}

MockController::~MockController() {
    m_bridge.stop();
}

void MockController::run() {
    if (!m_bridge.start(30002)) {
        std::cerr << "Failed to start HMI Bridge!" << std::endl;
        return;
    }

    std::cout << "Mock Controller Running..." << std::endl;

    // Main Loop: 250Hz (4ms)
    const int loopRateHz = 250;
    const double dt = 1.0 / loopRateHz;

    while (true) {
        auto start = std::chrono::steady_clock::now();

        m_bridge.poll();        // Process Network Input
        updatePhysics(dt);      // Simulate Robot
        updateMonitorData(dt);  // Simulate Sensors

        // Broadcast Status at 50Hz (every 5th cycle)
        static int cycle = 0;
        if (++cycle >= 5) {
            cycle = 0;
            m_status.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch()).count();

            m_status.configVersion = m_configVer;
            m_status.trajVersion = m_trajVer;
            m_status.programVersion = m_progVer;

            // Delta-compression logic: Only send heavy data if HMI has old version
            bool sendConfig = (m_lastInput.ackConfigVersion != m_configVer);
            bool sendTraj = (m_lastInput.ackTrajVersion != m_trajVer);
            bool sendProg = (m_lastInput.ackProgramVersion != m_progVer);

            RobotStatus broadcastStatus = m_status;

            if (!sendConfig) broadcastStatus.config.tools.clear();
            else broadcastStatus.config = m_status.config;

            if (!sendTraj) {
                broadcastStatus.trajectory.points.clear();
                broadcastStatus.trajectory.waypoints.clear();
            } else {
                broadcastStatus.trajectory = m_status.trajectory;
            }

            if (!sendProg) broadcastStatus.loadedProgram.steps.clear();
            else broadcastStatus.loadedProgram = m_loadedProgram;

            m_bridge.broadcastStatus(broadcastStatus);
        }

        std::this_thread::sleep_until(start + std::chrono::milliseconds(4));
    }
}

void MockController::handleInput(const ControlState& input) {
    m_lastInput = input;

    // --- Safety Handling ---
    if (input.setEStop) {
        m_status.sys.isEStop = true;
        m_executor.stop();
        m_isMovingHome = false;
        // Safety: Freeze target immediately
        m_targetJoints = m_status.motion.plannedJoints;
        std::cout << "[MOCK] E-STOP TRIGGERED!" << std::endl;
    }
    if (input.resetEStop) {
        m_status.sys.isEStop = false;
        std::cout << "[MOCK] E-STOP RESET." << std::endl;
    }

    // --- Transactions (Config) ---
    if (input.configUpdateReqId > m_lastProcessedConfigReqId) {
        m_status.config = input.newConfig;
        m_configMgr.save(m_status.config);
        m_configVer++;
        m_status.processedConfigReqId = input.configUpdateReqId;
        m_lastProcessedConfigReqId = input.configUpdateReqId;
    }

    // --- Transactions (Program) ---
    if (input.programUpdateReqId > m_lastProcessedProgramReqId) {
        m_loadedProgram = input.newProgram;
        m_executor.load(m_loadedProgram);
        generateTrajectory(m_loadedProgram);
        m_progVer++;
        m_status.processedProgramReqId = input.programUpdateReqId;
        m_lastProcessedProgramReqId = input.programUpdateReqId;
    }

    if (m_status.sys.isEStop) return;

    // --- Jogging Request ---
    // Instead of instant movement, we increment the *target* position.
    // The physics loop will smooth out the movement towards this target.
    if (input.jogRequestId > m_lastProcessedJogId) {
        if (input.jogAxis >= 0 && input.jogAxis < 6) {
            m_targetJoints[input.jogAxis] += input.jogIncrement;
        }
        m_lastProcessedJogId = input.jogRequestId;
        m_status.processedJogId = m_lastProcessedJogId;
    }

    // --- Program Control ---
    if (input.programCommand == 1) { // Start
        m_executor.start();
        m_isMovingHome = false;
        m_targetJoints = m_status.motion.plannedJoints;
    } else if (input.programCommand == 2) { // Pause
        m_executor.pause();
        m_targetJoints = m_status.motion.plannedJoints;
    } else if (input.programCommand == 3) { // Stop
        m_executor.stop();
        m_isMovingHome = false;
        m_targetJoints = m_status.motion.plannedJoints;
    } else if (input.programCommand == 4) { // Move Home
        m_isMovingHome = true;
        m_executor.stop();
        m_targetJoints = home;
    }
}

void MockController::updatePhysics(double dt) {
    bool requestReal = m_lastInput.enableRealMode;
    bool wasSimulated = m_status.motion.isSimulated;

    // Mode Switching Logic
    if (wasSimulated && requestReal) {
        std::cout << "[MOCK] Switching to REAL mode. Syncing Planner to Actual." << std::endl;
        m_status.motion.plannedJoints = m_status.motion.actualJoints;
        m_targetJoints = m_status.motion.plannedJoints;
        m_executor.stop();
        m_isMovingHome = false;
        if (!m_loadedProgram.steps.empty()) {
            generateTrajectory(m_loadedProgram);
        }
    }

    m_status.motion.isSimulated = !requestReal;
    m_status.motion.speedRatio = m_lastInput.speedOverride;

    bool moving = false;

    if (!m_status.sys.isEStop) {
        if (m_isMovingHome) {
            // Homing: Move towards hardcoded 'home' target
            moveTowardsTarget(dt, 0.5); // Fixed 50% speed for homing

            // Check if reached
            bool reached = true;
            for(int i=0; i<6; ++i) if(std::abs(m_status.motion.plannedJoints[i] - home[i]) > 0.1) reached = false;
            if(reached) {
                m_isMovingHome = false;
                std::cout << "[MOCK] Homing Complete." << std::endl;
            }
            moving = true;
        }
        else if (m_executor.isRunning) {
            // Program Execution
            m_executor.update(dt, m_lastInput.speedOverride, m_status.motion);
            // Sync manual target to program to prevent jump on stop
            m_targetJoints = m_status.motion.plannedJoints;
            moving = true;
        }
        else {
            // Manual Jogging (Smooth Interpolation)
            moveTowardsTarget(dt, m_lastInput.speedOverride);

            // Check if effectively moving
            for(int i=0; i<6; ++i) {
                if (std::abs(m_status.motion.plannedJoints[i] - m_targetJoints[i]) > 0.001) moving = true;
            }
        }
    }

    m_status.motion.isMoving = moving;

    // In simulation, Actual = Planned. In Real mode, Actual would come from hardware.
    if (!m_status.motion.isSimulated) {
        m_status.motion.actualJoints = m_status.motion.plannedJoints;
    }

    // --- Kinematics Calculations ---
    Math::Mat4 fP = Math::CalculateFK(m_status.motion.plannedJoints);

    // Apply Tool Offset
    Math::Mat4 actTool = Math::Mat4::Identity();
    if(!m_status.config.tools.empty()) actTool = Math::Mat4::FromPose(m_status.config.tools[0].offset);

    m_status.motion.plannedTcp = (fP * actTool).ToPose();
    m_status.motion.actualTcp = (Math::CalculateFK(m_status.motion.actualJoints) * actTool).ToPose();

    // Calculate Monitor Pose (Relative to Selected Base)
    Math::Mat4 mT = Math::Mat4::Identity();
    Math::Mat4 mB = Math::Mat4::Identity();

    for(const auto& t : m_status.config.tools) {
        if(t.id == m_lastInput.monitorToolId) { mT = Math::Mat4::FromPose(t.offset); break; }
    }
    for(const auto& b : m_status.config.bases) {
        if(b.id == m_lastInput.monitorBaseId) { mB = Math::Mat4::FromPose(b.offset); break; }
    }

    // T_base_tool = Inv(T_world_base) * T_world_flange * T_flange_tool
    m_status.motion.monitorPose = (mB.Inverse() * fP * mT).ToPose();

    // Update Program State info
    m_status.prog.isRunning = m_executor.isRunning;
    m_status.prog.currentLine = m_executor.currentStepIndex;
    if (!m_loadedProgram.name.empty()) {
        strncpy(m_status.prog.programName, m_loadedProgram.name.c_str(), 63);
    }
}

/**
 * @brief Smoothly interpolates the current joints towards the target joints.
 * Limits the speed based on JOG_SPEED_DEG_S and the speed override.
 */
void MockController::moveTowardsTarget(double dt, double speedRatio) {
    double maxStep = JOG_SPEED_DEG_S * speedRatio * dt;
    if (maxStep < 0.001) maxStep = 0.001; // Avoid divide by zero or stuck

    for (int i = 0; i < 6; ++i) {
        double diff = m_targetJoints[i] - m_status.motion.plannedJoints[i];
        if (std::abs(diff) <= maxStep) {
            m_status.motion.plannedJoints[i] = m_targetJoints[i];
        } else {
            if (diff > 0) m_status.motion.plannedJoints[i] += maxStep;
            else m_status.motion.plannedJoints[i] -= maxStep;
        }
    }
}

void MockController::moveHome(double dt) {
    // Deprecated wrapper (logic moved to updatePhysics)
}

void MockController::generateTrajectory(const ProgramData& prog) {
    m_status.trajectory.points.clear();
    m_status.trajectory.waypoints.clear();

    if (prog.steps.empty()) {
        m_trajVer++;
        std::cout << "[MOCK] Trajectory cleared (Empty Program)." << std::endl;
        return;
    }

    // Start trajectory from CURRENT PLANNED POSITION
    std::array<double, 6> cursor = m_status.motion.plannedJoints;
    m_status.trajectory.points.push_back(Math::CalculateFK(cursor).ToXYZ());

    for(size_t i = 0; i < prog.steps.size(); ++i) {
        std::array<double, 6> start = cursor;
        std::array<double, 6> end = prog.steps[i].targetJoints;

        // Add target as a waypoint (green ball)
        m_status.trajectory.waypoints.push_back(Math::CalculateFK(end).ToXYZ());

        // Dense interpolation for the line (10 segments per move)
        int density = 10;
        for(int k=1; k<=density; ++k) {
            double t = (double)k / density;
            std::array<double, 6> interp;
            for(int j=0; j<6; ++j) {
                interp[j] = start[j] + (end[j] - start[j]) * t;
            }
            m_status.trajectory.points.push_back(Math::CalculateFK(interp).ToXYZ());
        }
        cursor = end;
    }

    m_trajVer++;
    std::cout << "[MOCK] Trajectory generated. Points: " << m_status.trajectory.points.size()
              << " Waypoints: " << m_status.trajectory.waypoints.size() << std::endl;
}

void MockController::updateMonitorData(double dt) {
    m_simTime += dt;
    // Simulate sine-wave loads for realism
    m_status.sys.cpuLoad = 20.0 + 10.0 * sin(m_simTime * 0.5);
    m_status.sys.controllerTemp = 45.0 + 5.0 * sin(m_simTime * 0.1);
    m_status.sys.networkLatency = 1.0 + (rand() % 40) / 10.0;
}
