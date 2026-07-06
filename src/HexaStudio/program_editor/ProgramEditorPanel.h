// --- START OF FILE: HexaStudio/program_editor/ProgramEditorPanel.h ---
/**
 * @file ProgramEditorPanel.h
 * @brief Builder-based, RobotService-free left program-editor panel (standalone module).
 *
 * This is the decoupled replacement for the production PanelLeft. The visual layout is identical
 * (sidebar nav, program list, tools stack, edit sub-pages, controller file page, GUI settings) — only
 * the wiring changed:
 *  - all program content lives in ProgramBuilder (single source of truth); the view uses
 *    ProgramEditorModel; the production ProgramDelegate renders rows unchanged;
 *  - all program mutations go through ProgramBuilder (no inline ProgramCommand construction, no
 *    direct model setData, no magic strings);
 *  - NO dependency on the RobotService singleton: the panel emits intent (play/resume/pause/stop,
 *    programChanged, remote file ops) and receives feedback through slots, so it builds, runs and
 *    tests standalone. The host (MainWindow at integration, FakeController in the bench) wires it up.
 */
#ifndef HEXA_PROGRAM_EDITOR_PANEL_H
#define HEXA_PROGRAM_EDITOR_PANEL_H

#include <QWidget>
#include <QVector>
#include <QString>
#include <QStringList>

class QResizeEvent;

#include "ProgramData.h"            // ProgramCommand, CommandType
#include "ProgramBuilder.h"         // hexa::ProgramBuilder
#include "BackendTypes.h"   // HmiProgramStatus, HmiMotionStatus

class QAbstractButton;
class QButtonGroup;
class QStackedWidget;
class QListView;
class QComboBox;
class QPushButton;
class QStringListModel;
class QModelIndex;
class QLabel;
class ProgramDelegate;

namespace hexa {

class ProgramEditorModel;

class ProgramEditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProgramEditorPanel(QWidget* parent = nullptr);

    /// @brief Reflect controller status: execution line, teach gating, RUN/RESUME button.
    void updateState(const HmiProgramStatus& progStatus, const HmiMotionStatus& motionStatus);

    /// @brief Host-injected guided-demo page: adds the page to the main stack and reveals the
    /// GUIDE nav chip (hidden until a host installs the page — benches don't). The guide is a
    /// separate shell-owned module; this panel only hosts the page and never sees its headers.
    void installGuidePage(QWidget* page);

    // Guide target access (bench-selftest precedent: the guide drives REAL widgets across module
    // boundaries through explicit accessors — never findChild/objectName lookups). These are the
    // real controls the guided demo highlights to teach the teach-and-run workflow. Implemented
    // in the .cpp (full widget types) rather than inline, so the header stays forward-declared.
    QAbstractButton* navRunButton() const;
    QAbstractButton* navUiButton() const;
    QAbstractButton* navTeachButton() const;
    QAbstractButton* navFileButton() const;
    QWidget* programListWidget() const;
    QAbstractButton* runButton() const;
    QAbstractButton* teachButton() const;
    QWidget* controllerFileListWidget() const;
    QAbstractButton* controllerLoadButton() const;

public slots:
    /// @brief External/controller program load: replaces content, does NOT re-upload (no self-echo).
    void loadProgram(const QVector<ProgramCommand>& program);
    /// @brief Initial cached program at startup; does NOT re-upload.
    void setProgram(const QVector<ProgramCommand>& program);
    /// @brief Controller confirmed a program save: clear the UNSAVED marker and adopt the file name.
    /// The controller is the only program store, so no other event may clear the dirty state.
    void confirmProgramSaved(const QString& filename);
    /// @brief Populate the remote (controller storage) file list.
    void setRemoteFileList(const QStringList& files);
    /// @brief Window-state feedback from the shell: the full-screen control must always say what
    /// pressing it will DO next (ENTER vs EXIT FULL SCREEN). The shell owns the window and pushes
    /// every state change here (F11, the on-screen button, OS-driven changes).
    void setFullScreenActive(bool active);

signals:
    void playRequested(const QVector<ProgramCommand>& program);
    void resumeRequested();
    void pauseRequested();
    void stopRequested();
    void programChanged(const QVector<ProgramCommand>& program);
    void remoteListRequested();
    void remoteLoadRequested(const QString& filename);
    void remoteSaveRequested(const QString& filename, const QVector<ProgramCommand>& program);
    void remoteDeleteRequested(const QString& filename);
    void viewToolbarToggled(bool visible);
    void viewTrajectoryToggled(bool visible);
    void approachVisibleToggled(bool visible);   // show/hide the approach+departure trajectory layer
    void ghostVisibleToggled(bool visible);      // show/hide the physical-robot ghost in the viewport
    void tcpFrameVisibleToggled(bool visible);   // show/hide the flange TCP marker (dot + XYZ triad)
    void fullScreenToggleRequested();   // UI tab: toggle full-screen both ways (the shell owns the window)

protected:
    // Re-elides the program-name line when the operator resizes the column (splitter).
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onNavClicked(int id);
    void onPlayResumeClicked();
    void onTeachClicked();
    void onTeachViaClicked();
    void onTouchUpClicked();
    void onDeleteClicked();
    void onCopyClicked();
    void onPasteClicked();
    void onInsertCmdClicked();
    void onSelectionChanged(const QModelIndex& current, const QModelIndex& previous);
    void onEditVelocityClicked();
    void onEditWaitTimeClicked();
    void onEditLabelIdClicked();
    void onEditConditionClicked();   // edit a WAIT DI / IF DI condition (port + trigger level)
    void onEditCommentClicked();     // edit a comment step's text
    void onEditZoneChanged(const QString& zone);
    void onUndoClicked();
    void onRedoClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();

private:
    void setupUi();
    QWidget* createProgramView();
    QWidget* createFullFilePage();
    QWidget* createGuiSettingsPage();
    QWidget* createRunTools();
    QWidget* createTeachTools();
    QWidget* createCmdTools();
    QWidget* createEditTools();
    QWidget* createEditMotion();
    QWidget* createEditLogic();
    QWidget* createEditIO();
    QWidget* createEditGotoLabel();
    QWidget* createEditComment();
    QWidget* createEditEmpty();

