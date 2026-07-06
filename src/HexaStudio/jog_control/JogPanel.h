// --- START OF FILE: HexaStudio/jog_control/JogPanel.h ---
/**
 * @file JogPanel.h
 * @brief Standalone, RobotService-free jog panel for the new-product jog module.
 *
 * This is the module-owned jog panel (it does NOT modify the shipping PanelRight). It keeps the exact
 * same intent/feedback contract PanelRight exposes - so it drops into the same MainWindow wiring - but
 * evolves the layout independently:
 *   - coordinate frame (JOINT / WORLD / TOOL) is a drop-down (not a row of tabs);
 *   - the arm/status indicator (SIM / ENABLE / READY / MOVING) is folded INTO the JOG button, so a
 *     separate status label row is not needed;
 *   - jog pad is a full-width pendant-style grid "[-] [axis | desired value | unit] [+]": the
 *     DESIRED (commanded) value sits in a full-row-height DRO-style cell between the jog keys
 *     (near-black inset, right-aligned mono digits, muted unit suffix) and must stay readable;
 *     the cell turns amber when the joint axis sits at its configured limit (display-only,
 *     joint-space); the minus/plus keys absorb the spare width and height (no dead bands);
 *   - the passive monitor (ACTUAL values) is a framed Surface card, mono-font, right-aligned columns,
 *     labels always matching the selected monitor base.
 *
 * The safety-relevant behaviour of the original is preserved verbatim: jog is discrete, armed only on
 * REAL hardware, direction buttons are gated ("anti-stick") while the robot is moving with an absolute
 * watchdog release, and Cartesian orientation axes use a separate degree step.
 */
#ifndef HEXA_JOG_PANEL_H
#define HEXA_JOG_PANEL_H

#include <QWidget>
#include <QVector>
#include <QStringList>

#include "HexaWidgets.h"
#include "BackendTypes.h"   // HmiMotionStatus, HmiSystemConfig, HmiToolData, HmiBaseData

class QPushButton;
class QLabel;
class QComboBox;
class QTimer;
class QFrame;

namespace hexa {

class JogPanel : public QWidget {
    Q_OBJECT
public:
    explicit JogPanel(QWidget* parent = nullptr);

    void updateState(const HmiMotionStatus& status);

    // Guide target access (explicit accessors, never findChild): the guided demo highlights these
    // real controls to teach the manual-motion workflow. Ownership stays with the panel.
    QPushButton* enableJogButton() const { return m_btnJogEnable; }
    QPushButton* goHomeButton() const { return m_btnHome; }
    // A representative jog key (axis 1 minus): the demo points at "these move the robot" without
    // implying any single axis. Null only before setupUi (never in a wired panel).
    QPushButton* firstJogKey() const { return m_jogButtons.isEmpty() ? nullptr : m_jogButtons.first(); }

public slots:
    void onConfigReceived(const HmiSystemConfig& config);

signals:
    void jogRequested(int axisIndex, double increment);
    void jogEnableRequested(bool enabled);
    void goHomeRequested();
    void coordSystemChanged(int mode);
    void jogContextChanged(QString tool, QString base);
    void monitorContextChanged(QString tool, QString base);

private slots:
    void onCoordModeChanged(int index);
    void onStepClicked();
    void onJogEnableClicked();
    void onJogBtnPressed();
    void onGoHomeClicked();
    void onJogNoticeTimeout();
    void onToolButtonClicked();
    void onBaseButtonClicked();
    void onMonitorContextChanged();
    void onToggleMonitor(bool visible);

private:
    void setupUi();
    QWidget* createJogSection();
    QWidget* createMonitorSection();
    void updateJogLabels(int mode);
    void updateMonitorLabels(bool isJoint);
    void setJogButtonsEnabled(bool enabled);
    void applyValueBoxStyle(int axisIndex, bool atLimit);
    void updateAxisLimitHighlights(const QVector<double>& displayValues);
    void blockJog();
    void unblockJog();
    void applyStep(const QString& text);
    void updateStepOptions();
    void refreshJogStatus();   // now styles the JOG button itself (folded status)
    void showJogNotice(const QString& text);
    void emitContextChanged();

    // --- Jog state ---
    int    m_currentMode = 0;       // 0 = JOINT, otherwise Cartesian (WORLD/TOOL)
    double m_currentStep = 1.0;     // translation step (mm) or joint step (deg)
    double m_currentOrientStep = 1.0; // orientation step (deg) for Cartesian Rx/Ry/Rz
    bool   m_jogEnabled = false;
    bool   m_isSimulated = false;

    // Jog-button gating (anti-stick): buttons gray out while the robot is moving and return when it
    // stops; the release timer is an absolute safety cap so they can never stick.
    bool    m_jogBlocked = false;
    bool    m_jogSawMotion = false;
    QTimer* m_jogReleaseTimer = nullptr;

    // Step options per mode.
    QStringList m_stepOptions;
    QStringList m_jointStepOptions;
    QStringList m_cartStepOptions;        // translation (mm)
    QStringList m_cartOrientStepOptions;  // orientation (deg), index-aligned with m_cartStepOptions
    int m_stepIndex = 0;

    QComboBox*   m_comboCoord = nullptr;
    QPushButton* m_btnStep = nullptr;
    QVector<QLabel*>      m_axisLabels;
    QVector<QLabel*>      m_activeDisplays;
    QVector<QLabel*>      m_unitLabels;      // unit suffix per DRO cell (degree sign / mm)
    QVector<QFrame*>      m_valueBoxes;      // framed desired-value cell per axis (limit highlight)
    QVector<QPushButton*> m_jogButtons;

    // Joint-axis limits (from HmiSystemConfig::axisLimits) for the at-limit display highlight.
    // Display-only: the controller stays the safety arbiter; unknown limits mean no highlight.
    struct AxisLimitRange {
        bool known = false;
        double minDeg = 0.0;
        double maxDeg = 0.0;
    };
    QVector<AxisLimitRange> m_axisLimits;    // index = axis 0..5
    QVector<bool>           m_axisAtLimit;   // current highlight state (restyle only on change)

    QPushButton* m_btnJogEnable = nullptr;  // toggles arm AND shows SIM/ENABLE/READY/MOVING
    QLabel*      m_noticeLabel = nullptr;   // transient warning (axis limit / unreachable)
    QTimer*      m_noticeTimer = nullptr;
    QString      m_lastNotice;
    QPushButton* m_btnHome = nullptr;

    // Control context (Tool / Base).
    QPushButton* m_btnJogTool = nullptr;
    QPushButton* m_btnJogBase = nullptr;
    QString m_currentJogToolName;
    QString m_currentJogBaseName;
    int m_currentJogToolId = -1;
    int m_currentJogBaseId = -1;
    QVector<HmiToolData> m_availableTools;
    QVector<HmiBaseData> m_availableBases;

    // Passive monitor.
    QWidget*    m_monitorContent = nullptr;
    QVector<QLabel*> m_passiveLabels;
    QVector<QLabel*> m_passiveDisplays;
    QComboBox*  m_comboMonTool = nullptr;
    QComboBox*  m_comboMonBase = nullptr;
};

} // namespace hexa

#endif // HEXA_JOG_PANEL_H
// --- END OF FILE: HexaStudio/jog_control/JogPanel.h ---
