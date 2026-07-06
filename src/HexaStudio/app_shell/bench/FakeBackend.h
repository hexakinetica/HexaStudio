// --- START OF FILE: HexaStudio/app_shell/bench/FakeBackend.h ---
/**
 * @file FakeBackend.h
 * @brief The bench implementation of the BackendClient contract: ONE coherent offline controller
 *        simulation for the assembled application.
 *
 * Deliberately a fresh, single state machine rather than a composition of the per-module fakes:
 * the integration fake must present ONE consistent HmiRobotStatus / HmiSystemConfig world, which
 * gluing four partial fakes together cannot guarantee. The per-module fakes remain the right tool
 * for the per-module benches. Qt Core only, deterministic (sine-based jitter, fixed timings).
 *
 * HONESTY BOUNDARY (boss decision 2026-07-05, "удалим заглушки чтобы не путаться"): this fake tests
 * WIRING AND GATES ONLY. It does NOT simulate robot motion or trajectory previews - a jog intent is
 * echoed into the commanded target (displayJoints) so the seam is verifiable, but planned/actual
 * joints do not move and no trajectory is ever published. Motion, ghost feedback and planner
 * previews are reviewed exclusively on the live stack (HexaCore + HexaStudioNG).
 */
#ifndef HEXA_FAKE_BACKEND_H
#define HEXA_FAKE_BACKEND_H

#include <QHash>
#include <QTimer>

#include "BackendClient.h"

namespace hexa {

class FakeBackend : public BackendClient {
    Q_OBJECT
public:
    explicit FakeBackend(QObject* parent = nullptr);

    // Publish the full initial state (config, HAL config, status, sim-process state). Call once
    // after the shell wiring is connected - mirrors the first sync of the real backend.
    void publishAll();

    // The TCP HAL simulator is a host-process concern outside the BackendClient contract; the
    // bench consumes the HAL panel's launch/stop intents with this.
    void setTcpSimulatorRunning(bool running);

    const HmiRobotStatus& status() const { return m_status; }
    const HmiSystemConfig& config() const { return m_config; }

    // BackendClient command surface --------------------------------------------------------
    void setMode(bool isRealRobot) override;
    void setSpeedOverride(int percent) override;
    void setEStop(bool active) override;
    void clearError() override;

    void jogJointIncremental(int axisIndex, double increment) override;
    void setJogEnabled(bool enabled) override;
    void setJogMode(int mode) override;
    void setJogContext(int toolId, int baseId) override;
    void setMonitorContext(int toolId, int baseId) override;

    void startProgram(const QVector<ProgramCommand>& program) override;
    void pauseProgram() override;
    void resumeProgram() override;
    void stopProgram() override;
    void uploadProgram(const QVector<ProgramCommand>& program) override;
    void requestRemoteFileList() override;
    void loadRemoteProgramFile(const QString& filename) override;
    void saveRemoteProgramFile(const QString& filename,
                               const QVector<ProgramCommand>& program) override;
    void deleteRemoteProgramFile(const QString& filename) override;

    void applySettings(const HmiSystemConfig& newConfig) override;

    void connectHalEndpoint(const QString& host, int port) override;
    void disconnectHalEndpoint() override;
    void jogJointIncrementalHal(int axisIndex, double stepDeg) override;
    void startHomingSequence(int axisIndex) override;
    void masterAxis(int axisIndex) override;
    void setZeroAll() override;
    void applyHalConfig(const HmiHalConfig& halConfig) override;

private slots:
    void onTick();

private:
    void pushStatus();
    void pushHalConfig();
    // Records a jog intent into the commanded target (displayJoints) so the UI seam is verifiable.
    // Deliberately NO motion: planned/actual joints stay put (see the honesty boundary above).
    void echoJogTarget(int axisIndex, double deltaDeg);

    HmiRobotStatus m_status;
    HmiSystemConfig m_config;                              // halConfigCurrent lives inside
    QHash<QString, QVector<ProgramCommand>> m_remoteFiles; // controller storage emulation
    QVector<ProgramCommand> m_activeProgram;

    QTimer m_tick;
    quint32 m_tickCount = 0;
    int m_programTicksPerRow = 0;   // countdown to the next executed program row (flow echo only)
};

} // namespace hexa

#endif // HEXA_FAKE_BACKEND_H
// --- END OF FILE: HexaStudio/app_shell/bench/FakeBackend.h ---
