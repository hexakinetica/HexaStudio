// --- START OF FILE: HexaStudio/program_editor/ProgramEditorPanel.cpp ---
#include "ProgramEditorPanel.h"
#include "ProgramEditorModel.h"

#include "HexaTheme.h"
#include "HexaWidgets.h"
#include "ProgramDelegate.h"
#include "ProgramIssueDelegate.h"
#include "cyberkeyboard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStackedLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QComboBox>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QStringListModel>
#include <QMessageBox>
#include <QVariant>
#include <array>

namespace hexa {

namespace {

constexpr std::array<const char*, 6> kTcpFieldLabels = {"X", "Y", "Z", "Rx", "Ry", "Rz"};
constexpr std::array<const char*, 6> kTcpFieldUnits  = {"mm", "mm", "mm", "deg", "deg", "deg"};

// Sidebar tab ids. These double as the QButtonGroup ids AND the tools-stack page order (Run..Edit),
// so the mapping tools-page == nav-id is explicit rather than a bare integer coincidence.
enum NavId { NavRun = 0, NavTeach, NavCmd, NavEdit, NavFile, NavUi, NavGuide };
// Pages of the main stack (program view / file page / GUI settings). The guided-demo page is
// host-injected (installGuidePage) and tracked by m_guidePageIndex, not by this enum.
enum MainPage { PageProgram = 0, PageFile, PageGui };
// Pages of the EDIT properties sub-stack.
enum EditPage { EditMotion = 0, EditLogic, EditIo, EditGotoLabel, EditComment, EditEmpty };
// Dynamic property carrying a stable command id on CMD buttons (dispatch key, not the display text).
constexpr const char* kCmdIdProperty = "cmdId";

// Tool-area button heights (10-inch touch panel: gloved-finger targets). The tools stack uses
// fixed per-tab heights (toolsHeightForTab), so these values are load-bearing for that
// arithmetic - change them together. TEACH is the tightest fit: 5 rows * 44 + spacing = 245/250.
constexpr int kToolButtonHeightPx = 44;
constexpr int kValueFieldHeightPx = 34;

// The setComboItemEnabled helper that gated CIRC/SPLINE motion items lived here; it was removed
// with the last gated combo item (all motion types are executable now — mandate rule 7, no dead code).

} // namespace

ProgramEditorPanel::ProgramEditorPanel(QWidget* parent)
    : QWidget(parent) {
    qRegisterMetaType<QVector<ProgramCommand>>("QVector<ProgramCommand>");
    qRegisterMetaType<QVector<double>>("QVector<double>");

    m_builder = new ProgramBuilder(this);
    setupUi();

    // Every local content mutation pushes the program out for upload (replaces the old per-handler
    // emitProgramChange()).
    connect(m_builder, &ProgramBuilder::stepInserted, this, [this](int) { emit programChanged(m_builder->program()); });
    connect(m_builder, &ProgramBuilder::stepRemoved,  this, [this](int) { emit programChanged(m_builder->program()); });
    connect(m_builder, &ProgramBuilder::stepChanged,  this, [this](int) { emit programChanged(m_builder->program()); });
    connect(m_builder, &ProgramBuilder::stepMoved,    this, [this](int, int) { emit programChanged(m_builder->program()); });
    // A full reset (undo/redo/clear) is a LocalEdit and must be uploaded too; an
    // ExternalLoad (controller/initial cache) must NOT echo back as an upload.
    connect(m_builder, &ProgramBuilder::programReset, this, [this](ProgramResetReason reason) {
        if (reason == ProgramResetReason::LocalEdit) {
            emit programChanged(m_builder->program());
        }
    });

    // Dirty marker + file identity: both land in the one title label (audit A3).
    connect(m_builder, &ProgramBuilder::modifiedChanged, this,
            [this](bool) { refreshProgramTitle(); });

    // Undo/redo availability and reorder bounds change on every content event and selection move.
    connect(m_builder, &ProgramBuilder::stepInserted, this, [this](int) { refreshEditAffordances(); });
    connect(m_builder, &ProgramBuilder::stepRemoved,  this, [this](int) { refreshEditAffordances(); });
    connect(m_builder, &ProgramBuilder::stepChanged,  this, [this](int) { refreshEditAffordances(); });
    connect(m_builder, &ProgramBuilder::stepMoved,    this, [this](int, int) { refreshEditAffordances(); });
    connect(m_builder, &ProgramBuilder::programReset, this, [this](ProgramResetReason) { refreshEditAffordances(); });
    refreshEditAffordances();
}

void ProgramEditorPanel::refreshProgramTitle() {
    if (!m_lblActiveProgram) {
        return;
    }
    // Boss review 2026-07-07: the file name lives on its OWN second line under the title, and the
    // panel must never auto-expand for it (the one-line "PROGRAM • name • UNSAVED" forced the left
    // column wider). The name label's Ignored horizontal policy keeps the column width owned by
    // the operator (splitter only); the name is elided to whatever width the column actually has,
    // with the full name on the tooltip.
    QString title = QStringLiteral("PROGRAM");
    if (m_builder && m_builder->isModified()) {
        title += QStringLiteral(" • UNSAVED");
    }
    m_lblActiveProgram->setText(title);

    if (m_lblProgramName) {
        if (m_loadedProgramName.isEmpty()) {
            m_lblProgramName->clear();
            m_lblProgramName->setVisible(false);
            m_lblProgramName->setToolTip(QString());
        } else {
            const QFontMetrics fm(m_lblProgramName->font());
            const int width_px = m_lblProgramName->width() > 20 ? m_lblProgramName->width() - 4 : 260;
            m_lblProgramName->setText(fm.elidedText(m_loadedProgramName, Qt::ElideMiddle, width_px));
            m_lblProgramName->setToolTip(m_loadedProgramName);
            m_lblProgramName->setVisible(true);
        }
    }
}

void ProgramEditorPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Re-elide the program-name line to the new column width (manual splitter resize).
    refreshProgramTitle();
}

int ProgramEditorPanel::currentRow() const {
    const QModelIndex idx = m_programView->currentIndex();
    return idx.isValid() ? idx.row() : -1;
}

void ProgramEditorPanel::selectRow(int row) {
    if (row >= 0 && row < m_model->rowCount()) {
        m_programView->setCurrentIndex(m_model->index(row));
    }
}

void ProgramEditorPanel::updateState(const HmiProgramStatus& progStatus, const HmiMotionStatus& motionStatus) {
    m_lastMotionStatus = motionStatus;
    if (m_loadedProgramName != progStatus.loadedProgramName) {
        m_loadedProgramName = progStatus.loadedProgramName;
        refreshProgramTitle();
    }
    m_model->setActiveRow(progStatus.currentRowIndex);
    m_isPaused = progStatus.isPaused;

    // P5 execution annotation: non-zero registers + the last evaluated IF. Empty (hidden) until a
    // run first produces something; kept after the run as a "what happened" record.
    if (m_lblExecAnnotation) {
        QStringList parts;
        for (int i = 0; i < progStatus.registers.size(); ++i) {
            if (progStatus.registers[i] != 0) {
                parts.append(QStringLiteral("R%1=%2").arg(i).arg(progStatus.registers[i]));
            }
        }
        if (progStatus.lastBranchLine >= 0) {
            parts.append(QStringLiteral("IF@%1 %2")
                             .arg(progStatus.lastBranchLine)
                             .arg(progStatus.lastBranchTaken ? QStringLiteral("taken")
                                                             : QStringLiteral("not taken")));
        }
        const QString text = parts.join(QStringLiteral("  ·  "));
        m_lblExecAnnotation->setText(text);
        m_lblExecAnnotation->setVisible(!text.isEmpty());
    }

    const bool isSim = motionStatus.isSimulated;
    const bool canTeach = !progStatus.isRunning && !motionStatus.isMoving && !isSim;
    m_canTeach = canTeach;
    if (m_btnTeach) {
        m_btnTeach->setEnabled(canTeach);
        m_btnTeach->setToolTip(isSim ? "Switch to REAL mode to Teach" : "Record Position");
    }
    if (m_btnTouchUp) {
        m_btnTouchUp->setEnabled(canTeach);
        m_btnTouchUp->setToolTip(isSim ? "Switch to REAL mode to Touch Up" : "Update Position");
    }
    refreshTeachViaButton();

    // PAUSED -> RESUME (continue from current step), else RUN; RUNNING disabled. The button style is set
    // once at creation (identical across all three states), so it is intentionally NOT re-applied here:
    // updateState runs on every status tick and re-polishing the stylesheet each time would be wasteful.
    if (m_btnPlay) {
        if (progStatus.isRunning) {
            m_btnPlay->setText("RUNNING...");
            m_btnPlay->setEnabled(false);
        } else if (progStatus.isPaused) {
            m_btnPlay->setText("RESUME");
            m_btnPlay->setEnabled(true);
        } else {
            m_btnPlay->setText("RUN");
            m_btnPlay->setEnabled(true);
        }
    }
}

