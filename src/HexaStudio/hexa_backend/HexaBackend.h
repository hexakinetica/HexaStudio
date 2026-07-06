// --- START OF FILE: HexaStudio/hexa_backend/HexaBackend.h ---
/**
 * @file HexaBackend.h
 * @brief The new-product backend: a module-owned implementation of the BackendClient contract.
 *
 * hexa_backend is the from-scratch Studio backend (boss decision 2026-07-03,
 * REQ_studio_modularization_backend.md §8; detailed requirement REQ_hexa_backend.md). It implements
 * the BackendClient seam DIRECTLY - there is no singleton and no adapter forwarding layer. It
 * reproduces the E2E-validated data path of the shipping RobotService (RDT::ClientState + RdtClient on
 * a worker thread, version-tracked smart updates, the DTO mappers and the shared monitor/jog pose
 * math) and additionally:
 *   - absorbs the three RobotServiceAdapter pieces (halConfigChanged republish, QSettings workstation
 *     endpoint override, tcpSimulatorStateChanged);
 *   - fills the program-mapper fields the shipping stub dropped (tool_id/base_id + Label id linkage);
 *   - syncs the planner trajectory preview (version-tracked) into the trajectoryReceived signal,
 *     consumed by the viewport3d module.
 *
 * Threading model (identical to the proven RobotService): a BackendRdtWorker owns the RdtClient on its
 * own QThread and ticks it at 50 Hz; a Qt timer on the owning thread polls ClientState at 50 Hz and
 * publishes the push-only contract signals. All ClientState access is via its documented thread-safe
 * request/get API, exactly as the shipping service used it.
 */
#ifndef HEXA_BACKEND_H
#define HEXA_BACKEND_H

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

#include "BackendClient.h"   // hexa::BackendClient contract (+ HMI DTOs, ProgramCommand)
#include "RdtProtocol.h"     // RDT::NetProtocol::JogFrame (stored member), wire DTOs used by mappers

namespace RDT {
class ClientState;
class RdtClient;
class AxisSet;
class INetworkClient;
struct CartPose;
} // namespace RDT

namespace hexa {

// Optional transport override for the controller link (same signature as
// RDT::RdtClient::TransportFactory). Empty = platform TCP (networked edition, unchanged);
// the HexaStudioDesktop composition root injects a loopback-client factory here.
using RdtTransportFactory = std::function<std::unique_ptr<RDT::INetworkClient>()>;

/**
 * @class BackendRdtWorker
 * @brief Owns the RdtClient on a dedicated thread and ticks it (connect/update/send) at 50 Hz.
 *
 * Mirrors the shipping RobotService::RdtWorker so the module owns its network pump without reaching
 * into src/. It only touches the shared ClientState through the client; no HMI types cross here.
 */
class BackendRdtWorker : public QObject {
    Q_OBJECT
public:
    explicit BackendRdtWorker(std::shared_ptr<RDT::ClientState> client_state,
                              RdtTransportFactory transport_factory = {},
                              QObject* parent = nullptr);
    // Out-of-line: the unique_ptr<RdtClient> member holds an incomplete type in this header, so the
    // destructor (which Qt's QMetaType machinery instantiates for this QObject) must be emitted in the
    // .cpp where RdtClient is complete.
    ~BackendRdtWorker() override;

public slots:
    void start();
    void tick();
    void connectToServer(const QString& ip, int port, bool auto_reconnect);
    void disconnectFromServer();

signals:
    void connectionChanged(bool connected);

private:
    std::shared_ptr<RDT::ClientState> client_state_;
    RdtTransportFactory transport_factory_;   // empty = platform TCP
    std::unique_ptr<RDT::RdtClient> client_;
    QTimer* timer_ = nullptr;
    bool last_connected_ = false;
};

/**
 * @class HexaBackend
 * @brief The production BackendClient of the new product (owns the controller connection + sync).
 */
class HexaBackend : public BackendClient {
    Q_OBJECT
public:
    /**
     * @param transport_factory Optional controller-link transport override (see
     * RdtTransportFactory). main_ng passes nothing (TCP); the desktop edition injects loopback.
     */
    explicit HexaBackend(QObject* parent = nullptr, RdtTransportFactory transport_factory = {});
    ~HexaBackend() override;

