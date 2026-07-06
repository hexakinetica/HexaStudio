// --- START OF FILE: HexaStudio/app_shell/BackendClient.h ---
/**
 * @file BackendClient.h
 * @brief THE backend contract of the new product: the single abstract seam between the assembled
 *        HMI (ShellWindow + feature modules) and the robot controller side.
 *
 * Architecture (integration foundation, boss directive 2026-07-03):
 *
 *     ShellWindow (composition root + mediator)
 *         │  panel signals in / panel slots out (per-module contracts, see MODULARIZATION § 8)
 *         ▼
 *     BackendClient (THIS contract)  ◄── the ONE sanctioned abstraction of the project
 *         ▲                              (Simplicity Mandate rule 4: justified by TWO real
 *         │                               implementations and a hard domain boundary)
 *         ├── FakeBackend (bench)         - coherent offline controller simulation
 *         └── RobotServiceAdapter (Phase C) - thin forwarder onto the shipping RobotService
 *
 * Contract rules:
 *   - the COMMAND surface (slots) mirrors the shipping RobotService method names 1:1, so the
 *     Phase C adapter is mechanical forwarding, not a translation layer;
 *   - the FEEDBACK surface (signals) is push-only: implementations publish state, the shell and
 *     the panels never pull (no getStatus()/getConfig() reads - polling hid staleness bugs in the
 *     shipping overlays);
 *   - ids, not names: user-facing names are mapped to ids by the SHELL (it owns the last received
 *     config), exactly like the shipping MainWindow lambdas;
 *   - everything is typed DTOs from BackendTypes.h - no raw wire structures cross this seam.
 */
#ifndef HEXA_BACKEND_CLIENT_H
#define HEXA_BACKEND_CLIENT_H

#include <QObject>
#include <QStringList>
#include <QVector>

#include "BackendTypes.h"   // HmiRobotStatus, HmiSystemConfig, HmiHalConfig
#include "ProgramData.h"            // ProgramCommand (shared program authoring DTO)

namespace hexa {

class BackendClient : public QObject {
    Q_OBJECT
public:
    explicit BackendClient(QObject* parent = nullptr) : QObject(parent) {}
    ~BackendClient() override = default;

public slots:
    // --- Top bar / global ---
    virtual void setMode(bool isRealRobot) = 0;
    virtual void setSpeedOverride(int percent) = 0;
    virtual void setEStop(bool active) = 0;
    virtual void clearError() = 0;

    // --- Jog (pendant jog panel) ---
    virtual void jogJointIncremental(int axisIndex, double increment) = 0;
    virtual void setJogEnabled(bool enabled) = 0;
    virtual void setJogMode(int mode) = 0;
    virtual void setJogContext(int toolId, int baseId) = 0;
    virtual void setMonitorContext(int toolId, int baseId) = 0;

    // --- Program execution + controller storage ---
    virtual void startProgram(const QVector<ProgramCommand>& program) = 0;
    virtual void pauseProgram() = 0;
    virtual void resumeProgram() = 0;
    virtual void stopProgram() = 0;
    virtual void uploadProgram(const QVector<ProgramCommand>& program) = 0;
    virtual void requestRemoteFileList() = 0;
    virtual void loadRemoteProgramFile(const QString& filename) = 0;
    virtual void saveRemoteProgramFile(const QString& filename,
                                       const QVector<ProgramCommand>& program) = 0;
    virtual void deleteRemoteProgramFile(const QString& filename) = 0;

    // --- System configuration ---
    virtual void applySettings(const HmiSystemConfig& newConfig) = 0;

    // --- HAL runtime (Motor Configurator) ---
    virtual void connectHalEndpoint(const QString& host, int port) = 0;
    virtual void disconnectHalEndpoint() = 0;
    virtual void jogJointIncrementalHal(int axisIndex, double stepDeg) = 0;
    virtual void startHomingSequence(int axisIndex) = 0;   // -1 = full sequential homing 1..6
    virtual void masterAxis(int axisIndex) = 0;            // set-zero of one axis
    virtual void setZeroAll() = 0;                         // atomic set-zero of all axes
    virtual void applyHalConfig(const HmiHalConfig& halConfig) = 0;

signals:
    // --- Feedback (push-only; implementations publish, nobody pulls) ---
    void robotStateChanged(const HmiRobotStatus& status);
    void configReceived(const HmiSystemConfig& config);
    // Planner trajectory preview: dense sampled TCP path + programmed waypoints, world frame, mm.
    // Consumed by the viewport3d module (added at its integration - the first real consumer).
    void trajectoryReceived(const HmiTrajectoryData& data);
    void halConfigChanged(const HmiHalConfig& halConfig);
    void programLoaded(const QVector<ProgramCommand>& program);
    // Controller confirmed a program SAVE under this file name. The pendant has no local program
    // store (boss directive 2026-07-06), so this is the only event that may clear the editor's
    // UNSAVED marker — an optimistic clear on request would hide a failed save.
    void programSaved(const QString& filename);
    void remoteFileListReceived(const QStringList& files);
    void tcpSimulatorStateChanged(bool running);
    void messageOccurred(const QString& text, bool isError);
};

} // namespace hexa

#endif // HEXA_BACKEND_CLIENT_H
// --- END OF FILE: HexaStudio/app_shell/BackendClient.h ---