void ProgramEditorPanel::onPlayResumeClicked() {
    if (m_isPaused) {
        emit resumeRequested();
        return;
    }
    // Author-side RUN gate (REQ-0009): refuse to start a program that has blocking (Error) issues.
    // The controller stays the final safety arbiter; this is the fast local pre-check that prevents
    // sending a known-bad program (empty, motion point without a taught pose, speed out of range).
    const QVector<ProgramIssue> issues = m_builder->validate();
    QStringList blocking;
    int firstErrorRow = -1;
    for (const ProgramIssue& issue : issues) {
        if (issue.severity == IssueSeverity::Error) {
            if (firstErrorRow < 0) {
                firstErrorRow = issue.stepIndex;
            }
            blocking << issue.message;
        }
    }
    if (!blocking.isEmpty()) {
        if (firstErrorRow >= 0) {
            selectRow(firstErrorRow);
        }
        QMessageBox::warning(this, QStringLiteral("Cannot run program"),
                             QStringLiteral("The program was not started because it has blocking "
                                            "issues:\n\n- %1").arg(blocking.join(QStringLiteral("\n- "))));
        return;
    }
    emit playRequested(m_builder->program());
}

void ProgramEditorPanel::onTeachClicked() {
    if (!m_btnTeach->isEnabled()) {
        return;
    }
    const QString motionType = m_comboMotionType->currentText(); // PTP/LIN/CIRC/SPLINE
    MotionKind kind = MotionKind::PTP;
    if (motionType == QLatin1String("LIN")) {
        kind = MotionKind::LIN;
    } else if (motionType == QLatin1String("CIRC")) {
        kind = MotionKind::CIRC;
    } else if (motionType == QLatin1String("SPLINE")) {
        kind = MotionKind::SPLINE;
    }
    const bool blend = (m_btnBlendMode && m_btnBlendMode->isChecked());
    auto r = m_builder->addMotionPoint(kind, m_lastMotionStatus.actualJoints, m_lastMotionStatus.actualTcp,
                                       m_lastMotionStatus.activeToolId, m_lastMotionStatus.activeBaseId,
                                       blend, currentRow());
    if (r.isSuccess()) {
        selectRow(r.value());
    }
}

void ProgramEditorPanel::onTeachViaClicked() {
    if (!m_btnTeachVia || !m_btnTeachVia->isEnabled()) {
        return;
    }
    // Record the current robot TCP as the arc's auxiliary point on the selected CIRC step (the
    // KUKA-style two-touch flow: + TEACH already recorded the target). The enable gate guarantees
    // a CIRC row is selected; a failure here is a programming error surfaced by the Result.
    auto r = m_builder->teachVia(currentRow(), m_lastMotionStatus.actualTcp);
    if (r.isError()) {
        QMessageBox::warning(this, QStringLiteral("Teach via point"),
                             QStringLiteral("Could not record the via point: %1.").arg(toString(r.error())));
        return;
    }
    // Refresh the EDIT affordances (via display) for the unchanged selection.
    onSelectionChanged(m_programView->currentIndex(), QModelIndex());
}

void ProgramEditorPanel::onTouchUpClicked() {
    if (!m_btnTouchUp->isEnabled()) {
        return;
    }
    m_builder->touchUp(currentRow(), m_lastMotionStatus.actualJoints, m_lastMotionStatus.actualTcp);
}

void ProgramEditorPanel::setupUi() {
    QHBoxLayout* globalLayout = new QHBoxLayout(this);
    globalLayout->setContentsMargins(0, 0, 0, 0);
    globalLayout->setSpacing(0);
    QWidget* sidebar = new QWidget();
    sidebar->setFixedWidth(64);
    // The host card paints the surface; the sidebar only marks its edge with a hairline.
    sidebar->setStyleSheet(QStringLiteral(
        "background: transparent; border: none; border-right: 1px solid %1;").arg(Hexa::Colors::HairlineSoft));
    QVBoxLayout* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(4, 8, 8, 8);
    sideLayout->setSpacing(8);
    m_navGroup = new QButtonGroup(this);
    // One nav language: muted label when idle, an accent-tinted rounded chip when checked. The
    // previous per-tab colours (cyan/violet/grey) were decorative and collided with the state
    // colours used across the HMI (violet elsewhere means "motion active").
    auto createNavBtn = [&](const QString& text, int id) {
        QPushButton* btn = new QPushButton(text);
        btn->setCheckable(true); btn->setFixedSize(52, 52); btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString(
            "QPushButton { background: transparent; border: none; border-radius: 10px;"
            " color: %1; font-family: '%2'; font-size: 11px; font-weight: 600; }"
            "QPushButton:checked { background-color: %3; color: %4; }"
            "QPushButton:hover:!checked { background-color: rgba(255, 255, 255, 0.04); color: %5; }")
            .arg(Hexa::Colors::TextMuted, Hexa::Fonts::familyUI(), Hexa::Colors::StateHover,
                 Hexa::Colors::Primary, Hexa::Colors::Accent));
        m_navGroup->addButton(btn, id);
        sideLayout->addWidget(btn);
        return btn;
    };
    QPushButton* btnRun = createNavBtn("RUN", NavRun);
    createNavBtn("TEACH", NavTeach);
    createNavBtn("CMD", NavCmd);
    createNavBtn("EDIT", NavEdit);
    sideLayout->addSpacing(10);
    QFrame* sep = new QFrame(); sep->setFrameShape(QFrame::HLine); sep->setStyleSheet(Hexa::Styles::Separator);
    sideLayout->addWidget(sep); sideLayout->addSpacing(10);
    createNavBtn("FILE", NavFile);
    createNavBtn("UI", NavUi);
    sideLayout->addStretch();
    // GUIDE (guided demo) sits alone at the BOTTOM, visually apart from the authoring tabs
    // (boss decision 2026-07-06). Hidden until a host injects the page via installGuidePage —
    // the guide is a separate shell-owned module and benches run this panel without it.
    m_btnNavGuide = createNavBtn("GUIDE", NavGuide);
    m_btnNavGuide->setVisible(false);

    btnRun->setChecked(true);
    connect(m_navGroup, &QButtonGroup::idClicked, this, &ProgramEditorPanel::onNavClicked);
    m_mainStack = new QStackedWidget();
    m_mainStack->addWidget(createProgramView());
    m_mainStack->addWidget(createFullFilePage());
    m_mainStack->addWidget(createGuiSettingsPage());
    globalLayout->addWidget(sidebar); globalLayout->addWidget(m_mainStack);
}

void ProgramEditorPanel::installGuidePage(QWidget* page) {
    m_guidePageIndex = m_mainStack->addWidget(page);
    m_btnNavGuide->setVisible(true);
}

// Guide target access: real widgets handed to the shell's guide registry through explicit
// accessors (never findChild — hidden data flow). NavId stays file-local; only the accessors
// cross the module boundary.
QAbstractButton* ProgramEditorPanel::navRunButton() const {
    return m_navGroup->button(NavRun);
}

QAbstractButton* ProgramEditorPanel::navUiButton() const {
    return m_navGroup->button(NavUi);
}

QAbstractButton* ProgramEditorPanel::navTeachButton() const {
    return m_navGroup->button(NavTeach);
}

QAbstractButton* ProgramEditorPanel::navFileButton() const {
    return m_navGroup->button(NavFile);
}

QWidget* ProgramEditorPanel::programListWidget() const {
    return m_programView;
}

QAbstractButton* ProgramEditorPanel::runButton() const {
    return m_btnPlay;
}

QAbstractButton* ProgramEditorPanel::teachButton() const {
    return m_btnTeach;
}

QWidget* ProgramEditorPanel::controllerFileListWidget() const {
    return m_fileListRemote;
}

QAbstractButton* ProgramEditorPanel::controllerLoadButton() const {
    return m_btnRemoteLoad;
}

