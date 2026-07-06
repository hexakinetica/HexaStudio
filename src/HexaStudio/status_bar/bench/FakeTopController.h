// --- START OF FILE: HexaStudio/status_bar/bench/FakeTopController.h ---
/**
 * @file FakeTopController.h
 * @brief Offline emulation of the RobotService surface that StatusBarPanel needs.
 *
 * Plays the controller's role at the RobotService boundary WITHOUT any network/RDT: consumes the
 * panel's intents (mode, speed override, E-Stop toggle), reports HmiTopStatus + isMoving, and serves
 * a demo config (controller IP for the About box). A deterministic jitter timer animates CPU / TEMP /
 * NETWORK so the monitor view looks alive on the bench. Qt Core only (QObject + QTimer).
 */
#ifndef HEXA_FAKE_TOP_CONTROLLER_H
#define HEXA_FAKE_TOP_CONTROLLER_H

#include <QObject>
#include <QTimer>

#include "BackendTypes.h"   // HmiTopStatus, HmiSystemConfig

namespace hexa {

class FakeTopController : public QObject {
    Q_OBJECT
public:
    explicit FakeTopController(QObject* parent = nullptr);

    static constexpr int kJitterIntervalMs = 500;

    HmiSystemConfig demoConfig() const;
    const HmiTopStatus& status() const { return m_status; }
    bool isMoving() const { return m_isMoving; }
    int speedPercent() const { return m_speedPercent; }

public slots:
    void setMode(bool isRealRobot);       // <- StatusBarPanel::modeChanged
    void setSpeedOverride(int percent);   // <- StatusBarPanel::speedChanged
    void setEStop(bool active);           // <- bench wiring (mirrors the MainWindow toggle lambda)
    void setMoving(bool moving);          // bench/test control of the MOVING state
    void setConnected(bool connected);    // bench/test control of the DISCONNECTED state

signals:
    void statusChanged(const HmiTopStatus& status, bool isMoving); // -> StatusBarPanel::updateState
    void configReceived(const HmiSystemConfig& config);            // -> StatusBarPanel::onConfigReceived
    void message(const QString& text);

private slots:
    void onJitterTick();

private:
    void pushStatus();

    HmiTopStatus m_status;
    bool m_isMoving = false;
    int m_speedPercent = 50;
    quint32 m_tick = 0;
    QTimer m_jitterTimer;
};

} // namespace hexa

#endif // HEXA_FAKE_TOP_CONTROLLER_H
// --- END OF FILE: HexaStudio/status_bar/bench/FakeTopController.h ---