    int  currentRow() const;
    void selectRow(int row);
    // Prompt the operator for an IO port (1-32) + level; the titles say whether it is a DI condition
    // (WAIT DI / IF) or a DO output assignment (SET DO). Returns false if cancelled or the port entry
    // was not a number; shared by step insertion and the IO edit button.
    bool promptIoPortState(const QString& portTitle, const QString& stateTitle,
                           int defaultPort, bool defaultState, int& outPort, bool& outState);
    // Prompt for a register index R[0..15] (P4). Returns false if cancelled or not a number.
    bool promptRegisterIndex(int defaultReg, int& outReg);
    // Prompt for a full register condition (register + EQ/NE/GT/LT operator + integer operand);
    // shared by register-IF insertion and its edit button. Returns false on cancel/invalid entry.
    bool promptRegisterCondition(int defaultReg, const QString& defaultOp, int defaultOperand,
                                 int& outReg, QString& outOp, int& outOperand);
    void editTcpComponent(int axisIndex);
    void updateTcpFields(const QVector<double>& tcpPose, bool editable);
    void refreshEditAffordances();
    // TEACH VIA is live only when teaching is allowed AND the selected step is a CIRC point; it is
    // refreshed from both signals that can change that (status tick + selection change).
    void refreshTeachViaButton();
    // Title line = "PROGRAM [• UNSAVED]"; the controller file identity lives on its own second
    // line (m_lblProgramName), elided to the column width so it can never widen the panel
    // (audit A3 + boss 2026-07-07).
    void refreshProgramTitle();   // enable/disable undo/redo/move per builder + selection state
    int  toolsHeightForTab(int navId) const;   // per-tab tools-area height (prevents button row overlap)

    ProgramBuilder*      m_builder = nullptr;   // owned: single source of truth
    ProgramEditorModel*  m_model = nullptr;     // owned: view adapter over m_builder
    ProgramDelegate*     m_delegate = nullptr;

    QButtonGroup*   m_navGroup = nullptr;
    QPushButton*    m_btnNavGuide = nullptr;   // hidden until installGuidePage (guide is host-injected)
    int             m_guidePageIndex = -1;     // main-stack index of the injected guide page
    QStackedWidget* m_mainStack = nullptr;
    QStackedWidget* m_toolsStack = nullptr;
    QStackedWidget* m_editStack = nullptr;
    QListView*      m_programView = nullptr;
    QLabel*         m_lblExecAnnotation = nullptr;  // P5: live registers + last branch readout

    QListView*        m_fileListRemote = nullptr;
    QStringListModel* m_remoteFileModel = nullptr;
    QPushButton*      m_btnRemoteLoad = nullptr;   // CONTROLLER LOAD (guide target)

    QLabel*      m_lblActiveProgram = nullptr;  // title line: "PROGRAM" + unsaved marker
    QLabel*      m_lblProgramName = nullptr;    // 2nd line: file name, elided to the column width (boss 2026-07-07)
    QString      m_loadedProgramName;           // from HmiProgramStatus (audit A3); empty = unnamed
    QLabel*      m_emptyHint = nullptr;         // overlay shown over the list when there are no steps
    QComboBox*   m_comboMotionType = nullptr;
    QPushButton* m_btnPlay = nullptr;
    QPushButton* m_btnUndo = nullptr;
    QPushButton* m_btnRedo = nullptr;
    QPushButton* m_btnMoveUp = nullptr;
    QPushButton* m_btnMoveDown = nullptr;
    QPushButton* m_btnVelocityVal = nullptr;
    QPushButton* m_btnConditionVal = nullptr;
    QPushButton* m_btnWaitTimeVal = nullptr;
    QPushButton* m_btnLabelIdVal = nullptr;
    QComboBox*   m_comboLogicType = nullptr;
    QComboBox*   m_comboZone = nullptr;
    QPushButton* m_btnIoConditionVal = nullptr;   // WAIT DI condition editor (replaces the gated combo)
    QPushButton* m_btnCommentVal = nullptr;       // comment text editor (EDIT page for # steps)
    QPushButton* m_btnFullScreen = nullptr;   // UI tab; label follows the window state (ENTER/EXIT)
    QPushButton* m_btnTeach = nullptr;
    QPushButton* m_btnTeachVia = nullptr;
    QPushButton* m_btnTouchUp = nullptr;
    QPushButton* m_tcpFields[6] {};
    QPushButton* m_btnBlendMode = nullptr;
    QLabel*      m_lblViaVal = nullptr;   // EDIT page: read-only via position of the selected CIRC step

    HmiMotionStatus m_lastMotionStatus;
    bool m_canTeach = false;   // last teach gate from updateState (feeds TEACH VIA enabling)
    bool m_isPaused = false;
    bool m_toolsCollapsed = false;   // when true, the bottom tools area is hidden in every program tab
};

} // namespace hexa

#endif // HEXA_PROGRAM_EDITOR_PANEL_H
// --- END OF FILE: HexaStudio/program_editor/ProgramEditorPanel.h ---