QWidget* ProgramEditorPanel::createProgramView() {
    QWidget* container = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(5, 5, 5, 5); layout->setSpacing(5);
    // Title row: program name (with unsaved marker) on the left; program-wide UNDO/REDO and the
    // tools-collapse toggle on the right, so all three are reachable from every tab.
    QWidget* titleRow = new QWidget();
    QHBoxLayout* titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(4);
    // "PROGRAM", not "ACTIVE PROGRAM": with the shell's stretch factors the left column stays at
    // its 300px working width, and the longer title cannot coexist with UNDO/REDO/collapse in that
    // row (probe screenshot evidence: it rendered as "IVE PROGR"). Left-aligned so that if width
    // still runs out (splitter dragged to minimum) the readable prefix survives, not the middle.
    m_lblActiveProgram = HexaWidgets::createLabelSectionTitle("PROGRAM");
    m_lblActiveProgram->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    titleLayout->addWidget(m_lblActiveProgram);
    titleLayout->addStretch();

    auto makeTitleButton = [](const QString& text, const QString& tip, int width) {
        QPushButton* b = new QPushButton(text);
        b->setFixedSize(width, Hexa::Dim::BtnCompact);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
        return b;
    };
    m_btnUndo = makeTitleButton(QStringLiteral("UNDO"), QStringLiteral("Undo the last change"), 44);
    connect(m_btnUndo, &QPushButton::clicked, this, &ProgramEditorPanel::onUndoClicked);
    titleLayout->addWidget(m_btnUndo);
    m_btnRedo = makeTitleButton(QStringLiteral("REDO"), QStringLiteral("Redo the last undone change"), 44);
    connect(m_btnRedo, &QPushButton::clicked, this, &ProgramEditorPanel::onRedoClicked);
    titleLayout->addWidget(m_btnRedo);

    // Collapse toggle: hides ONLY the bottom tools area (m_toolsStack) in every program tab, so the
    // program list keeps and gains that space. Placed next to the list it affects.
    QPushButton* btnCollapse = makeTitleButton(QStringLiteral("▼"),
                                               QStringLiteral("Hide the tool buttons to enlarge the program list"), 40);
    btnCollapse->setCheckable(true);
    connect(btnCollapse, &QPushButton::toggled, this, [this, btnCollapse](bool collapsed) {
        m_toolsCollapsed = collapsed;
        m_toolsStack->setVisible(!collapsed);
        if (!collapsed) {
            m_toolsStack->setFixedHeight(toolsHeightForTab(m_navGroup->checkedId()));
        }
        btnCollapse->setText(collapsed ? QStringLiteral("▲") : QStringLiteral("▼"));
        btnCollapse->setToolTip(collapsed ? QStringLiteral("Show the tool buttons")
                                          : QStringLiteral("Hide the tool buttons to enlarge the program list"));
    });
    titleLayout->addWidget(btnCollapse);
    layout->addWidget(titleRow);

    // Second line: the loaded program's file name (boss 2026-07-07). Horizontal policy IGNORED so
    // a long name can NEVER widen the left column - the column width belongs to the operator
    // (splitter). refreshProgramTitle() elides the text to the actual width; hidden until a
    // program identity exists (no empty gap).
    m_lblProgramName = new QLabel();
    m_lblProgramName->setStyleSheet(Hexa::Styles::LabelBorderless.arg(
        Hexa::Colors::TextMuted, Hexa::Fonts::familyMono(), "11"));
    m_lblProgramName->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_lblProgramName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_lblProgramName->setVisible(false);
    layout->addWidget(m_lblProgramName);

    m_programView = new QListView();
    m_model = new ProgramEditorModel(m_builder, this);
    m_delegate = new ProgramIssueDelegate(this);
    m_programView->setModel(m_model);
    m_programView->setItemDelegate(m_delegate);
    m_programView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_programView->setSpacing(2);
    m_programView->setStyleSheet(Hexa::Styles::ListView);
    connect(m_programView->selectionModel(), &QItemSelectionModel::currentChanged, this, &ProgramEditorPanel::onSelectionChanged);

    // Empty-state hint overlaid on the list (StackAll keeps both visible; the hint is shown only when
    // the program has no steps, guiding the operator to the first action).
    QWidget* listContainer = new QWidget();
    QStackedLayout* listStack = new QStackedLayout(listContainer);
    listStack->setStackingMode(QStackedLayout::StackAll);
    listStack->addWidget(m_programView);
    m_emptyHint = new QLabel(QStringLiteral("No steps yet.\nUse TEACH to record a point, or CMD to add a command."));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);   // the hint must wrap inside the list width, not clip at its edges
    m_emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyHint->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Hexa::Colors::TextMuted));
    listStack->addWidget(m_emptyHint);
    listStack->setCurrentWidget(m_emptyHint); // raise the hint above the list
    layout->addWidget(listContainer, 1);

    // P5 execution annotation strip: live registers ("R0=3 R2=7") and the last evaluated IF with
    // its outcome ("IF@4 taken"). Hidden while there is nothing to show (no run yet); after a run
    // it intentionally KEEPS the final values — a "what happened" record, same convention as the
    // viewport's ghosted executed trace.
    m_lblExecAnnotation = new QLabel();
    m_lblExecAnnotation->setStyleSheet(
        QString("color: %1; font-size: 11px; padding: 1px 4px;").arg(Hexa::Colors::TextMuted));
    m_lblExecAnnotation->setVisible(false);
    layout->addWidget(m_lblExecAnnotation);

    m_toolsStack = new QStackedWidget();
    m_toolsStack->setFixedHeight(toolsHeightForTab(NavRun)); // default tab is RUN
    m_toolsStack->setStyleSheet("background: transparent; border: none;");
    m_toolsStack->addWidget(createRunTools());
    m_toolsStack->addWidget(createTeachTools());
    m_toolsStack->addWidget(createCmdTools());
    m_toolsStack->addWidget(createEditTools());
    layout->addWidget(m_toolsStack);
    return container;
}

QWidget* ProgramEditorPanel::createRunTools() {
    QWidget* p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QHBoxLayout* l = new QHBoxLayout(p); l->setSpacing(5); l->setContentsMargins(0, 0, 0, 0);
    // RUN is the panel's single filled call-to-action (ButtonPrimary); PAUSE stays secondary and
    // STOP keeps the danger style, so the three read as main / secondary / abort at a glance.
    m_btnPlay = new QPushButton("RUN");
    m_btnPlay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_btnPlay->setStyleSheet(Hexa::Styles::ButtonPrimary);
    connect(m_btnPlay, &QPushButton::clicked, this, &ProgramEditorPanel::onPlayResumeClicked);
    QPushButton* btnPause = new QPushButton("PAUSE");
    btnPause->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    btnPause->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    connect(btnPause, &QPushButton::clicked, this, &ProgramEditorPanel::pauseRequested);
    QPushButton* btnStop = new QPushButton("STOP");
    btnStop->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    btnStop->setStyleSheet(Hexa::Styles::ButtonDangerNormal);
    connect(btnStop, &QPushButton::clicked, this, &ProgramEditorPanel::stopRequested);
    // RUN takes double width: the main action dominates the row (premium hierarchy on touch).
    l->addWidget(m_btnPlay, 2); l->addWidget(btnPause, 1); l->addWidget(btnStop, 1);
    return p;
}

QWidget* ProgramEditorPanel::createTeachTools() {
    QWidget* p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QGridLayout* l = new QGridLayout(p); l->setSpacing(5); l->setContentsMargins(0, 5, 0, 0);
    m_comboMotionType = HexaWidgets::createComboBox();
    m_comboMotionType->addItems({"PTP", "LIN", "CIRC", "SPLINE"});
    // All four motion types are executable end-to-end: CIRC (docs/REQ_motion_circ.md) and SPLINE
    // (docs/REQ_motion_spline.md — consecutive SPLINE points execute as ONE smooth curve).
    m_comboMotionType->setFixedHeight(kToolButtonHeightPx);
    m_comboMotionType->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    l->addWidget(m_comboMotionType, 0, 0);
    QPushButton* btnSetPP = new QPushButton("SET PP"); btnSetPP->setFixedHeight(kToolButtonHeightPx);
    btnSetPP->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    btnSetPP->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    // SET PP gated: needs a controller start-index in the RDT command (not present yet).
    btnSetPP->setEnabled(false);
    btnSetPP->setToolTip(QStringLiteral("Start-from-selected-line requires controller support - planned."));
    l->addWidget(btnSetPP, 0, 1);
    m_btnTeach = new QPushButton("+ TEACH"); m_btnTeach->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_btnTeach->setMinimumHeight(kToolButtonHeightPx); m_btnTeach->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    connect(m_btnTeach, &QPushButton::clicked, this, &ProgramEditorPanel::onTeachClicked);
    l->addWidget(m_btnTeach, 1, 0);
    m_btnTouchUp = new QPushButton("TOUCH UP"); m_btnTouchUp->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_btnTouchUp->setMinimumHeight(kToolButtonHeightPx); m_btnTouchUp->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    connect(m_btnTouchUp, &QPushButton::clicked, this, &ProgramEditorPanel::onTouchUpClicked);
    l->addWidget(m_btnTouchUp, 1, 1);

    // TEACH VIA records the auxiliary point of the SELECTED CIRC step from the current robot pose
    // (two-touch CIRC flow, docs/REQ_motion_circ.md). Enabled only when teaching is allowed and a
    // CIRC row is selected — for every other selection it reads as OFF with an explaining tooltip.
    m_btnTeachVia = new QPushButton("TEACH VIA");
    m_btnTeachVia->setMinimumHeight(kToolButtonHeightPx);
    m_btnTeachVia->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_btnTeachVia->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    m_btnTeachVia->setEnabled(false);
    connect(m_btnTeachVia, &QPushButton::clicked, this, &ProgramEditorPanel::onTeachViaClicked);
    l->addWidget(m_btnTeachVia, 2, 0, 1, 2);

    m_btnBlendMode = new QPushButton("BLEND: OFF");
    m_btnBlendMode->setCheckable(true);
    m_btnBlendMode->setMinimumHeight(kToolButtonHeightPx);
    m_btnBlendMode->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    // Latched-warning look comes from the :checked rule (ButtonCheckedWarning) - the style is set
    // once and Qt tracks the state; only the text needs the toggle handler.
    m_btnBlendMode->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract
                                  + Hexa::Styles::ButtonCheckedWarning);
    m_btnBlendMode->setToolTip("ON: newly taught points are recorded with corner blending (APPROX 50mm) "
                               "instead of an exact stop (FINE). Change any point's zone in its properties.");
    connect(m_btnBlendMode, &QPushButton::toggled, this, [this](bool on) {
        m_btnBlendMode->setText(on ? "BLEND: ON" : "BLEND: OFF");
    });
    l->addWidget(m_btnBlendMode, 3, 0, 1, 2);

    QHBoxLayout* row3 = new QHBoxLayout(); row3->setSpacing(5);
    QPushButton* btnCopy = new QPushButton("COPY"); btnCopy->setFixedHeight(kToolButtonHeightPx); btnCopy->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    connect(btnCopy, &QPushButton::clicked, this, &ProgramEditorPanel::onCopyClicked);
    row3->addWidget(btnCopy);
    QPushButton* btnPaste = new QPushButton("PASTE"); btnPaste->setFixedHeight(kToolButtonHeightPx); btnPaste->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    connect(btnPaste, &QPushButton::clicked, this, &ProgramEditorPanel::onPasteClicked);
    row3->addWidget(btnPaste);
    QPushButton* btnDel = new QPushButton("DEL"); btnDel->setFixedHeight(kToolButtonHeightPx); btnDel->setStyleSheet(Hexa::Styles::ButtonDangerNormal);
    connect(btnDel, &QPushButton::clicked, this, &ProgramEditorPanel::onDeleteClicked);
    row3->addWidget(btnDel);
    l->addLayout(row3, 4, 0, 1, 2);

    // Reorder controls. MOVE stays in TEACH per the boss decision; UNDO/REDO live in the program title
    // row (program-wide, reachable from every tab).
    QHBoxLayout* row4 = new QHBoxLayout(); row4->setSpacing(5);
    m_btnMoveUp = new QPushButton("MOVE ▲"); m_btnMoveUp->setFixedHeight(kToolButtonHeightPx);
    m_btnMoveUp->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    connect(m_btnMoveUp, &QPushButton::clicked, this, &ProgramEditorPanel::onMoveUpClicked);
    row4->addWidget(m_btnMoveUp);
    m_btnMoveDown = new QPushButton("MOVE ▼"); m_btnMoveDown->setFixedHeight(kToolButtonHeightPx);
    m_btnMoveDown->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    connect(m_btnMoveDown, &QPushButton::clicked, this, &ProgramEditorPanel::onMoveDownClicked);
    row4->addWidget(m_btnMoveDown);
    l->addLayout(row4, 5, 0, 1, 2);
    return p;
}

