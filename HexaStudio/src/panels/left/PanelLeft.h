/**
 * @file PanelLeft.h
 * @brief Header for the Program Editor Panel.
 * @author HexaKinetica Team
 * @version 1.0
 *
 * This file defines the left-side panel used for creating, editing, and managing
 * robot programs. It implements a visual block editor using a QListView and
 * a custom model.
 */

#ifndef PANELLEFT_H
#define PANELLEFT_H

#include <QWidget>
#include <QListView>
#include <QPushButton>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QComboBox>
#include <QLineEdit>
#include <QTabWidget>

#include "ProgramModel.h"
#include "ProgramDelegate.h"
#include "../../styles/HexaWidgets.h"
#include "../../backend/BackendTypes.h"

/**
 * @brief The Visual Program Editor widget.
 *
 * @details
 * **Functionality:**
 * - **Editor:** Visual list of commands (Motion, Logic, IO).
 * - **Teaching:** "Teach" button to record current robot position into the program.
 * - **Management:** Load/Save programs (local JSON files) and upload to Controller.
 * - **Execution:** Play/Pause/Stop controls.
 *
 * **Architecture:**
 * - Uses `ProgramModel` (QAbstractListModel) to store data.
 * - Uses `ProgramDelegate` to render custom UI blocks.
 * - Auto-syncs changes to the backend via `programChanged` signal.
 */
class PanelLeft : public QWidget
{
    Q_OBJECT
public:
    explicit PanelLeft(QWidget *parent = nullptr);

    /**
     * @brief Externally add a command to the current program.
     * @param cmd The command structure to add.
     */
    void addCommand(const ProgramCommand &cmd);

    /**
     * @brief Updates the UI based on the backend state.
     * @details
     * - Highlights the currently executing line (`progStatus.currentRowIndex`).
     * - Enables/Disables "Teach" buttons based on whether the robot is moving or in simulation.
     * - Updates the Play button state (Run vs Running...).
     *
     * @param progStatus Interpreter status (Running, Paused, Line Index).
     * @param motionStatus Motion status (Used for Teach logic).
     */
    void updateState(const HmiProgramStatus &progStatus, const HmiMotionStatus &motionStatus);

signals:
    /**
     * @brief Emitted when the "Delete" action is triggered.
     */
    void deleteRequested();

    /**
     * @brief Emitted when the user requests to run the program.
     * @param program The complete program structure to execute.
     */
    void playRequested(const QVector<ProgramCommand> &program);

    /**
     * @brief Emitted on ANY modification to the program structure.
     * @details Used for auto-uploading the program to the controller to refresh
     * the trajectory visualization immediately.
     * @param program The updated program structure.
     */
    void programChanged(const QVector<ProgramCommand> &program);

    /**
     * @brief Emitted when the Pause button is clicked.
     */
    void pauseRequested();

    /**
     * @brief Emitted when the Stop button is clicked.
     */
    void stopRequested();

    /**
     * @brief Toggles the visibility of the 3D View toolbar.
     */
    void viewToolbarToggled(bool visible);

    /**
     * @brief Toggles the visibility of the trajectory lines in the 3D view.
     */
    void viewTrajectoryToggled(bool visible);

private slots:
    void onNavClicked(int id);
    void onTeachClicked();
    void onTouchUpClicked();
    void onDeleteClicked();
    void onCopyClicked();
    void onPasteClicked();
    void onSetPPClicked();
    void onInsertCmdClicked();
    void onLoadClicked();
    void onSaveClicked();
    void onDeleteFileClicked();

    void onSyncFromControllerClicked();

    /**
     * @brief Handles selection changes in the list view.
     * @details Updates the "Properties" tab (Edit Tools) to show fields relevant
     * to the selected command type (e.g., Speed for Motion, Condition for Logic).
     */
    void onSelectionChanged(const QModelIndex &current, const QModelIndex &previous);

    void onEditVelocityClicked();
    void onEditConditionClicked();
    void onEditLogicTypeChanged(const QString &type);
    void onUiSettingToggled(bool checked);

private:
    void setupUi();

    // --- UI Component Creators ---
    QWidget* createProgramView();
    QWidget* createFullFilePage();
    QWidget* createGuiSettingsPage();
    QWidget* createRunTools();
    QWidget* createTeachTools();
    QWidget* createCmdTools();
    QWidget* createEditTools();

    // --- Property Editors ---
    QWidget* createEditMotion();
    QWidget* createEditLogic();
    QWidget* createEditIO();
    QWidget* createEditEmpty();

    /// @brief Helper to emit the `programChanged` signal.
    void emitProgramChange();

    // --- Member Variables ---
    QButtonGroup *m_navGroup;
    QStackedWidget *m_mainStack;
    QStackedWidget *m_toolsStack;
    QStackedWidget *m_editStack;
    QListView *m_programView;
    ProgramModel *m_model;
    ProgramDelegate *m_delegate;

    QTabWidget *m_fileTabs;
    QListView *m_fileListLocal;

    // Controls
    QComboBox *m_comboMotionType;
    QPushButton *m_btnPlay;
    QPushButton *m_btnVelocityVal;
    QPushButton *m_btnConditionVal;
    QComboBox *m_comboLogicType;
    QPushButton *m_btnTeach;
    QPushButton *m_btnTouchUp;

    // Clipboard
    ProgramCommand m_copyBuffer;
    bool m_hasCopy = false;

    // State cache for Teaching
    HmiMotionStatus m_lastMotionStatus;
    QString m_displayedProgramName;
};

#endif // PANELLEFT_H
