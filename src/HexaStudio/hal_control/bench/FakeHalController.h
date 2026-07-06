// --- START OF FILE: HexaStudio/hal_control/bench/FakeHalController.h ---
/**
 * @file FakeHalController.h
 * @brief Offline emulation of the RobotService surface the HAL panel needs.
 *
 * Plays the controller's role at the RobotService boundary WITHOUT any network/RDT: serves a demo
 * HmiHalConfig (axis states covering the whole badge variety) plus a demo HmiRobotStatus, and
 * consumes every HalPanel intent (connect/disconnect, jog, homing, set-zero, clear-errors, jog-arm,
 * E-Stop) with simple deterministic state changes pushed back like real backend telemetry.
 * Qt Core only.
 */
#ifndef HEXA_FAKE_HAL_CONTROLLER_H
#define HEXA_FAKE_HAL_CONTROLLER_H

#include <QObject>

#include "BackendTypes.h"   // HmiHalConfig, HmiRobotStatus

namespace hexa {

class FakeHalController : public QObject {
    Q_OBJECT
public:
    explicit FakeHalController(QObject* parent = nullptr);

    const HmiHalConfig& halConfig() const { return m_halConfig; }
    const HmiRobotStatus& robotStatus() const { return m_robotStatus; }
    int halJogCount() const { return m_halJogCount; }
    int lastHalJogAxis() const { return m_lastHalJogAxis; }
    double lastHalJogStep() const { return m_lastHalJogStep; }
    bool jogArmed() const { return m_jogArmed; }

public slots:
    void applyHalConfig(const HmiHalConfig& config);    // <- HalPanel::applyHalRequested
    void connectHal(const QString& host, int port);     // <- HalPanel::halConnectRequested
    void disconnectHal();                               // <- HalPanel::halDisconnectRequested
    void halJog(int axisIndex, double stepDeg);         // <- HalPanel::halJogRequested
    void startHoming(int axisIndex);                    // <- HalPanel::homingRequested (-1 = all)
    void setZeroAxis(int axisIndex);                    // <- HalPanel::setZeroRequested
    void setZeroAll();                                  // <- HalPanel::setZeroAllRequested
    void clearErrors();                                 // <- HalPanel::clearErrorsRequested
    void setJogArmed(bool armed);                       // <- HalPanel::jogArmRequested
    void requestEStop();                                // <- HalPanel::eStopRequested

signals:
    void halConfigChanged(const HmiHalConfig& config);    // -> HalPanel::setHalConfigCurrent
    void robotStateChanged(const HmiRobotStatus& status); // -> HalPanel::onRobotStateChanged
    void message(const QString& text);

private:
    void pushHal();

    HmiHalConfig m_halConfig;
    HmiRobotStatus m_robotStatus;
    int m_halJogCount = 0;
    int m_lastHalJogAxis = -1;
    double m_lastHalJogStep = 0.0;
    bool m_jogArmed = false;
};

} // namespace hexa

#endif // HEXA_FAKE_HAL_CONTROLLER_H
// --- END OF FILE: HexaStudio/hal_control/bench/FakeHalController.h ---