QWidget* ProgramEditorPanel::createCmdTools() {
    QWidget* p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QGridLayout* grid = new QGridLayout(p); grid->setContentsMargins(0, 5, 0, 0); grid->setSpacing(5);
    // Command table: {display label, stable id (dispatch key), wired}. Only wired commands are enabled;
    // the rest are gated (visible-but-disabled) until their data path reaches the controller. Dispatch
    // uses the stable id carried as a property, never the display text.
    struct CmdDef { const char* label; const char* id; };
    // Every command is wired end-to-end as of sequencer P4 (registers + BREAK); the gating
    // machinery (visible-but-disabled + planned tooltip) was removed with its last user.
    const std::array<CmdDef, 11> cmds = {{
        {"WAIT TIME", "WAIT_TIME"},
        {"SET DO",    "SET_DO"},
        {"WAIT DI",   "WAIT_DI"},
        {"COMMENT",   "COMMENT"},
        {"IF / ELSE", "IF_ELSE"},
        {"GOTO",      "GOTO"},
        {"LABEL",     "LABEL"},
        {"BREAK",     "BREAK"},
        {"SET VAR",   "SET_VAR"},
        {"INC VAR",   "INC_VAR"},
        {"DEC VAR",   "DEC_VAR"},
    }};
    // Two columns (wider labels stay readable at the panel's working width - boss live review);
    // eleven commands make six rows. toolsHeightForTab(NavCmd) carries the matching height.
    int row = 0, col = 0;
    for (const CmdDef& def : cmds) {
        QPushButton* btn = new QPushButton(QString::fromLatin1(def.label));
        btn->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
        btn->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
        btn->setProperty(kCmdIdProperty, QString::fromLatin1(def.id));
        connect(btn, &QPushButton::clicked, this, &ProgramEditorPanel::onInsertCmdClicked);
        grid->addWidget(btn, row, col);
        col++; if (col > 1) { col = 0; row++; }
    }
    return p;
}

QWidget* ProgramEditorPanel::createEditTools() {
    QWidget* p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QVBoxLayout* l = new QVBoxLayout(p); l->setContentsMargins(0, 0, 0, 0);
    QLabel* lblType = new QLabel("EDIT PROPERTIES"); lblType->setAlignment(Qt::AlignCenter); lblType->setStyleSheet(Hexa::Styles::LabelHeaderSimple);
    l->addWidget(lblType);
    m_editStack = new QStackedWidget();
    // Page order MUST match the EditPage enum above.
    m_editStack->addWidget(createEditMotion());
    m_editStack->addWidget(createEditLogic());
    m_editStack->addWidget(createEditIO());
    m_editStack->addWidget(createEditGotoLabel());
    m_editStack->addWidget(createEditComment());
    m_editStack->addWidget(createEditEmpty());
    l->addWidget(m_editStack);
    return p;
}

QWidget* ProgramEditorPanel::createEditMotion() {
    QWidget* p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QGridLayout* l = new QGridLayout(p); l->setContentsMargins(0, 0, 0, 0); l->setSpacing(4);
    l->setAlignment(Qt::AlignTop);   // properties hug the section title, not the vertical centre
    l->addWidget(HexaWidgets::createLabelText("VEL:"), 0, 0);
    m_btnVelocityVal = new QPushButton("100%"); m_btnVelocityVal->setFixedHeight(kValueFieldHeightPx); m_btnVelocityVal->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    connect(m_btnVelocityVal, &QPushButton::clicked, this, &ProgramEditorPanel::onEditVelocityClicked);
    l->addWidget(m_btnVelocityVal, 0, 1);
    l->addWidget(HexaWidgets::createLabelText("ZONE:"), 0, 2);
    m_comboZone = HexaWidgets::createComboBox();
    m_comboZone->addItems({"FINE", "APPROX 1mm", "APPROX 5mm", "APPROX 10mm",
                           "APPROX 25mm", "APPROX 50mm", "APPROX 100mm"});
    m_comboZone->setFixedHeight(kValueFieldHeightPx);   // align with the VEL field in the same row
    m_comboZone->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(m_comboZone, &QComboBox::currentTextChanged, this, &ProgramEditorPanel::onEditZoneChanged);
    l->addWidget(m_comboZone, 0, 3, 1, 3);

    for (int i = 0; i < 6; ++i) {
        const int row = 1 + i / 3;
        const int baseCol = (i % 3) * 2;
        l->addWidget(HexaWidgets::createLabelText(kTcpFieldLabels[i]), row, baseCol);
        QPushButton* field = new QPushButton("0.00");
        field->setFixedHeight(kValueFieldHeightPx);
        field->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
        connect(field, &QPushButton::clicked, this, [this, i]() { editTcpComponent(i); });
        l->addWidget(field, row, baseCol + 1);
        m_tcpFields[i] = field;
    }

    // CIRC via-point display (read-only). The via is taught with TEACH VIA, not typed: its whole
    // value is one physical robot pose, so per-component keypad editing would invite inconsistent
    // arcs. Hidden for non-CIRC steps.
    m_lblViaVal = HexaWidgets::createLabelText(QStringLiteral("VIA: not taught"));
    m_lblViaVal->setToolTip(QStringLiteral("Auxiliary point of the CIRC arc. Re-record it with TEACH VIA."));
    m_lblViaVal->setVisible(false);
    l->addWidget(m_lblViaVal, 3, 0, 1, 6);
    return p;
}

QWidget* ProgramEditorPanel::createEditLogic() {
    QWidget* p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QGridLayout* l = new QGridLayout(p); l->setContentsMargins(0, 0, 0, 0);
    l->setAlignment(Qt::AlignTop);
    l->addWidget(HexaWidgets::createLabelText("BLOCK:"), 0, 0);
    m_comboLogicType = HexaWidgets::createComboBox(); m_comboLogicType->addItems({"IF", "ELSE", "END_IF", "WAIT"});
    m_comboLogicType->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    // Block-type switching is gated (only WAIT is creatable/wired; IF/ELSE/END_IF are not). The combo
    // is shown for display of a selected step but not editable until structured logic is wired.
    m_comboLogicType->setEnabled(false);
    m_comboLogicType->setToolTip(QStringLiteral("Logic block editing is planned (control flow not wired yet)."));
    l->addWidget(m_comboLogicType, 0, 1);
    l->addWidget(HexaWidgets::createLabelText("COND:"), 1, 0);
    m_btnConditionVal = new QPushButton("---"); m_btnConditionVal->setFixedHeight(kToolButtonHeightPx); m_btnConditionVal->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    m_btnConditionVal->setToolTip(QStringLiteral("Edit the DI port and trigger level this IF branches on."));
    connect(m_btnConditionVal, &QPushButton::clicked, this, &ProgramEditorPanel::onEditConditionClicked);
    l->addWidget(m_btnConditionVal, 1, 1);
    l->addWidget(HexaWidgets::createLabelText("TIME:"), 2, 0);
    m_btnWaitTimeVal = new QPushButton("1.0s"); m_btnWaitTimeVal->setFixedHeight(kToolButtonHeightPx); m_btnWaitTimeVal->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    connect(m_btnWaitTimeVal, &QPushButton::clicked, this, &ProgramEditorPanel::onEditWaitTimeClicked);
    l->addWidget(m_btnWaitTimeVal, 2, 1);
    return p;
}

QWidget* ProgramEditorPanel::createEditIO() {
    QWidget* p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QHBoxLayout* l = new QHBoxLayout(p);
    l->setAlignment(Qt::AlignTop);
    l->addWidget(HexaWidgets::createLabelText("CONDITION:"));
    m_btnIoConditionVal = new QPushButton("---");
    m_btnIoConditionVal->setFixedHeight(kToolButtonHeightPx);
    m_btnIoConditionVal->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    m_btnIoConditionVal->setToolTip(QStringLiteral(
        "Edit the IO port and level: the DI condition of a WAIT DI, or the DO output of a SET DO."));
    connect(m_btnIoConditionVal, &QPushButton::clicked, this, &ProgramEditorPanel::onEditConditionClicked);
    l->addWidget(m_btnIoConditionVal);
    return p;
}

