/**
 * @file PanelTop.h
 * @brief Header for the Top Dashboard Panel.
 * @author HexaKinetica Team
 * @version 1.0
 *
 * Defines the top bar UI which is responsible for high-level system control
 * (Connection, E-Stop, Mode switching) and status visualization.
 */

#ifndef PANELTOP_H
#define PANELTOP_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QStackedWidget>
#include <QComboBox>
#include "../../styles/HexaWidgets.h"
#include "../../backend/BackendTypes.h"

/**
 * @brief The Top Dashboard Panel widget.
 *
 * This panel is always visible at the top of the main window.
 * It contains:
 * - Brand/Logo and Connection Status.
 * - Central controls: Simulation/Real mode switch, Speed Override.
 * - Monitoring controls: Toggle detailed stats (CPU/Temp/Ping).
 * - Emergency Stop button (Red, Right-aligned).
 */
class PanelTop : public QWidget
{
    Q_OBJECT
public:
    explicit PanelTop(QWidget *parent = nullptr);

    /**
     * @brief Updates the UI based on the latest robot status.
     * @param status The system-level status flags (E-Stop, Power, Connectivity).
     * @param isMoving True if the robot is currently executing a motion.
     */
    void updateState(const HmiTopStatus &status, bool isMoving);

signals:
    /**
     * @brief Emitted when the user toggles the Sim/Real switch.
     * @param isRealRobot True for Real Robot, False for Simulation.
     */
    void modeChanged(bool isRealRobot);

    /**
     * @brief Emitted when the E-Stop button is clicked.
     */
    void eStopRequested();

    /**
     * @brief Emitted when the "SETTINGS" button is clicked.
     */
    void settingsRequested();

    /**
     * @brief Emitted when the speed override value changes.
     * @param percent New speed in percent (1-100).
     */
    void speedChanged(int percent);

private slots:
    void onModeToggled(bool checked);
    void onEStopClicked();
    void onToggleMonitor();
    void onSpeedChanged(const QString &text);

private:
    void setupUi();

    QWidget* createLeftSection();
    QWidget* createRightSection();
    QWidget* createCenterSection();
    QWidget* createCenterControls();
    QWidget* createCenterMonitor();

    // --- UI ELEMENTS ---
    QLabel *m_lblBrand;
    QLabel *m_lblStatus;

    QStackedWidget *m_centerStack;

    HexaToggle *m_switchMode;
    QPushButton *m_btnSettings;
    QPushButton *m_btnMonitor;
    QComboBox *m_comboSpeed;
    QPushButton *m_btnCloseMonitor;
    QPushButton *m_btnEStop;

    // Monitor Labels
    QLabel *m_lblCpu;
    QLabel *m_lblTemp;
    QLabel *m_lblPing;

    bool m_isRobotMoving = false;
};

#endif // PANELTOP_H
