// --- START OF FILE: HexaStudio/jog_control/bench/FakeJogController.h ---
/**
 * @file FakeJogController.h
 * @brief Offline emulation of the RobotService surface that PanelRight (the jog panel) needs.
 *
 * FakeJogController plays the controller's role at the RobotService boundary WITHOUT any network/RDT:
 * it receives jog intent from the panel (jog step, enable, coordinate mode, go-home), "moves" a
 * simulated robot, and reports motion status + a demo tool/base configuration - exactly the feedback
 * PanelRight displays. It is Qt Core only (QObject + QTimer), so the jog module runs and is checked in
 * isolation from HexaStudio and the controller.
 */
#ifndef HEXA_FAKE_JOG_CONTROLLER_H
#define HEXA_FAKE_JOG_CONTROLLER_H

#include <QObject>
#include <QTimer>

#include "BackendTypes.h"   // HmiMotionStatus, HmiSystemConfig, HmiToolData, HmiBaseData

namespace hexa {

class FakeJogController : public QObject {
    Q_OBJECT
public:
    explicit FakeJogController(QObject* parent = nullptr);

    // How much a single jog request moves the target, and how long "moving" is reported afterwards so
    // the panel's jog-button gating (MOVING -> READY) is exercised.
    static constexpr double kJogStepScale = 1.0;
    static constexpr int    kMotionSettleMs = 250;

    HmiSystemConfig demoConfig() const;              // two tools + two bases for the context selectors
    const HmiMotionStatus& status() const { return m_status; }

    // Name -> id mapping for the context signals (the panel emits names; the controller seam takes
    // ids). Mirrors the lookup MainWindow does against RobotService::getConfig(); unknown name -> 0.
    int toolIdByName(const QString& name) const;
    int baseIdByName(const QString& name) const;

public slots:
    void requestJog(int axisIndex, double increment); // <- JogPanel::jogRequested
    void setJogEnabled(bool enabled);                 // <- JogPanel::jogEnableRequested
    void setJogMode(int mode);                        // <- JogPanel::coordSystemChanged
    void goHome();                                    // <- JogPanel::goHomeRequested
    void setJogContext(int toolId, int baseId);       // <- JogPanel::jogContextChanged (name-mapped)
    void setMonitorContext(int toolId, int baseId);   // <- JogPanel::monitorContextChanged (name-mapped)

signals:
    void configReceived(const HmiSystemConfig& config); // -> PanelRight::onConfigReceived
    void statusChanged(const HmiMotionStatus& status);  // -> PanelRight::updateState
    void message(const QString& text);

private:
    void beginMotion();
    void endMotion();
    void pushStatus();
    void refreshMonitorPose();

    HmiMotionStatus m_status;
    QVector<HmiToolData> m_tools;         // demo tools (also served via demoConfig)
    QVector<HmiBaseData> m_bases;         // demo bases (also served via demoConfig)
    QVector<HmiAxisLimit> m_axisLimits;   // demo joint limits; jog clamps here (exercises the at-limit UI)
    int m_monitorToolId = 0;
    int m_monitorBaseId = 0;
    QTimer m_settleTimer;
};

} // namespace hexa

#endif // HEXA_FAKE_JOG_CONTROLLER_H
// --- END OF FILE: HexaStudio/jog_control/bench/FakeJogController.h ---