QWidget* ProgramEditorPanel::createEditGotoLabel() {
    QWidget* p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QGridLayout* l = new QGridLayout(p); l->setContentsMargins(0, 0, 0, 0);
    l->setAlignment(Qt::AlignTop);
    l->addWidget(HexaWidgets::createLabelText("LABEL ID:"), 0, 0);
    m_btnLabelIdVal = new QPushButton("0"); m_btnLabelIdVal->setFixedHeight(kToolButtonHeightPx); m_btnLabelIdVal->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    connect(m_btnLabelIdVal, &QPushButton::clicked, this, &ProgramEditorPanel::onEditLabelIdClicked);
    l->addWidget(m_btnLabelIdVal, 0, 1);
    return p;
}

QWidget* ProgramEditorPanel::createEditComment() {
    QWidget* p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QGridLayout* l = new QGridLayout(p); l->setContentsMargins(0, 0, 0, 0);
    l->setAlignment(Qt::AlignTop);
    l->addWidget(HexaWidgets::createLabelText("TEXT:"), 0, 0);
    m_btnCommentVal = new QPushButton("COMMENT");
    m_btnCommentVal->setFixedHeight(kToolButtonHeightPx);
    m_btnCommentVal->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    m_btnCommentVal->setToolTip(QStringLiteral("Edit the comment text."));
    connect(m_btnCommentVal, &QPushButton::clicked, this, &ProgramEditorPanel::onEditCommentClicked);
    l->addWidget(m_btnCommentVal, 0, 1);
    l->setColumnStretch(1, 1);
    return p;
}

QWidget* ProgramEditorPanel::createEditEmpty() {
    QWidget* p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QVBoxLayout* l = new QVBoxLayout(p); l->setAlignment(Qt::AlignCenter);
    QLabel* lbl = new QLabel("NO PROPERTIES"); lbl->setStyleSheet(Hexa::Styles::LabelHeaderSimple);
    l->addWidget(lbl);
    return p;
}

QWidget* ProgramEditorPanel::createFullFilePage() {
    // Controller storage is the ONLY program library (boss directive 2026-07-06): the pendant keeps
    // no local program files, so the page hosts the controller file list directly (no tab chrome).
    // The controller resolves the directory itself (programs_dir in hexacore_runtime_config.json).
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(5, 5, 5, 5); layout->setSpacing(10);

    QLabel* lblRemote = new QLabel("CONTROLLER STORAGE");
    lblRemote->setStyleSheet(Hexa::Styles::LabelHeaderSimple);
    lblRemote->setAlignment(Qt::AlignCenter);
    layout->addWidget(lblRemote);

    m_fileListRemote = new QListView();
    m_remoteFileModel = new QStringListModel(this);
    m_fileListRemote->setModel(m_remoteFileModel);
    m_fileListRemote->setStyleSheet(Hexa::Styles::ListView);
    layout->addWidget(m_fileListRemote, 1);

    QGridLayout* remoteBtns = new QGridLayout();
    remoteBtns->setSpacing(5);
    // No REFRESH button (boss 2026-07-07): the list refreshes itself - on entering the FILE page
    // (onNavClicked) and after every save/delete (HexaBackend re-requests the list once the
    // operation's response arrives).

    // Member (not a local): the guided demo highlights the LOAD control to show the
    // load-a-program workflow (explicit accessor, never findChild).
    m_btnRemoteLoad = new QPushButton("LOAD");
    QPushButton* btnLoadRemote = m_btnRemoteLoad;
    btnLoadRemote->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    btnLoadRemote->setFixedHeight(kToolButtonHeightPx);
    connect(btnLoadRemote, &QPushButton::clicked, this, [this]() {
        const QModelIndex idx = m_fileListRemote->currentIndex();
        if (!idx.isValid()) return;
        emit remoteLoadRequested(idx.data().toString());
    });

    QPushButton* btnSaveRemote = new QPushButton("SAVE");
    btnSaveRemote->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    btnSaveRemote->setFixedHeight(kToolButtonHeightPx);
    connect(btnSaveRemote, &QPushButton::clicked, this, [this]() {
        bool ok;
        QString name = CyberKeyboard::getString(this, "program.json", "ENTER FILE NAME", &ok);
        if (!ok || name.isEmpty()) return;
        emit remoteSaveRequested(name, m_builder->program());
    });

    QPushButton* btnDeleteRemote = new QPushButton("DELETE");
    btnDeleteRemote->setStyleSheet(Hexa::Styles::ButtonDangerNormal);
    btnDeleteRemote->setFixedHeight(kToolButtonHeightPx);
    connect(btnDeleteRemote, &QPushButton::clicked, this, [this]() {
        const QModelIndex idx = m_fileListRemote->currentIndex();
        if (!idx.isValid()) return;
        const QString fileName = idx.data().toString();
        auto reply = QMessageBox::question(this, "Confirm Delete",
                                           "Delete remote file " + fileName + "?", QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
        emit remoteDeleteRequested(fileName);
    });

    // Two-row grid (buttons in one row clipped their labels at the panel's working width - boss
    // live review): LOAD is the primary action and takes the full first row.
    remoteBtns->addWidget(btnLoadRemote, 0, 0, 1, 2);
    remoteBtns->addWidget(btnSaveRemote, 1, 0);
    remoteBtns->addWidget(btnDeleteRemote, 1, 1);
    layout->addLayout(remoteBtns);
    return page;
}

QWidget* ProgramEditorPanel::createGuiSettingsPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10); layout->setSpacing(15); layout->setAlignment(Qt::AlignTop);
    layout->addWidget(HexaWidgets::createLabelSectionTitle("INTERFACE SETTINGS"));
    layout->addWidget(HexaWidgets::createSeparatorH());

    // Each toggle forwards its state straight to the matching intent signal; the shell routes them
    // to the viewport (no state is kept here - the viewport owns the visual flags).
    QHBoxLayout* rowToolbar = new QHBoxLayout();
    rowToolbar->addWidget(HexaWidgets::createLabelText("VIEW CONTROL TOOLBAR")); rowToolbar->addStretch();
    HexaToggle* toggleToolbar = new HexaToggle(); toggleToolbar->setChecked(true);
    connect(toggleToolbar, &QAbstractButton::toggled, this, &ProgramEditorPanel::viewToolbarToggled);
    rowToolbar->addWidget(toggleToolbar);
    layout->addLayout(rowToolbar);

    QHBoxLayout* rowTraj = new QHBoxLayout();
    rowTraj->addWidget(HexaWidgets::createLabelText("SHOW TRAJECTORY")); rowTraj->addStretch();
    HexaToggle* toggleTraj = new HexaToggle(); toggleTraj->setChecked(true);
    connect(toggleTraj, &QAbstractButton::toggled, this, &ProgramEditorPanel::viewTrajectoryToggled);
    rowTraj->addWidget(toggleTraj);
    layout->addLayout(rowTraj);

    // Approach/departure transfer moves: shown by default (steel-coloured layer); the toggle hides
    // them entirely. Default matches ViewportPanel::m_isApproachVisible.
    QHBoxLayout* rowApproach = new QHBoxLayout();
    rowApproach->addWidget(HexaWidgets::createLabelText("SHOW APPROACH")); rowApproach->addStretch();
    HexaToggle* toggleApproach = new HexaToggle(); toggleApproach->setChecked(true);
    connect(toggleApproach, &QAbstractButton::toggled, this, &ProgramEditorPanel::approachVisibleToggled);
    rowApproach->addWidget(toggleApproach);
    layout->addLayout(rowApproach);

    // Physical-robot ghost: shown by default (it only appears when the physical arm diverges from
    // the commanded pose). Default matches ViewportPanel::m_isGhostVisible.
    QHBoxLayout* rowGhost = new QHBoxLayout();
    rowGhost->addWidget(HexaWidgets::createLabelText("SHOW GHOST")); rowGhost->addStretch();
    HexaToggle* toggleGhost = new HexaToggle(); toggleGhost->setChecked(true);
    connect(toggleGhost, &QAbstractButton::toggled, this, &ProgramEditorPanel::ghostVisibleToggled);
    rowGhost->addWidget(toggleGhost);
    layout->addLayout(rowGhost);

    // Flange TCP marker (teal dot + XYZ triad). Default matches ViewportPanel::m_isTcpFrameVisible.
    QHBoxLayout* rowTcpFrame = new QHBoxLayout();
    rowTcpFrame->addWidget(HexaWidgets::createLabelText("SHOW TCP FRAME")); rowTcpFrame->addStretch();
    HexaToggle* toggleTcpFrame = new HexaToggle(); toggleTcpFrame->setChecked(true);
    connect(toggleTcpFrame, &QAbstractButton::toggled, this, &ProgramEditorPanel::tcpFrameVisibleToggled);
    rowTcpFrame->addWidget(toggleTcpFrame);
    layout->addLayout(rowTcpFrame);

    layout->addWidget(HexaWidgets::createSeparatorH());
    // Full-screen: the app launches full-screen (F11 also toggles). This on-screen control works
    // BOTH ways - the 10-inch panel has no keyboard, so after leaving full-screen the operator
    // needs a way back (the previous exit-only button stranded the window in normal mode - boss
    // live review). The shell owns the window: the panel emits the intent and the shell feeds the
    // resulting window state back through setFullScreenActive, so the label always names the NEXT
    // action (boss request; a static "TOGGLE" label made the operator guess).
    m_btnFullScreen = HexaWidgets::createButtonStd("ENTER FULL SCREEN", this, 0, kToolButtonHeightPx);
    m_btnFullScreen->setToolTip(QStringLiteral("Switch to full-screen (F11 does the same)"));
    connect(m_btnFullScreen, &QPushButton::clicked, this, &ProgramEditorPanel::fullScreenToggleRequested);
    layout->addWidget(m_btnFullScreen);

    layout->addStretch();
    return page;
}

