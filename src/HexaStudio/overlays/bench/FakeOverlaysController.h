// --- START OF FILE: HexaStudio/overlays/bench/FakeOverlaysController.h ---
/**
 * @file FakeOverlaysController.h
 * @brief Offline emulation of the RobotService surface the settings overlay needs.
 *
 * Plays the controller's role at the RobotService boundary WITHOUT any network/RDT:
 *   - settings seam: serves a demo HmiSystemConfig, consumes the apply intent and echoes the
 *     applied config back the way the real backend re-publishes settings after an acknowledge;
 *   - diagnostics seam: serves a demo HmiRobotStatus and deterministic fault-scenario injectors
 *     (HAL warning, safety fault, recovery, E-Stop toggle) so the diagnostics panel's annunciator
 *     and event log are exercised offline.
 * (The HAL runtime seam lives in the hal_control module's FakeHalController.) Qt Core only.
 */
#ifndef HEXA_FAKE_OVERLAYS_CONTROLLER_H
#define HEXA_FAKE_OVERLAYS_CONTROLLER_H

#include <QObject>

#include "BackendTypes.h"   // HmiSystemConfig, HmiRobotStatus

namespace hexa {

class FakeOverlaysController : public QObject {
    Q_OBJECT
public:
    explicit FakeOverlaysController(QObject* parent = nullptr);

    HmiSystemConfig demoConfig() const { return m_config; }
    const HmiSystemConfig& lastApplied() const { return m_lastApplied; }
    bool hasApplied() const { return m_hasApplied; }
    const HmiRobotStatus& robotStatus() const { return m_robotStatus; }

public slots:
    // Settings seam
    void applyConfig(const HmiSystemConfig& config);   // <- SettingsPanel::applyRequested
    // Diagnostics seam
    void clearError();                                 // <- DiagnosticsPanel::clearErrorRequested
    void toggleEStop();                                // <- DiagnosticsPanel::eStopRequested
                                                       //    (mirrors the MainWindow toggle lambda)
    // Deterministic scenario injectors (bench/selftest drive these; no randomness).
    void injectHalWarning();                           // HAL: Ok -> Warning: SyncLost
    void injectSafetyFault();                          // SAFETY: Error(AxisLimit), latched, axis A3
    void recoverAll();                                 // everything back to Ok / no fault

signals:
    void configReceived(const HmiSystemConfig& config);   // -> SettingsPanel::setConfig
    void robotStateChanged(const HmiRobotStatus& status); // -> DiagnosticsPanel::updateStatus
    void message(const QString& text);

private:
    void pushStatus();

    HmiSystemConfig m_config;
    HmiSystemConfig m_lastApplied;
    bool m_hasApplied = false;
    HmiRobotStatus m_robotStatus;
};

} // namespace hexa

#endif // HEXA_FAKE_OVERLAYS_CONTROLLER_H
// --- END OF FILE: HexaStudio/overlays/bench/FakeOverlaysController.h ---
