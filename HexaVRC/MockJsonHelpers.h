#ifndef MOCK_JSON_HELPERS_H
#define MOCK_JSON_HELPERS_H

#include "../Shared/RdtProtocol.h"
#include "../Shared/nlohmann/json.hpp"
#include <cstring>

using json = nlohmann::json;

namespace Rdt {

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ToolData, id, name, offset)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ControllerConfig, tools, bases, ipAddress, axisLimits)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TrajectoryPoint, x, y, z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TrajectoryPath, points, waypoints)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProgramStep, id, type, targetJoints, speed, commandCode)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProgramData, name, steps)

inline void from_json(const json& j, ControlState& p) {
    if(j.contains("jogRequestId")) j.at("jogRequestId").get_to(p.jogRequestId);
    if(j.contains("jogAxis")) j.at("jogAxis").get_to(p.jogAxis);
    if(j.contains("jogIncrement")) j.at("jogIncrement").get_to(p.jogIncrement);

    if(j.contains("speedOverride")) j.at("speedOverride").get_to(p.speedOverride);
    if(j.contains("enableRealMode")) j.at("enableRealMode").get_to(p.enableRealMode);
    if(j.contains("monitorToolId")) j.at("monitorToolId").get_to(p.monitorToolId);
    if(j.contains("monitorBaseId")) j.at("monitorBaseId").get_to(p.monitorBaseId);

    // NEW: E-Stop flags
    if(j.contains("setEStop")) j.at("setEStop").get_to(p.setEStop);
    if(j.contains("resetEStop")) j.at("resetEStop").get_to(p.resetEStop);

    if(j.contains("programCommand")) j.at("programCommand").get_to(p.programCommand);
    if(j.contains("previewStepIndex")) j.at("previewStepIndex").get_to(p.previewStepIndex);

    if(j.contains("ackConfigVersion")) j.at("ackConfigVersion").get_to(p.ackConfigVersion);
    if(j.contains("ackTrajVersion")) j.at("ackTrajVersion").get_to(p.ackTrajVersion);
    if(j.contains("ackProgramVersion")) j.at("ackProgramVersion").get_to(p.ackProgramVersion);

    if(j.contains("configUpdateReqId")) j.at("configUpdateReqId").get_to(p.configUpdateReqId);
    if(j.contains("newConfig")) j.at("newConfig").get_to(p.newConfig);

    if(j.contains("programUpdateReqId")) j.at("programUpdateReqId").get_to(p.programUpdateReqId);
    if(j.contains("newProgram")) j.at("newProgram").get_to(p.newProgram);
}

inline void to_json(json& j, const MotionState& p) {
    j = json{
        {"actualJoints", p.actualJoints}, {"plannedJoints", p.plannedJoints},
        {"actualTcp", p.actualTcp}, {"plannedTcp", p.plannedTcp},
        {"monitorPose", p.monitorPose},
        {"isMoving", p.isMoving}, {"isSimulated", p.isSimulated}, {"speedRatio", p.speedRatio}
    };
}

inline void to_json(json& j, const SystemState& p) {
    j = json{
        {"isEStop", p.isEStop},
        {"isPowerOn", p.isPowerOn},
        {"activeErrorId", p.activeErrorId},
        {"cpuLoad", p.cpuLoad},
        {"controllerTemp", p.controllerTemp},
        {"networkLatency", p.networkLatency}
    };
}

inline void to_json(json& j, const ProgramState& p) {
    j = json{
        {"isRunning", p.isRunning}, {"isPaused", p.isPaused},
        {"currentLine", p.currentLine}, {"programName", std::string(p.programName)}
    };
}

inline void to_json(json& j, const RobotStatus& p) {
    j = json{
        {"timestamp", p.timestamp},
        {"motion", p.motion}, {"sys", p.sys}, {"prog", p.prog},
        {"processedJogId", p.processedJogId},
        {"processedConfigReqId", p.processedConfigReqId},
        {"processedProgramReqId", p.processedProgramReqId},
        {"configVersion", p.configVersion},
        {"trajVersion", p.trajVersion},
        {"programVersion", p.programVersion}
    };
    if (!p.config.tools.empty()) j["config"] = p.config;
    if (!p.trajectory.points.empty()) j["trajectory"] = p.trajectory;
    if (!p.loadedProgram.steps.empty()) j["loadedProgram"] = p.loadedProgram;
}

} // namespace Rdt

#endif // MOCK_JSON_HELPERS_H
