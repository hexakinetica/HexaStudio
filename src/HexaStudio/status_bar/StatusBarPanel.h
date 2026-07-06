// --- START OF FILE: HexaStudio/status_bar/StatusBarPanel.h ---
/**
 * @file StatusBarPanel.h
 * @brief Standalone, RobotService-free top status bar for the new-product status_bar module.
 *
 * Module-owned re-implementation of the shipping PanelTop (which stays untouched). It keeps
 * PanelTop's exact intent/feedback contract, so it drops into the same MainWindow wiring:
 *   - intent out: modeChanged / eStopRequested / settingsRequested / speedChanged /
 *     diagnosticsRequested;
 *   - feedback in: updateState(HmiTopStatus, isMoving).
 *
 * Differences vs the shipping panel:
 *   - NO RobotService singleton: the About box gets the controller IP via onConfigReceived and the
 *     application version via setAppVersion (host-owned), instead of RobotService::instance();
 *   - honest stats: VOLTAGE and CYCLE TIME show "---" because HmiTopStatus does not carry these
 *     values - the module does not fabricate data (the shipping panel hardcodes placeholders).
 *
 * Safety behaviour preserved verbatim: REAL-mode switch confirmation, mode switch rejected while the
 * robot is moving, speed override >50% confirmation with revert, E-STOP/RESET danger styling,
 * traffic-light system status with unchanged precedence (E-STOP > OFFLINE > MOVING > ERROR > READY;
 * OFFLINE is the v0.4.0 display word for the connection-lost state, formerly "DISCONNECTED").
 */
#ifndef HEXA_STATUS_BAR_PANEL_H
#define HEXA_STATUS_BAR_PANEL_H

#include <QWidget>

#include "HexaWidgets.h"     // HexaToggle
#include "BackendTypes.h"   // HmiTopStatus, HmiSystemConfig

class QLabel;
class QPushButton;
class QComboBox;
class QStackedWidget;

namespace hexa {

class StatusBarPanel : public QWidget {
    Q_OBJECT
public:
    explicit StatusBarPanel(QWidget* parent = nullptr);

    /// @brief Reflect controller status. @p isProgramPaused surfaces a held program as PAUSED
    /// (defaults to false so program-less hosts, e.g. the standalone bench, stay unchanged).
    void updateState(const HmiTopStatus& status, bool isMoving, bool isProgramPaused = false);

    // Host-owned About-box data (the shipping panel reached into the RobotService singleton for it).
    void setAppVersion(const QString& version);

    // Guide target access (explicit accessor, never findChild): the E-STOP button for
    // highlight-only demo steps. The shell also reads its geometry from here so the guide
    // callout can never be placed over it (safety constraint).
    QPushButton* eStopButton() const { return m_btnEStop; }

    // Shell-only layout alignment (mediator concern): pin the bar's left group and the E-STOP under
    // the shell's side columns, so the bar's section dividers line up with the splitter boundaries of
    // the panels below. The standalone bench does NOT call this and keeps its honest content sizing
    // (STB-REQ-0010); the widths passed here are the shell's column constants, not baked into the
    // module.
    void setColumnGuides(int leftColumnPx, int rightColumnPx);

public slots:
    void onConfigReceived(const HmiSystemConfig& config);

signals:
    void modeChanged(bool isRealRobot);
    void eStopRequested();
    void settingsRequested();
    void speedChanged(int percent);
    void diagnosticsRequested();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onModeToggled(bool checked);
    void onEStopClicked();
    void onToggleStats();
    void onSpeedChanged(const QString& text);

private:
    void setupUi();
    QWidget* createLeftSection();
    QWidget* createCenterSection();
    QWidget* createCenterControls();
    QWidget* createCenterStats();
    QWidget* createRightSection();
    void showAboutBox();
    void syncStackPagePolicies();

    // --- UI elements ---
    QWidget* m_leftSection = nullptr;    // brand + status group (pinned to the left column by the shell)
    QWidget* m_rightSection = nullptr;   // E-STOP group (pinned to the right column by the shell)
    QLabel* m_lblBrand = nullptr;
    QLabel* m_lblStatus = nullptr;
    QStackedWidget* m_centerStack = nullptr;
    HexaToggle* m_switchMode = nullptr;
    QComboBox* m_comboSpeed = nullptr;
    QPushButton* m_btnSettings = nullptr;
    QPushButton* m_btnStats = nullptr;
    QPushButton* m_btnCloseStats = nullptr;
    QPushButton* m_btnEStop = nullptr;

    // System-stats labels. Only stats HmiTopStatus actually reports have cells; VOLTAGE/CYCLE TIME
    // return once the DTO carries them (cross-module item) - no dead display slots.
    QLabel* m_lblCpu = nullptr;
    QLabel* m_lblTemp = nullptr;
    QLabel* m_lblPing = nullptr;

    // --- State ---
    bool m_isRobotMoving = false;
    int m_lastAcceptedSpeedPercent = 50;
    QString m_controllerIp;   // from onConfigReceived; empty until the config arrives
    QString m_appVersion;     // from setAppVersion; empty until the host provides it
};

} // namespace hexa

#endif // HEXA_STATUS_BAR_PANEL_H
// --- END OF FILE: HexaStudio/status_bar/StatusBarPanel.h ---
