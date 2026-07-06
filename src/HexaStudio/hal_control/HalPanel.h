// --- START OF FILE: HexaStudio/hal_control/HalPanel.h ---
/**
 * @file HalPanel.h
 * @brief Standalone, RobotService-free HAL runtime overlay - the hal_control module (split out of
 *        the overlays module by boss decision 2026-07-03: HAL is a direct-hardware commissioning
 *        panel with the largest intent contract of the project, its own bench-run value for MKS
 *        bring-up, and a real hardware domain boundary).
 *
 * Module-owned re-implementation of the shipping HalOverlay (which stays untouched). It keeps the
 * shipping overlay's feedback surface (setHalConfigCurrent / setTcpSimulatorRunning + robot status)
 * and its existing intent signals, and REPLACES every direct RobotService::instance() call with a
 * typed intent signal, so the panel drops into MainWindow wiring at integration:
 *   RobotService call (shipping)            -> intent signal (module)
 *   connectHalEndpoint(host, port)          -> halConnectRequested(host, port)
 *   disconnectHalEndpoint()                 -> halDisconnectRequested()
 *   jogJointIncrementalHal(axis, step)      -> halJogRequested(axis, stepDeg)
 *   startHomingSequence(axis | -1)          -> homingRequested(axisIndex)
 *   masterAxis(axis)                        -> setZeroRequested(axisIndex)
 *   setZeroAll()                            -> setZeroAllRequested()
 *   clearError()                            -> clearErrorsRequested()
 *   setJogEnabled(armed)                    -> jogArmRequested(armed)
 *   setEStop(true)                          -> eStopRequested()
 *   stateChanged pull / getConfig() pull    -> onRobotStateChanged / setHalConfigCurrent (pushed)
 *
 * Presentation: restyled from the 1:1 tcp_client_app copy (foreign #1e1e1e/#58a6ff palette) to the
 * Hexa pendant theme (overlay module cards, theme borders, mono values, badge palette for states).
 *
 * Safety behaviour preserved verbatim:
 *   - overlay-local jog arm (independent of the jog panel), DISARMED automatically on hide;
 *   - jog is blocked with an explicit reason while: bridge disconnected, disarmed, axis homing,
 *     axis faulted;
 *   - E-STOP is gated only on the bridge connection, never on the HAL link state;
 *   - per-axis telemetry stale detection (title marked after 1.5 s without updates);
 *   - SET ZERO ALL is one atomic command (a per-axis loop would collapse in the one-shot command
 *     field before the next network snapshot);
 *   - explicit warning badge while the bridge is up and the app shows Simulation ("HAL drives REAL
 *     hardware" - decision P2);
 *   - the bring-up command gate (bridge connected only) is kept 1:1, including the documented
 *     production policy to restore after MKS validation.
 */
#ifndef HEXA_HAL_PANEL_H
#define HEXA_HAL_PANEL_H

#include <QWidget>
#include <QHash>
#include <QTimer>

#include "BackendTypes.h"   // HmiHalConfig, HmiRobotStatus

class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QHideEvent;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

namespace hexa {

class HalPanel : public QWidget {
    Q_OBJECT
public:
    explicit HalPanel(QWidget* parent = nullptr);

public slots:
    void setHalConfigCurrent(const HmiHalConfig& config);
    void setTcpSimulatorRunning(bool running);
    void onRobotStateChanged(const HmiRobotStatus& status);
    // Sync the backend selector from the controller config echo ("sim"/"udp"/"mks_tcp").
    // Signal-blocked: an echo must never re-emit realtimeBackendSelected.
    void setRealtimeBackend(const QString& backend);

signals:
    void closeRequested();
    // Operator picked a realtime backend in the BACKEND dropdown ("sim"/"udp"/"mks_tcp").
    // The shell forwards it into the persisted controller config (applySettings path).
    void realtimeBackendSelected(const QString& backend);
    void applyHalRequested(const HmiHalConfig& halConfig);
    void tcpHalSimulatorLaunchRequested();
    void tcpHalSimulatorStopRequested();
    void halConnectRequested(const QString& host, int port);
    void halDisconnectRequested();
    void halJogRequested(int axisIndex, double stepDeg);
    void homingRequested(int axisIndex);      // -1 = full sequential homing 1..6
    void setZeroRequested(int axisIndex);
    void setZeroAllRequested();
    void clearErrorsRequested();
    void jogArmRequested(bool armed);
    void eStopRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;   // blocks click-through
    void paintEvent(QPaintEvent* event) override;        // dims the application behind
    void hideEvent(QHideEvent* event) override;          // safety: disarm jog on leave

private slots:
    void refreshStaleRows();

private:
    struct AxisRow {
        QWidget* container = nullptr;
        QLabel* title = nullptr;
        QLabel* state = nullptr;
        QLabel* position = nullptr;
        QLabel* protection = nullptr;
        QPushButton* jogMinusButton = nullptr;
        QPushButton* jogPlusButton = nullptr;
        QPushButton* enableButton = nullptr;
        QPushButton* disableButton = nullptr;
        QPushButton* homeButton = nullptr;
        QPushButton* setZeroButton = nullptr;
        qint64 lastSeenMs = 0;
    };