    // --- Composition-root surface (kept 1:1 with RobotServiceAdapter so main_ng swaps drop-in) ---
    // Publish the cached state once after the shell wiring is connected.
    void publishInitialState();
    // The TCP HAL simulator process is owned by main; it reports its state here (push-only signal).
    void notifyTcpSimulatorState(bool running);
    // Controller endpoint lifecycle, driven by the composition root from the workstation settings.
    void connectToController(const QString& ip, int port);
    void disconnectFromController();

    // Read access for the composition root (probe result, endpoint default). Push-only for panels;
    // these are host-process reads, never used by the shell/panels.
    const HmiRobotStatus& getStatus() const { return m_status; }
    const HmiSystemConfig& getConfig() const { return m_config; }

    // --- BackendClient command surface (contract names mirror the shipping service 1:1) ---
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
    void onUpdateTimer();
    void onConnectionChanged(bool connected);

private:
    // --- DTO mappers (reproduced from the shipping service) ---
    void mapRdtConfigToHmi(const RDT::NetProtocol::RobotConfigData& rdt_config, HmiSystemConfig& hmi_config);
    RDT::NetProtocol::RobotConfigData mapHmiConfigToRdt(const HmiSystemConfig& hmi_config);
    void mapRdtHalConfigToHmi(const RDT::NetProtocol::HalConfigState& rdt_hal, HmiHalConfig& hmi_hal);
    RDT::NetProtocol::HalConfigCommand mapHmiHalConfigToRdt(const HmiHalConfig& hmi_hal);
    // Program mapping lives in the stateless ProgramMapper (studio ↔ wire contract), reused by the
    // pipeline integration test. See program_mapper::toRdt / toHmi.

    // --- Helpers ---
    QVector<double> axisSetToVector(const RDT::AxisSet& axisSet);
    QVector<double> cartPoseToVector(const RDT::CartPose& pose);
    static RDT::CartPose qVectorToCartPose(const QVector<double>& v);
    // Express a world flange pose as the TCP in a Tool/Base frame; shared RDT::pose_math is the single
    // source of truth (also used by HexaMotion's FrameTransformer).
    QVector<double> calculateMonitorPose(const RDT::CartPose& flangePoseWorld, int toolId, int baseId);

    // --- Absorbed RobotServiceAdapter piece 2: workstation endpoint override (QSettings) ---
    HmiSystemConfig withWorkstationOverrides(const HmiSystemConfig& config) const;

    // --- State storage ---
    HmiRobotStatus m_status;
    HmiSystemConfig m_config;

    // Version tracking for smart updates.
    uint32_t m_lastKnownConfigVersion = 0;
    uint32_t m_lastKnownProgramVersion = 0;
    uint32_t m_lastKnownTrajVersion = 0;
    uint32_t m_lastKnownFileOpId = 0;
    // True while a program-version bump WE caused (uploadProgram) is still expected to echo back, so
    // the poll syncs the cache silently instead of re-emitting programLoaded (which would reset the
    // editor tools menu mid-teach). See uploadProgram() and the program-sync block in onUpdateTimer().
    bool m_localProgramEchoPending = false;

    // Monitor / jog context.
    int m_monitorToolId = 0;
    int m_monitorBaseId = 0;
    int m_jogToolId = 0;
    int m_jogBaseId = 0;
    RDT::NetProtocol::JogFrame m_jogFrame = RDT::NetProtocol::JogFrame::JOINT;

    // Caches.
    QVector<ProgramCommand> m_cachedProgram;

    // Operator-visible program identity (audit A3): the controller file the current program came
    // from (LOAD) or went to (SAVE). Pendings hold the requested name until the file-op response
    // confirms it; empty until any load/save happened.
    QString m_loadedProgramName;
    QString m_pendingLoadName;
    QString m_pendingSaveName;
    // Armed by save/delete: re-request the file list once the operation's response arrives, so the
    // library view stays current without a manual REFRESH (boss 2026-07-07).
    bool m_refreshListAfterFileOp = false;
    uint32_t m_halRequestId = 0;

    // Shared backend components + Qt integration.
    std::shared_ptr<RDT::ClientState> m_client_state;
    QTimer* m_updateTimer = nullptr;
    QThread* m_rdtThread = nullptr;
    std::atomic<bool> m_isConnected{false};
    BackendRdtWorker* m_rdtWorker = nullptr;
};

} // namespace hexa

#endif // HEXA_BACKEND_H
// --- END OF FILE: HexaStudio/hexa_backend/HexaBackend.h ---