void ProgramEditorPanel::onSelectionChanged(const QModelIndex& current, const QModelIndex& previous) {
    Q_UNUSED(previous);
    if (!current.isValid()) { m_editStack->setCurrentIndex(EditEmpty); return; }
    const CommandType type = current.data(ProgramEditorModel::TypeRole).value<CommandType>();
    const QVariantMap params = current.data(ProgramEditorModel::ParamsRole).toMap();
    if (type == CommandType::Motion) {
        m_editStack->setCurrentIndex(EditMotion);
        m_btnVelocityVal->setText(QString::number(params.value(ProgramBuilder::kSpeed, 100).toInt()) + "%");
        const QString zone = params.value(ProgramBuilder::kZone, "FINE").toString();
        m_comboZone->blockSignals(true);
        const int zoneIndex = m_comboZone->findText(zone);
        m_comboZone->setCurrentIndex(zoneIndex >= 0 ? zoneIndex : 0);
        m_comboZone->blockSignals(false);
        const QString motionCode = current.data(ProgramEditorModel::CodeRole).toString();
        const bool cartesianDominant = ProgramBuilder::isCartesianDominant(motionCode);
        updateTcpFields(params.value(ProgramBuilder::kTcp).value<QVector<double>>(), cartesianDominant);
        if (m_lblViaVal) {
            const bool isCirc = ProgramBuilder::isCircCode(motionCode);
            m_lblViaVal->setVisible(isCirc);
            if (isCirc) {
                const QVector<double> via = params.value(ProgramBuilder::kTcpVia).value<QVector<double>>();
                m_lblViaVal->setText(via.size() >= 3
                    ? QStringLiteral("VIA: %1, %2, %3 mm")
                          .arg(via[0], 0, 'f', 2).arg(via[1], 0, 'f', 2).arg(via[2], 0, 'f', 2)
                    : QStringLiteral("VIA: not taught (use TEACH VIA)"));
            }
        }
    } else if (type == CommandType::Logic) {
        const QString code = current.data(ProgramEditorModel::CodeRole).toString();
        if (ProgramBuilder::isLogicBlockCode(code)) {
            m_editStack->setCurrentIndex(EditLogic);
            QString subtype = params.value(ProgramBuilder::kSubtype, code).toString();
            if (subtype.isEmpty()) subtype = code;
            m_comboLogicType->blockSignals(true);
            const int logicIndex = m_comboLogicType->findText(subtype);
            if (logicIndex >= 0) m_comboLogicType->setCurrentIndex(logicIndex);
            else m_comboLogicType->setCurrentIndex(code == "WAIT" ? 3 : 0);
            m_comboLogicType->blockSignals(false);

            const bool isIf = (subtype == "IF" || code == "IF");
            m_btnConditionVal->setVisible(isIf);
            if (isIf) m_btnConditionVal->setText(params.value(ProgramBuilder::kCondition, "---").toString());

            const bool isWait = (subtype == "WAIT" || code == "WAIT");
            m_btnWaitTimeVal->setVisible(isWait);
            if (isWait) m_btnWaitTimeVal->setText(QString::number(params.value(ProgramBuilder::kTime, 1.0).toDouble(), 'f', 2) + "s");
        } else if (ProgramBuilder::isLabelCode(code)) {
            m_editStack->setCurrentIndex(EditGotoLabel);
            m_btnLabelIdVal->setText(QString::number(params.value(ProgramBuilder::kLabelId, 0).toInt()));
        } else {
            m_editStack->setCurrentIndex(EditEmpty);
        }
    } else if (type == CommandType::IO) {
        // IO steps share one editable button: WAIT DI shows its DI condition (port + trigger level),
        // SET DO shows its output assignment (port + level); both are stored under kCondition.
        m_editStack->setCurrentIndex(EditIo);
        if (m_btnIoConditionVal) {
            m_btnIoConditionVal->setText(params.value(ProgramBuilder::kCondition, "---").toString());
        }
    } else if (type == CommandType::Comment) {
        m_editStack->setCurrentIndex(EditComment);
        if (m_btnCommentVal) {
            m_btnCommentVal->setText(current.data(ProgramEditorModel::NameRole).toString());
        }
    } else {
        m_editStack->setCurrentIndex(EditEmpty);
    }
    refreshEditAffordances(); // move-up/down bounds depend on the current selection
}

void ProgramEditorPanel::onEditCommentClicked() {
    const int row = currentRow();
    if (row < 0) {
        return;
    }
    const QString currentText =
        m_model->index(row, 0).data(ProgramEditorModel::NameRole).toString();
    bool ok = false;
    const QString text = CyberKeyboard::getString(this, currentText, "ENTER COMMENT TEXT", &ok);
    if (!ok) {
        return;
    }
    const auto result = m_builder->setCommentText(row, text);
    if (result.isError()) {
        QMessageBox::warning(this, "Edit failed",
                             "Could not edit comment: " + toString(result.error()) + ".");
        return;
    }
    m_btnCommentVal->setText(
        m_model->index(row, 0).data(ProgramEditorModel::NameRole).toString());
}

void ProgramEditorPanel::onInsertCmdClicked() {
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    const QString cmdId = btn->property(kCmdIdProperty).toString();
    const int after = currentRow();
    if (cmdId == QLatin1String("WAIT_TIME")) {
        auto r = m_builder->addWait(1.0, after);
        if (r.isSuccess()) selectRow(r.value());
    } else if (cmdId == QLatin1String("COMMENT")) {
        selectRow(m_builder->addComment(QStringLiteral("COMMENT"), after)); // cannot fail -> plain index
    } else if (cmdId == QLatin1String("LABEL") || cmdId == QLatin1String("GOTO")) {
        // Flow control: LABEL marks a jump target; GOTO jumps to the LABEL with the same id. Both take
        // a non-negative label id. A GOTO to a not-yet-defined label is allowed here and flagged by the
        // RUN gate (validate) until the matching LABEL exists.
        const bool isLabel = (cmdId == QLatin1String("LABEL"));
        bool ok = false;
        const QString title = isLabel ? QStringLiteral("ENTER LABEL ID")
                                      : QStringLiteral("ENTER TARGET LABEL ID");
        const QString text = CyberKeyboard::getString(this, QStringLiteral("0"), title, &ok);
        if (!ok) return;   // cancelled: stay on the CMD tab, insert nothing
        bool isNum = false;
        const int labelId = text.toInt(&isNum);
        if (!isNum || labelId < 0) {
            QMessageBox::warning(this, QStringLiteral("Invalid label id"),
                                 QStringLiteral("Label id must be a non-negative integer."));
            return;
        }
        auto r = isLabel ? m_builder->addLabel(labelId, after) : m_builder->addGoto(labelId, after);
        if (r.isSuccess()) selectRow(r.value());
    } else if (cmdId == QLatin1String("WAIT_DI")) {
        // WAIT DI blocks until DI[port] == triggerState (0 = infinite timeout, per the adopted policy).
        int port = ProgramBuilder::kMinDigitalInputPort;
        bool state = true;
        if (!promptIoPortState(QStringLiteral("ENTER DI PORT (1-32)"),
                               QStringLiteral("TRIGGER STATE (1=HIGH, 0=LOW)"),
                               port, state, port, state)) return;
        auto r = m_builder->addWaitDI(port, state, after);
        if (r.isError()) {
            QMessageBox::warning(this, QStringLiteral("Invalid WAIT DI"),
                                 QStringLiteral("Could not add WAIT DI: %1.").arg(toString(r.error())));
            return;
        }
        selectRow(r.value());
    } else if (cmdId == QLatin1String("SET_DO")) {
        // SET DO drives digital output DO[port] to the given level; the controller actuates it
        // through the HAL and faults the program if the active backend refuses the write.
        int port = ProgramBuilder::kMinDigitalInputPort;
        bool state = true;
        if (!promptIoPortState(QStringLiteral("ENTER DO PORT (1-32)"),
                               QStringLiteral("OUTPUT STATE (1=HIGH, 0=LOW)"),
                               port, state, port, state)) return;
        auto r = m_builder->addSetDo(port, state, after);
        if (r.isError()) {
            QMessageBox::warning(this, QStringLiteral("Invalid SET DO"),
                                 QStringLiteral("Could not add SET DO: %1.").arg(toString(r.error())));
            return;
        }
        selectRow(r.value());
    } else if (cmdId == QLatin1String("IF_ELSE")) {
        // IF (flat conditional jump): branch to the target LABEL on a DI level or on a register
        // compare (P4). The source is chosen first; each source then has its own prompt flow.
        bool ok = false;
        const QString sourceText = CyberKeyboard::getString(
            this, QStringLiteral("0"), QStringLiteral("CONDITION SOURCE (0=DI, 1=REGISTER)"), &ok);
        if (!ok) return;
        const bool isRegister = (sourceText.trimmed() == QLatin1String("1"));

        int port = ProgramBuilder::kMinDigitalInputPort;
        bool state = true;
        int reg = ProgramBuilder::kMinRegisterIndex;
        QString op = QStringLiteral("GT");
        int operand = 0;
        if (isRegister) {
            if (!promptRegisterCondition(reg, op, operand, reg, op, operand)) return;
        } else {
            if (!promptIoPortState(QStringLiteral("ENTER DI PORT (1-32)"),
                                   QStringLiteral("TRIGGER STATE (1=HIGH, 0=LOW)"),
                                   port, state, port, state)) return;
        }
        const QString labelText =
            CyberKeyboard::getString(this, QStringLiteral("0"), QStringLiteral("GOTO TARGET LABEL ID"), &ok);
        if (!ok) return;
        bool isNum = false;
        const int target = labelText.toInt(&isNum);
        if (!isNum || target < 0) {
            QMessageBox::warning(this, QStringLiteral("Invalid label id"),
                                 QStringLiteral("Target label id must be a non-negative integer."));
            return;
        }
        auto r = isRegister ? m_builder->addConditionalJumpOnRegister(reg, op, operand, target, after)
                            : m_builder->addConditionalJump(port, state, target, after);
        if (r.isError()) {
            QMessageBox::warning(this, QStringLiteral("Invalid IF"),
                                 QStringLiteral("Could not add IF: %1.").arg(toString(r.error())));
            return;
        }
        selectRow(r.value());
    } else if (cmdId == QLatin1String("SET_VAR")) {
        // SET VAR writes a constant to a register: R[reg] = value (the counter-loop initializer).
        int reg = ProgramBuilder::kMinRegisterIndex;
        if (!promptRegisterIndex(reg, reg)) return;
        bool ok = false;
        const QString valueText = CyberKeyboard::getString(
            this, QStringLiteral("0"), QStringLiteral("VALUE (INTEGER)"), &ok);
        if (!ok) return;
        bool isNum = false;
        const int value = valueText.toInt(&isNum);
        if (!isNum) return;
        auto r = m_builder->addSetVar(reg, value, after);
        if (r.isError()) {
            QMessageBox::warning(this, QStringLiteral("Invalid SET VAR"),
                                 QStringLiteral("Could not add SET VAR: %1.").arg(toString(r.error())));
            return;
        }
        selectRow(r.value());
    } else if (cmdId == QLatin1String("INC_VAR") || cmdId == QLatin1String("DEC_VAR")) {
        const bool isInc = (cmdId == QLatin1String("INC_VAR"));
        int reg = ProgramBuilder::kMinRegisterIndex;
        if (!promptRegisterIndex(reg, reg)) return;
        auto r = isInc ? m_builder->addIncVar(reg, after) : m_builder->addDecVar(reg, after);
        if (r.isError()) {
            QMessageBox::warning(this, QStringLiteral("Invalid register step"),
                                 QStringLiteral("Could not add %1: %2.")
                                     .arg(isInc ? QStringLiteral("INC VAR") : QStringLiteral("DEC VAR"),
                                          toString(r.error())));
            return;
        }
        selectRow(r.value());
    } else if (cmdId == QLatin1String("BREAK")) {
        // BREAK stops the program immediately at this line (STOP-from-code; boss decision).
        auto r = m_builder->addBreak(after);
        if (r.isSuccess()) selectRow(r.value());
    }
    if (auto* runBtn = m_navGroup->button(NavRun)) {
        runBtn->click(); // return to the RUN view after inserting
    }
}