    void setupUi();
    QFrame* setupStatusBlock();
    QFrame* setupBackendBlock();
    QFrame* setupConnectionBlock();
    QFrame* setupJogStepBlock();
    // Shows/hides the left-column cards for the selected backend: sim -> SIM note (no transport),
    // mks_tcp -> the existing CONNECTION card, udp -> deferred-configuration note.
    void updateBackendView();
    QFrame* setupGlobalActionsBlock();
    QFrame* setupAxisListBlock();
    void createAxisRow(int axisIndex);
    void updateAxisRow(int axisIndex);
    void setCommandControlsEnabled(bool enabled);
    void setAxisCommandControlsEnabled(AxisRow& row, bool enabled);
    void sendJogStep(int axisIndex, double signedStepDeg);
    void updateJogButtonLabels();
    [[nodiscard]] double selectedJogStepDeg() const;
    void setHalStatusIndicator(const QString& halStatus);
    void setOwnerIndicator(int ownerId);
    void setJogArmed(bool armed);
    void updateConnectionIndicator(bool connected);
    void setStatusMessage(const QString& text);

    static QString stateText(int state);
    static QString stateShortText(int state);
    static QString positionText(double deg);
    static QString protectionText(int protectionCode);

    // Backend selector (left column, above the connection card)
    QComboBox* m_backendCombo = nullptr;
    QFrame* m_connectionCard = nullptr;
    QLabel* m_backendNoteLabel = nullptr;
    QString m_backend = QStringLiteral("sim");

    // Connection block
    QLineEdit* m_hostEdit = nullptr;
    QSpinBox* m_portSpin = nullptr;
    QPushButton* m_connectButton = nullptr;
    QPushButton* m_disconnectButton = nullptr;
    QPushButton* m_startTcpSimButton = nullptr;
    QPushButton* m_stopTcpSimButton = nullptr;
    QLabel* m_connectionStateLabel = nullptr;

    // Status block
    QLabel* m_halStatusLabel = nullptr;
    QLabel* m_simWarningLabel = nullptr;
    QLabel* m_ownerLabel = nullptr;
    QLabel* m_homingStatusLabel = nullptr;
    QLabel* m_backendErrorLabel = nullptr;
    QLabel* m_statusBarLabel = nullptr;

    // Global actions
    QPushButton* m_enableAllButton = nullptr;
    QPushButton* m_disableAllButton = nullptr;
    QPushButton* m_homeAllButton = nullptr;
    QPushButton* m_setZeroAllButton = nullptr;
    QPushButton* m_clearErrorsButton = nullptr;
    QPushButton* m_jogEnableButton = nullptr;
    QPushButton* m_eStopAllButton = nullptr;

    // Jog step block
    QPushButton* m_step1Button = nullptr;
    QPushButton* m_step10Button = nullptr;
    QPushButton* m_stepCustomButton = nullptr;
    QDoubleSpinBox* m_customStepSpin = nullptr;

    QVBoxLayout* m_axisListLayout = nullptr;

    HmiHalConfig m_currentHalConfig;
    HmiRobotStatus m_lastStatus;
    QHash<int, AxisRow> m_rows;   // key: 0-based axis index
    QTimer m_staleTimer;

    // HAL-panel-local jog arm - independent of the jog panel's shared jogEnabled. Works in SIM.
    bool m_jogArmed = false;
};

} // namespace hexa

#endif // HEXA_HAL_PANEL_H
// --- END OF FILE: HexaStudio/hal_control/HalPanel.h ---
