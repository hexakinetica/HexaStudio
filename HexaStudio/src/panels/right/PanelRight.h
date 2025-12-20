/**
 * @file PanelRight.h
 * @brief Header for the Manual Control (Jogging) Panel.
 * @author HexaKinetica Team
 * @version 1.0
 *
 * This file defines the right-side panel responsible for manual robot manipulation.
 * It allows the user to move the robot axes individually (Jogging) in different
 * coordinate systems (Joint, World, Tool) and monitor the current pose.
 */

#ifndef PANELRIGHT_H
#define PANELRIGHT_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QButtonGroup>
#include <QComboBox>
#include <QVector>
#include "../../styles/HexaWidgets.h"
#include "../../backend/BackendTypes.h"

/**
 * @brief The Manual Control Panel widget.
 *
 * @details
 * **Features:**
 * - **Jogging:** Move robot axes incrementally or continuously.
 * - **Coordinate Systems:** Switch between Joint (Angles) and Cartesian (World/Tool) jogging.
 * - **Step Selection:** Variable step sizes (0.1mm to Continuous).
 * - **Monitoring:** Real-time display of actual robot coordinates.
 *
 * **Interaction:**
 * - Emits `jogRequested` signals when buttons are pressed.
 * - Receives updates via `updateState` to refresh coordinate displays.
 * - Updates Tool/Base lists via `onConfigReceived`.
 */
class PanelRight : public QWidget
{
    Q_OBJECT
public:
    explicit PanelRight(QWidget *parent = nullptr);

    /**
     * @brief Updates the UI with the latest robot telemetry.
     * @details Updates the numerical displays for Joint/Cartesian coordinates.
     * Handles the enabling/disabling of controls based on the active coordinate system.
     * @param status The current motion status (positions, active frame).
     */
    void updateState(const HmiMotionStatus &status);

public slots:
    /**
     * @brief Updates the available Tool and Base options.
     * @details Called when the `RobotService` receives a configuration update.
     * Repopulates the ComboBoxes for Jog and Monitor contexts.
     * @param config The system configuration containing Tool/Base lists.
     */
    void onConfigReceived(const HmiSystemConfig &config);

signals:
    /**
     * @brief Emitted when a jog button is pressed.
     * @param axisIndex The axis index (0-5) or Cartesian dimension (X, Y, Z, Rx, Ry, Rz).
     * @param increment The distance/angle to move (signed).
     */
    void jogRequested(int axisIndex, double increment);

    /**
     * @brief Emitted when the "MOVE HOME" button is clicked.
     */
    void homeRequested();

    /**
     * @brief Emitted when the coordinate system tab is changed.
     * @param mode 0=Joint, 1=World, 2=Tool.
     */
    void coordSystemChanged(int mode);

    /**
     * @brief Emitted when the step size is changed.
     * @param val The step size (e.g., 1.0, 10.0). 0.0 usually indicates Continuous mode.
     */
    void stepChanged(double val);

    /**
     * @brief Emitted when the active Jogging Context changes.
     * @param tool The selected tool name.
     * @param base The selected base name.
     */
    void jogContextChanged(QString tool, QString base);

    /**
     * @brief Emitted when the active Monitoring Context changes.
     * @details Affects the coordinates displayed in the "Monitor System" section.
     * @param tool The selected tool name.
     * @param base The selected base name.
     */
    void monitorContextChanged(QString tool, QString base);

private slots:
    void onTabChanged(int id);
    void onStepClicked();
    void onJogBtnPressed();
    void onHomeClicked();
    void onContextChangedSlot();
    void onMonitorContextChangedSlot();
    void onToggleMonitor(bool visible);

private:
    void setupUi();
    QWidget* createActiveJogSection();
    QWidget* createPassiveMonitorSection();

    /// @brief Updates axis labels (e.g., "A1" vs "X") based on selected mode.
    void updateJogLabels(int mode);

    /// @brief Updates monitor labels based on selected reference frame.
    void updateMonitorLabels(bool isJoint);

    /// @brief Enables/Disables jog buttons (e.g. disabled in continuous mode if logic dictates).
    void setJogButtonsEnabled(bool enabled);

    /// @brief Applies the logic for step selection (Continuous vs Incremental).
    void applyStep(const QString &text);

    // --- State ---
    int m_currentMode = 0;   ///< 0=Joint, 1=World, 2=Tool
    double m_currentStep = 1.0;
    bool m_isContinuousMode = false;

    // --- UI Elements ---
    QButtonGroup *m_coordGroup;
    QPushButton *m_btnStep;
    QStringList m_stepOptions;
    int m_stepIndex = 0;

    // Arrays for managing 6 axes dynamically
    QVector<QLabel*> m_axisLabels;
    QVector<QLabel*> m_activeDisplays;
    QPushButton *m_btnHome;
    QVector<QPushButton*> m_jogButtons;

    QComboBox *m_comboJogTool;
    QComboBox *m_comboJogBase;

    QWidget *m_monitorContent;
    QVector<QLabel*> m_passiveLabels;
    QVector<QLabel*> m_passiveDisplays;
    QComboBox *m_comboMonTool;
    QComboBox *m_comboMonBase;
};

#endif // PANELRIGHT_H