void ProgramEditorPanel::onDeleteClicked() {
    // No confirmation dialog by design: a deleted step is instantly recoverable via UNDO. (A deleted
    // file is not recoverable, which is why onDeleteFileClicked does confirm - the asymmetry is
    // intentional.)
    m_builder->remove(currentRow());
}

void ProgramEditorPanel::onCopyClicked() {
    m_builder->copy(currentRow());
}

void ProgramEditorPanel::onPasteClicked() {
    auto r = m_builder->paste(currentRow());
    if (r.isSuccess()) selectRow(r.value());
}

void ProgramEditorPanel::onEditVelocityClicked() {
    bool ok; const QString text = CyberKeyboard::getString(this, "100", "ENTER VELOCITY %", &ok);
    if (!ok) return;
    bool isNum = false; const int val = text.toInt(&isNum);
    auto r = m_builder->setSpeed(currentRow(), isNum ? val : -1);
    if (r.isError()) {
        QMessageBox::warning(this, "Invalid velocity", "Velocity must be an integer between 1 and 100 %.");
        return;
    }
    m_btnVelocityVal->setText(QString::number(val) + "%");
}

void ProgramEditorPanel::onEditWaitTimeClicked() {
    const int row = currentRow();
    if (row < 0) return;
    const double currentTime = m_builder->at(row).params.value(ProgramBuilder::kTime, 1.0).toDouble();
    bool ok;
    const QString text = CyberKeyboard::getString(this, QString::number(currentTime, 'f', 2), "ENTER WAIT TIME (seconds)", &ok);
    if (!ok) return;
    bool isNum = false; const double val = text.toDouble(&isNum);
    if (!isNum) return;
    if (m_builder->setWaitTime(row, val).isSuccess()) {
        m_btnWaitTimeVal->setText(QString::number(val, 'f', 2) + "s");
    }
}

void ProgramEditorPanel::onEditLabelIdClicked() {
    const int row = currentRow();
    if (row < 0) return;
    const int currentLabelId = m_builder->at(row).params.value(ProgramBuilder::kLabelId, 0).toInt();
    bool ok;
    const QString text = CyberKeyboard::getString(this, QString::number(currentLabelId), "ENTER LABEL ID", &ok);
    if (!ok) return;
    bool isNum = false; const int val = text.toInt(&isNum);
    if (!isNum) return;
    if (m_builder->setLabelId(row, val).isSuccess()) {
        m_btnLabelIdVal->setText(QString::number(val));
    }
}

void ProgramEditorPanel::onEditZoneChanged(const QString& zone) {
    m_builder->setZone(currentRow(), zone);
}

bool ProgramEditorPanel::promptRegisterIndex(int defaultReg, int& outReg) {
    bool ok = false;
    const QString text = CyberKeyboard::getString(this, QString::number(defaultReg),
                                                  QStringLiteral("ENTER REGISTER (0-15)"), &ok);
    if (!ok) return false;
    bool isNum = false;
    outReg = text.toInt(&isNum);
    return isNum;
}

bool ProgramEditorPanel::promptRegisterCondition(int defaultReg, const QString& defaultOp,
                                                 int defaultOperand,
                                                 int& outReg, QString& outOp, int& outOperand) {
    if (!promptRegisterIndex(defaultReg, outReg)) return false;
    // Operator entry is numeric to stay on the same CyberKeyboard flow as every other prompt.
    static const QStringList kOps = {QStringLiteral("EQ"), QStringLiteral("NE"),
                                     QStringLiteral("GT"), QStringLiteral("LT")};
    bool ok = false;
    const int defaultOpIndex = std::max(0, static_cast<int>(kOps.indexOf(defaultOp)));
    const QString opText = CyberKeyboard::getString(
        this, QString::number(defaultOpIndex),
        QStringLiteral("OPERATOR (0: =, 1: !=, 2: >, 3: <)"), &ok);
    if (!ok) return false;
    bool isNum = false;
    const int opIndex = opText.toInt(&isNum);
    if (!isNum || opIndex < 0 || opIndex >= kOps.size()) return false;
    outOp = kOps.at(opIndex);
    const QString operandText = CyberKeyboard::getString(
        this, QString::number(defaultOperand), QStringLiteral("COMPARE VALUE (INTEGER)"), &ok);
    if (!ok) return false;
    outOperand = operandText.toInt(&isNum);
    return isNum;
}

bool ProgramEditorPanel::promptIoPortState(const QString& portTitle, const QString& stateTitle,
                                           int defaultPort, bool defaultState,
                                           int& outPort, bool& outState) {
    // defaultPort/defaultState are taken by value, so passing the same variables as outPort/outState
    // (edit-in-place) is safe.
    bool ok = false;
    const QString portText =
        CyberKeyboard::getString(this, QString::number(defaultPort), portTitle, &ok);
    if (!ok) return false;
    bool isNum = false;
    outPort = portText.toInt(&isNum);
    if (!isNum) return false;
    const QString stateText =
        CyberKeyboard::getString(this, defaultState ? QStringLiteral("1") : QStringLiteral("0"),
                                 stateTitle, &ok);
    if (!ok) return false;
    const QString s = stateText.trimmed();
    outState = (s == QLatin1String("1") || s.compare(QLatin1String("HIGH"), Qt::CaseInsensitive) == 0);
    return true;
}

void ProgramEditorPanel::onEditConditionClicked() {
    const int row = currentRow();
    if (row < 0) return;
    const ProgramCommand& cmd = m_builder->at(row);
    const bool isSetDo = (cmd.code == QLatin1String("SET DO"));
    if (cmd.code != QLatin1String("WAIT DI") && cmd.code != QLatin1String("IF") && !isSetDo) {
        return; // IO editing applies only to WAIT DI / IF conditions and SET DO output assignments
    }

    // A register-sourced IF (P4) edits its register condition; the source itself never flips
    // silently (delete and re-insert the step to change DI <-> REG).
    if (cmd.code == QLatin1String("IF") &&
        cmd.params.value(ProgramBuilder::kCondSource).toString() == QLatin1String("REG")) {
        int reg = cmd.params.value(ProgramBuilder::kReg, ProgramBuilder::kMinRegisterIndex).toInt();
        QString op = cmd.params.value(ProgramBuilder::kCompareOp, QStringLiteral("GT")).toString();
        int operand = cmd.params.value(ProgramBuilder::kOperand, 0).toInt();
        if (!promptRegisterCondition(reg, op, operand, reg, op, operand)) return;
        auto r = m_builder->setRegisterCondition(row, reg, op, operand);
        if (r.isError()) {
            QMessageBox::warning(this, QStringLiteral("Invalid condition"),
                                 QStringLiteral("Could not set condition: %1.").arg(toString(r.error())));
            return;
        }
        const QString newLabel = m_builder->at(row).params.value(ProgramBuilder::kCondition).toString();
        if (m_btnConditionVal) m_btnConditionVal->setText(newLabel);
        if (m_btnIoConditionVal) m_btnIoConditionVal->setText(newLabel);
        return;
    }

    const int curPort = cmd.params.value(ProgramBuilder::kPort, ProgramBuilder::kMinDigitalInputPort).toInt();
    const bool curState = cmd.params.value(ProgramBuilder::kState, true).toBool();
    int port = curPort;
    bool state = curState;
    const bool accepted = isSetDo
        ? promptIoPortState(QStringLiteral("ENTER DO PORT (1-32)"),
                            QStringLiteral("OUTPUT STATE (1=HIGH, 0=LOW)"),
                            curPort, curState, port, state)
        : promptIoPortState(QStringLiteral("ENTER DI PORT (1-32)"),
                            QStringLiteral("TRIGGER STATE (1=HIGH, 0=LOW)"),
                            curPort, curState, port, state);
    if (!accepted) return;
    auto r = m_builder->setCondition(row, port, state);
    if (r.isError()) {
        QMessageBox::warning(this, QStringLiteral("Invalid condition"),
                             QStringLiteral("Could not set condition: %1.").arg(toString(r.error())));
        return;
    }
    const QString newLabel = m_builder->at(row).params.value(ProgramBuilder::kCondition).toString();
    if (m_btnConditionVal) m_btnConditionVal->setText(newLabel);
    if (m_btnIoConditionVal) m_btnIoConditionVal->setText(newLabel);
}

void ProgramEditorPanel::updateTcpFields(const QVector<double>& tcpPose, bool editable) {
    for (int i = 0; i < 6; ++i) {
        if (!m_tcpFields[i]) continue;
        const double value = (i < tcpPose.size()) ? tcpPose[i] : 0.0;
        m_tcpFields[i]->setText(QString::number(value, 'f', 2));
        m_tcpFields[i]->setEnabled(editable);
        m_tcpFields[i]->setToolTip(editable
            ? QStringLiteral("Edit %1 (%2)").arg(QString::fromLatin1(kTcpFieldLabels[i]), QString::fromLatin1(kTcpFieldUnits[i]))
            : QStringLiteral("Read-only: a PTP point executes joint angles, so editing its TCP pose "
                             "would not move the robot. Use TOUCH UP to re-teach this point, or set it "
                             "to LIN to edit its TCP pose directly."));
    }
}

void ProgramEditorPanel::editTcpComponent(int axisIndex) {
    if (axisIndex < 0 || axisIndex >= 6) return;
    const int row = currentRow();
    if (row < 0) return;
    const QString motionCode = m_builder->at(row).code;
    if (!ProgramBuilder::isCartesianDominant(motionCode)) return; // cartesian-dominant only

    QVector<double> pose = m_builder->at(row).params.value(ProgramBuilder::kTcp).value<QVector<double>>();
    if (pose.size() < 6) pose.resize(6);
    bool ok = false;
    const QString prompt = QStringLiteral("ENTER %1 (%2)").arg(QString::fromLatin1(kTcpFieldLabels[axisIndex]), QString::fromLatin1(kTcpFieldUnits[axisIndex]));
    const QString text = CyberKeyboard::getString(this, QString::number(pose[axisIndex], 'f', 2), prompt, &ok);
    if (!ok) return;
    bool isNum = false; const double value = text.toDouble(&isNum);
    if (!isNum) return;
    if (m_builder->setTcpComponent(row, axisIndex, value).isSuccess()) {
        pose[axisIndex] = value;
        updateTcpFields(pose, true);
    }
}

void ProgramEditorPanel::setFullScreenActive(bool active) {
    if (!m_btnFullScreen) {
        return;
    }
    m_btnFullScreen->setText(active ? QStringLiteral("EXIT FULL SCREEN")
                                    : QStringLiteral("ENTER FULL SCREEN"));
    m_btnFullScreen->setToolTip(active
        ? QStringLiteral("Return to a normal window (F11 does the same)")
        : QStringLiteral("Switch to full-screen (F11 does the same)"));
}

void ProgramEditorPanel::onUndoClicked() {
    // A failed undo (empty stack) is a benign no-op; the button is normally disabled in that state.
    m_builder->undo();
}

void ProgramEditorPanel::onRedoClicked() {
    m_builder->redo();
}

void ProgramEditorPanel::onMoveUpClicked() {
    const int row = currentRow();
    if (row <= 0) {
        return; // top row or nothing selected: nothing to move up
    }
    if (m_builder->move(row, row - 1).isSuccess()) {
        selectRow(row - 1);
    }
}

void ProgramEditorPanel::onMoveDownClicked() {
    const int row = currentRow();
    if (row < 0 || row >= m_builder->stepCount() - 1) {
        return; // bottom row or nothing selected: nothing to move down
    }
    if (m_builder->move(row, row + 1).isSuccess()) {
        selectRow(row + 1);
    }
}

void ProgramEditorPanel::refreshEditAffordances() {
    if (m_btnUndo) {
        m_btnUndo->setEnabled(m_builder->canUndo());
    }
    if (m_btnRedo) {
        m_btnRedo->setEnabled(m_builder->canRedo());
    }
    const int row = currentRow();
    const int count = m_builder->stepCount();
    if (m_btnMoveUp) {
        m_btnMoveUp->setEnabled(row > 0);
    }
    if (m_btnMoveDown) {
        m_btnMoveDown->setEnabled(row >= 0 && row < count - 1);
    }
    if (m_emptyHint) {
        m_emptyHint->setVisible(count == 0);
    }
    refreshTeachViaButton();
}

void ProgramEditorPanel::refreshTeachViaButton() {
    if (!m_btnTeachVia) {
        return;
    }
    const int row = currentRow();
    const bool circSelected = (row >= 0 && row < m_builder->stepCount())
        && m_builder->at(row).type == CommandType::Motion
        && ProgramBuilder::isCircCode(m_builder->at(row).code);
    m_btnTeachVia->setEnabled(m_canTeach && circSelected);
    if (!circSelected) {
        m_btnTeachVia->setToolTip(QStringLiteral("Select a CIRC step to record its via point"));
    } else if (!m_canTeach) {
        m_btnTeachVia->setToolTip(QStringLiteral("Switch to REAL mode (robot idle) to record the via point"));
    } else {
        m_btnTeachVia->setToolTip(QStringLiteral("Record the current position as the arc's via point"));
    }
}

int ProgramEditorPanel::toolsHeightForTab(int navId) const {
    // Each tab's tool area gets exactly the height its rows need, so no row overlaps another and the
    // program list keeps the rest of the space. TEACH is the tallest (six rows).
    switch (navId) {
        case NavRun:   return 90;   // one row (RUN / PAUSE / STOP)
        case NavTeach: return 300;  // motion+SET PP, TEACH+TOUCH UP, TEACH VIA, BLEND, COPY/PASTE/DEL, MOVE ▲/▼
        case NavCmd:   return 300;  // six rows of two command buttons (6 * 44 + spacing + margin)
        case NavEdit:  return 210;  // properties (velocity/zone + TCP grid, or logic/goto/label)
        default:       return 160;
    }
}

void ProgramEditorPanel::onNavClicked(int id) {
    if (id == NavFile) {
        m_mainStack->setCurrentIndex(PageFile);
        // The controller list is refreshed automatically on entering the page (boss 2026-07-07:
        // the operator must not need a manual REFRESH press to see the library).
        emit remoteListRequested();
    } else if (id == NavUi) {
        m_mainStack->setCurrentIndex(PageGui);
    } else if (id == NavGuide) {
        // Reachable only after installGuidePage: the chip stays hidden until the page exists.
        m_mainStack->setCurrentIndex(m_guidePageIndex);
    } else {
        m_mainStack->setCurrentIndex(PageProgram);
        m_toolsStack->setCurrentIndex(id); // tools pages are added in Run..Edit order, matching NavId
        // Keep the tools hidden if the operator collapsed them; otherwise size them to this tab.
        if (!m_toolsCollapsed) {
            m_toolsStack->setFixedHeight(toolsHeightForTab(id));
        }
        if (id == NavEdit) {
            onSelectionChanged(m_programView->currentIndex(), QModelIndex());
        }
    }
}

// --- Incoming backend feedback (host-wired) --------------------------------------------------------

void ProgramEditorPanel::loadProgram(const QVector<ProgramCommand>& program) {
    // External/controller load: replace content WITHOUT re-uploading. Switch to RUN view (matches the
    // old programLoaded handler).
    m_builder->load(program);
    if (auto* runBtn = m_navGroup->button(NavRun)) {
        runBtn->click();
    }
}

void ProgramEditorPanel::setProgram(const QVector<ProgramCommand>& program) {
    m_builder->load(program); // initial cache; no upload
}

void ProgramEditorPanel::confirmProgramSaved(const QString& filename) {
    // Controller-confirmed save (the controller is the ONLY program store): adopt the file identity
    // and clear the dirty marker. Never cleared on the save REQUEST — a failed save must stay UNSAVED.
    m_loadedProgramName = filename;
    m_builder->markSaved();
    refreshProgramTitle();
}

void ProgramEditorPanel::setRemoteFileList(const QStringList& files) {
    if (m_remoteFileModel) m_remoteFileModel->setStringList(files);
}

} // namespace hexa
// --- END OF FILE: HexaStudio/program_editor/ProgramEditorPanel.cpp ---
