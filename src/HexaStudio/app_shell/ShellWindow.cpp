// --- START OF FILE: HexaStudio/app_shell/ShellWindow.cpp ---
#include "ShellWindow.h"
#include "BackendClient.h"

#include "StatusBarPanel.h"
#include "ConfirmDialog.h"
#include "JogPanel.h"
#include "ProgramEditorPanel.h"
#include "SettingsPanel.h"
#include "DiagnosticsPanel.h"
#include "HalPanel.h"
#include "ViewportPanel.h"
#include "GuideCallout.h"
#include "GuidePanel.h"
#include "GuideRunner.h"

#include "HexaTheme.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>

namespace hexa {

namespace {
constexpr int kDefaultWidthPx = 1280;
constexpr int kDefaultHeightPx = 860;
// Splitter proportions of the shipping MainWindow (left / centre / right).
constexpr int kLeftColumnPx = 300;
constexpr int kCenterColumnPx = 680;
constexpr int kRightColumnPx = 300;
// Diagnostics card anchor: under the top-bar status indicator (left side of the bar). The Y is
// derived from the actual bar geometry at show time, so it survives bar-height and DPI changes.
constexpr int kDiagnosticsAnchorXPx = 10;
constexpr int kDiagnosticsGapPx = 20;
// GO HOME program parameters (same values the shipping MainWindow used).
constexpr int kHomeSpeedPercent = 100;
// Guide callout docking: side margin, and the gap kept below the status bar / around the E-Stop.
constexpr int kGuideCalloutMarginPx = 10;
constexpr int kGuideCalloutGapPx = 14;
// Guide highlight frame: a bold violet border, inflated a few px so it sits AROUND the target,
// not on it. Static (no blink — GDE-REQ-0087).
constexpr int kGuideHighlightBorderPx = 5;
constexpr int kGuideHighlightInflatePx = 4;
} // namespace

ShellWindow::ShellWindow(const QString& appVersion, QWidget* parent)
    : QWidget(parent), m_appVersion(appVersion) {
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("background-color: %1;").arg(Hexa::Colors::Background));
    setupUi();
    registerGuideTargets();
    connectGuide();
    resize(kDefaultWidthPx, kDefaultHeightPx);

    // F11 toggles full-screen. The application launches full-screen by default (main_ng); the operator
    // can drop to a normal window (and back) without leaving the HMI. resize() above sets the windowed
    // geometry restored on exit from full-screen.
    auto* fullScreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(fullScreenShortcut, &QShortcut::activated, this, &ShellWindow::toggleFullScreen);
}

void ShellWindow::toggleFullScreen() {
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void ShellWindow::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    // Breathing room around the work area: the three columns read as instruments on the bench,
    // not as regions of one undifferentiated surface.
    mainLayout->setContentsMargins(10, 4, 10, 10);
    mainLayout->setSpacing(6);

    m_statusBar = new StatusBarPanel(this);
    m_statusBar->setAppVersion(m_appVersion);
    // Pin the top-bar's left group and E-STOP under the side columns, so the bar's section dividers
    // line up with the splitter boundaries of the panels below (the bar and the splitter share this
    // layout's left/right margins, so the same column constants align both).
    m_statusBar->setColumnGuides(kLeftColumnPx, kRightColumnPx);
    mainLayout->addWidget(m_statusBar);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setStyleSheet(Hexa::Styles::Splitter);
    m_splitter->setHandleWidth(10);   // the gap between cards doubles as a touch-draggable handle

    // Each work column sits on its own card (ID selector: a bare QFrame rule would leak onto every
    // descendant QFrame - separators, jog value boxes). NO explicit minimum width: the card's floor
    // is its content's honest layout minimum, so the splitter can never compress a column below
    // what its panel actually needs. The previous fixed 200px floor OVERRODE the content minimum
    // and let the jog row overflow with the keys colliding into the value cells (boss live review).
    auto wrapInCard = [this](QWidget* content) {
        QFrame* card = new QFrame(this);
        card->setObjectName(QStringLiteral("ShellCard"));
        card->setStyleSheet(QStringLiteral(
            "QFrame#ShellCard { background-color: %1; border: 1px solid %2; border-radius: 12px; }")
            .arg(Hexa::Colors::Surface, Hexa::Colors::Hairline));
        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(8, 8, 8, 8);
        cardLayout->addWidget(content);
        return card;
    };

    m_programEditor = new ProgramEditorPanel(m_splitter);
    m_leftCard = wrapInCard(m_programEditor);
    m_splitter->addWidget(m_leftCard);

    // Centre column: the viewport3d module (integrated), with a view-preset toolbar BELOW it (boss
    // request: the controls sit at the bottom of the scene, off the sightline to the robot). The
    // toolbar's visibility is driven by the program editor's viewToolbarToggled intent.
    QWidget* viewportColumn = new QWidget();
    QVBoxLayout* viewportLayout = new QVBoxLayout(viewportColumn);
    viewportLayout->setContentsMargins(0, 0, 0, 0);
    viewportLayout->setSpacing(4);

    m_viewToolbar = new QWidget(viewportColumn);
    QHBoxLayout* toolbarLayout = new QHBoxLayout(m_viewToolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(6);
    m_viewport = new ViewportPanel(viewportColumn);
    const struct {
        const char* label;
        void (ViewportPanel::*slot)();
        QPushButton** guideTargetStore;   // preset addressed by a guide scenario (nullptr = not a target)
    } kViewPresets[] = {
        {"TOP", &ViewportPanel::setViewTop, nullptr},
        {"FRONT", &ViewportPanel::setViewFront, nullptr},
        {"SIDE", &ViewportPanel::setViewSide, nullptr},
        {"ISO", &ViewportPanel::setViewIso, &m_btnViewIso},   // the guide's one framing interaction
        {"FIT", &ViewportPanel::setViewFitToScreen, nullptr},
    };
    for (const auto& preset : kViewPresets) {
        auto* button = new QPushButton(QString::fromLatin1(preset.label), m_viewToolbar);
        button->setFixedHeight(26);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { color: %1; font-family: '%2'; font-size: 11px; background: %3;"
            " border: 1px solid %4; border-radius: 3px; padding: 2px 10px; }"
            "QPushButton:pressed { background: %4; }")
            .arg(Hexa::Colors::TextMuted, Hexa::Fonts::familyUI(),
                 Hexa::Colors::Surface, Hexa::Colors::Border));
        connect(button, &QPushButton::clicked, m_viewport, preset.slot);
        if (preset.guideTargetStore) {
            *preset.guideTargetStore = button;
        }
        toolbarLayout->addWidget(button);
    }
    toolbarLayout->addStretch(1);

    // PRESENTATION (investor pitch, boss approved): collapse the HMI to the 3D scene + status bar.
    // The toggle is checkable and lives ON the scene toolbar, so the exit control can never
    // disappear together with the side panels it hides.
    auto* presentButton = new QPushButton(QStringLiteral("PRESENTATION"), m_viewToolbar);
    presentButton->setCheckable(true);
    presentButton->setFixedHeight(26);
    presentButton->setCursor(Qt::PointingHandCursor);
    presentButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-family: '%2'; font-size: 11px; background: %3;"
        " border: 1px solid %4; border-radius: 3px; padding: 2px 10px; }"
        "QPushButton:pressed { background: %4; }"
        "QPushButton:checked { color: #10151C; background: %5; border-color: %5; }")
        .arg(Hexa::Colors::TextMuted, Hexa::Fonts::familyUI(),
             Hexa::Colors::Surface, Hexa::Colors::Border, Hexa::Colors::Primary));
    connect(presentButton, &QPushButton::toggled, this, &ShellWindow::setPresentationMode);
    toolbarLayout->addWidget(presentButton);

    viewportLayout->addWidget(m_viewport, 1);
    viewportLayout->addWidget(m_viewToolbar, 0);
    m_splitter->addWidget(wrapInCard(viewportColumn));

    m_jogPanel = new JogPanel(m_splitter);
    m_rightCard = wrapInCard(m_jogPanel);
    m_splitter->addWidget(m_rightCard);

    m_splitter->setSizes({kLeftColumnPx, kCenterColumnPx, kRightColumnPx});
    // Window resizing is absorbed by the centre column only: the side panels hold their geometry,
    // so touch muscle memory on the jog keys and program tools does not move with window size.
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    // A drag must stop at each column's honest minimum, never snap it to zero: a collapsed column
    // on an operator HMI reads as a vanished instrument.
    m_splitter->setChildrenCollapsible(false);
    mainLayout->addWidget(m_splitter, 1);

    // Overlays: hidden until requested; they span (settings/HAL) or anchor to (diagnostics) the
    // window. Created last so they stack above the panels.
    m_settings = new SettingsPanel(this);
    m_settings->hide();
    m_hal = new HalPanel(this);
    m_hal->hide();
    m_diagnostics = new DiagnosticsPanel(this);
    m_diagnostics->hide();

    // Guided demo (guide module): the chooser page is injected into the program editor's stack
    // (GUIDE nav chip at the sidebar bottom); the engine and the floating callout live here —
    // cross-module logic belongs to the mediator. The callout raise()s itself on every step.
    m_guidePanel = new GuidePanel();
    m_programEditor->installGuidePage(m_guidePanel);
    m_guideRunner = new GuideRunner(this);
    m_guideCallout = new GuideCallout(this);
    m_guideCallout->hide();

    // Highlight frame: a mouse-transparent overlay drawn OVER the current target. It never touches
    // the target's own stylesheet, so it survives widgets that re-style themselves on status ticks
    // (the jog ENABLE button restyles ~20x/s — a stylesheet-appended border silently vanished on
    // it). WA_TransparentForMouseEvents lets both the virtual click and a real user click reach the
    // target underneath. None of the guide targets sit over the native viewport, so this small
    // overlay is not overdrawn (unlike a full-window overlay, which is why §6.3 forbids that).
    m_guideHighlight = new QFrame(this);
    m_guideHighlight->setObjectName(QStringLiteral("GuideHighlight"));
    m_guideHighlight->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_guideHighlight->setAttribute(Qt::WA_StyledBackground, true);
    m_guideHighlight->setStyleSheet(QStringLiteral(
        "QFrame#GuideHighlight { background: transparent; border: %1px solid %2; border-radius: 8px; }")
        .arg(kGuideHighlightBorderPx).arg(Hexa::Colors::Active));
    m_guideHighlight->hide();
}

// ---------------------------------------------------------------------------
// THE wiring. Every module <-> backend connection of the product, in one readable block.
// ---------------------------------------------------------------------------

void ShellWindow::connectBackend(BackendClient& backend) {
    Q_ASSERT(!m_backendConnected);   // one composition root, one wiring pass
    m_backendConnected = true;

    // ===== Feedback fan-out: backend publishes, the shell distributes ======================
    connect(&backend, &BackendClient::robotStateChanged, this,
            [this](const HmiRobotStatus& status) {
        m_lastStatus = status;
        m_statusBar->updateState(status.top, status.motion.isMoving, status.prog.isPaused);
        m_jogPanel->updateState(status.motion);
        m_programEditor->updateState(status.prog, status.motion);
        m_diagnostics->updateStatus(status);
        m_hal->onRobotStateChanged(status);
        // Order matters: the running gate must be current BEFORE the motion update advances the
        // executed-trajectory fade from the same status snapshot. isPaused rides along so a
        // RESUME does not wipe the executed trace (fresh-run detection).
        m_viewport->setProgramExecutionState(status.prog.isRunning, status.prog.isPaused);
        m_viewport->updateState(status.motion);
    });
    connect(&backend, &BackendClient::configReceived, this,
            [this](const HmiSystemConfig& config) {
        m_lastConfig = config;   // the shell owns name -> id mapping (see context lambdas below)
        m_statusBar->onConfigReceived(config);
        m_jogPanel->onConfigReceived(config);
        m_settings->setConfig(config);
        m_hal->setRealtimeBackend(config.realtimeBackend);   // controller echo -> selector sync
        // ONE source of truth for the robot model frame: the controller's own urdfPath +
        // root_transform arrive in the config snapshot; the viewport consumes exactly them, so the
        // visual robot, the KDL FK and the displayed trajectory share one base frame. The panel
        // re-parses only when the path actually changed (cheap on every config echo).
        if (!config.robotUrdfPath.isEmpty()) {
            m_viewport->setRobotModelConfig(config.robotUrdfPath,
                                            config.modelRootX, config.modelRootY, config.modelRootZ,
                                            config.modelRootRxDeg, config.modelRootRyDeg,
                                            config.modelRootRzDeg);
        }
    });
    connect(&backend, &BackendClient::trajectoryReceived,
            m_viewport, &ViewportPanel::updateTrajectoryPath);
    connect(&backend, &BackendClient::halConfigChanged,
            m_hal, &HalPanel::setHalConfigCurrent);
    connect(&backend, &BackendClient::programLoaded,
            m_programEditor, &ProgramEditorPanel::loadProgram);
    connect(&backend, &BackendClient::programSaved,
            m_programEditor, &ProgramEditorPanel::confirmProgramSaved);
    connect(&backend, &BackendClient::remoteFileListReceived,
            m_programEditor, &ProgramEditorPanel::setRemoteFileList);
    connect(&backend, &BackendClient::tcpSimulatorStateChanged,
            m_hal, &HalPanel::setTcpSimulatorRunning);

    // ===== Status bar intents ==============================================================
    connect(m_statusBar, &StatusBarPanel::modeChanged, &backend, &BackendClient::setMode);
    connect(m_statusBar, &StatusBarPanel::speedChanged, &backend, &BackendClient::setSpeedOverride);
    // E-Stop TOGGLE (shipping semantics): engaged -> release, released -> engage.
    connect(m_statusBar, &StatusBarPanel::eStopRequested, &backend, [this, &backend]() {
        backend.setEStop(!m_lastStatus.top.isEStop);
    });
    connect(m_statusBar, &StatusBarPanel::settingsRequested, this, [this]() {
        m_settings->resize(size());
        m_settings->show();
        m_settings->raise();
        syncViewportObscuring();
    });
    connect(m_statusBar, &StatusBarPanel::diagnosticsRequested, this, [this]() {
        if (m_diagnostics->isVisible()) {
            m_diagnostics->hide();
        } else {
            positionDiagnostics();
            m_diagnostics->show();
            m_diagnostics->raise();
        }
        syncViewportObscuring();
    });

    // ===== Program editor intents ==========================================================
    connect(m_programEditor, &ProgramEditorPanel::playRequested,
            &backend, &BackendClient::startProgram);
    connect(m_programEditor, &ProgramEditorPanel::pauseRequested,
            &backend, &BackendClient::pauseProgram);
    connect(m_programEditor, &ProgramEditorPanel::resumeRequested,
            &backend, &BackendClient::resumeProgram);
    connect(m_programEditor, &ProgramEditorPanel::stopRequested,
            &backend, &BackendClient::stopProgram);
    connect(m_programEditor, &ProgramEditorPanel::programChanged,
            &backend, &BackendClient::uploadProgram);
    connect(m_programEditor, &ProgramEditorPanel::remoteListRequested,
            &backend, &BackendClient::requestRemoteFileList);
    connect(m_programEditor, &ProgramEditorPanel::remoteLoadRequested,
            &backend, &BackendClient::loadRemoteProgramFile);
    connect(m_programEditor, &ProgramEditorPanel::remoteSaveRequested,
            &backend, &BackendClient::saveRemoteProgramFile);
    connect(m_programEditor, &ProgramEditorPanel::remoteDeleteRequested,
            &backend, &BackendClient::deleteRemoteProgramFile);
    // View toggles land in the integrated viewport3d module.
    connect(m_programEditor, &ProgramEditorPanel::viewToolbarToggled,
            m_viewToolbar, &QWidget::setVisible);
    connect(m_programEditor, &ProgramEditorPanel::viewTrajectoryToggled,
            m_viewport, &ViewportPanel::setTrajectoryVisible);
    connect(m_programEditor, &ProgramEditorPanel::approachVisibleToggled,
            m_viewport, &ViewportPanel::setApproachVisible);
    connect(m_programEditor, &ProgramEditorPanel::ghostVisibleToggled,
            m_viewport, &ViewportPanel::setGhostVisible);
    connect(m_programEditor, &ProgramEditorPanel::tcpFrameVisibleToggled,
            m_viewport, &ViewportPanel::setTcpFrameVisible);

    // Full-screen toggle from the UI-settings tab - BOTH directions: on the 10-inch panel there is
    // no keyboard, so after leaving full-screen the operator needs an on-screen way back (the old
    // exit-only button stranded the window in normal mode - boss live review). The shell owns the
    // window; the panel only emits the intent.
    connect(m_programEditor, &ProgramEditorPanel::fullScreenToggleRequested,
            this, &ShellWindow::toggleFullScreen);

    // ===== Jog panel intents ===============================================================
    connect(m_jogPanel, &JogPanel::jogRequested, &backend, &BackendClient::jogJointIncremental);
    connect(m_jogPanel, &JogPanel::jogEnableRequested, &backend, &BackendClient::setJogEnabled);
    connect(m_jogPanel, &JogPanel::coordSystemChanged, &backend, &BackendClient::setJogMode);
    // GO HOME: explicit confirmation (moves the robot), then a one-command PTP home program -
    // the same construction the shipping MainWindow used.
    connect(m_jogPanel, &JogPanel::goHomeRequested, &backend, [this, &backend]() {
        const bool confirmed = ConfirmDialog::confirm(
            this, QStringLiteral("SAFETY WARNING"),
            QStringLiteral("Move the robot to the home position?"),
            QStringLiteral("GO HOME"));
        if (!confirmed) return;
        QVector<ProgramCommand> program;
        ProgramCommand command(CommandType::Motion, QStringLiteral("PTP"),
                               QStringLiteral("GO Home"));
        command.params[QStringLiteral("Speed")] = kHomeSpeedPercent;
        command.params[QStringLiteral("Zone")] = QStringLiteral("FINE");
        command.params[QStringLiteral("Joints")] =
            QVariant::fromValue(QVector<double>{0.0, 0.0, 0.0, 0.0, 0.0, 0.0});
        program.append(command);
        backend.startProgram(program);
    });
    // Context signals carry user-facing NAMES; the shell maps them to ids against the last
    // received config (identical to the shipping MainWindow lambdas).
    connect(m_jogPanel, &JogPanel::jogContextChanged, &backend,
            [this, &backend](const QString& toolName, const QString& baseName) {
        backend.setJogContext(lookupToolId(toolName), lookupBaseId(baseName));
    });
    connect(m_jogPanel, &JogPanel::monitorContextChanged, &backend,
            [this, &backend](const QString& toolName, const QString& baseName) {
        const int baseId =
            (baseName == QLatin1String("JOINT")) ? 0 : lookupBaseId(baseName);
        backend.setMonitorContext(lookupToolId(toolName), baseId);
    });

    // ===== Settings overlay intents ========================================================
    connect(m_settings, &SettingsPanel::applyRequested, &backend, &BackendClient::applySettings);
    connect(m_settings, &SettingsPanel::closeRequested, this, [this]() {
        m_settings->hide();
        syncViewportObscuring();
    });
    connect(m_settings, &SettingsPanel::halOverlayRequested, this, [this]() {
        m_settings->hide();
        m_hal->resize(size());
        m_hal->show();
        m_hal->raise();
        syncViewportObscuring();
    });

    // ===== Diagnostics intents =============================================================
    connect(m_diagnostics, &DiagnosticsPanel::clearErrorRequested,
            &backend, &BackendClient::clearError);
    connect(m_diagnostics, &DiagnosticsPanel::eStopRequested, &backend, [this, &backend]() {
        backend.setEStop(!m_lastStatus.top.isEStop);   // toggle, same as the top bar
    });
    connect(m_diagnostics, &DiagnosticsPanel::closeRequested, this, [this]() {
        m_diagnostics->hide();
        syncViewportObscuring();
    });

    // ===== HAL runtime intents =============================================================
    // Backend selector: the shell owns the last received config, so it merges the selection and
    // forwards it over the proven applySettings path (persisted by the controller, applied live
    // for sim<->mks, echoed back into the selector via configReceived above).
    connect(m_hal, &HalPanel::realtimeBackendSelected, &backend,
            [this, &backend](const QString& selected) {
        m_lastConfig.realtimeBackend = selected;
        backend.applySettings(m_lastConfig);
    });
    connect(m_hal, &HalPanel::applyHalRequested, &backend, &BackendClient::applyHalConfig);
    connect(m_hal, &HalPanel::halConnectRequested, &backend, &BackendClient::connectHalEndpoint);
    connect(m_hal, &HalPanel::halDisconnectRequested,
            &backend, &BackendClient::disconnectHalEndpoint);
    connect(m_hal, &HalPanel::halJogRequested, &backend, &BackendClient::jogJointIncrementalHal);
    connect(m_hal, &HalPanel::homingRequested, &backend, &BackendClient::startHomingSequence);
    connect(m_hal, &HalPanel::setZeroRequested, &backend, &BackendClient::masterAxis);
    connect(m_hal, &HalPanel::setZeroAllRequested, &backend, &BackendClient::setZeroAll);
    connect(m_hal, &HalPanel::clearErrorsRequested, &backend, &BackendClient::clearError);
    connect(m_hal, &HalPanel::jogArmRequested, &backend, &BackendClient::setJogEnabled);
    // HAL E-STOP ALL ENGAGES (never toggles) - shipping semantics kept.
    connect(m_hal, &HalPanel::eStopRequested, &backend,
            [&backend]() { backend.setEStop(true); });
    connect(m_hal, &HalPanel::closeRequested, this, [this]() {
        m_hal->hide();
        syncViewportObscuring();
    });
    // The TCP HAL simulator is a host-process concern; at Phase C the shell launches/stops the
    // process. The bench consumes these two intents (see app_shell_bench_main).
}

// ---------------------------------------------------------------------------
// Guided demo: target registry + wiring (same one-readable-block rule as connectBackend)
// ---------------------------------------------------------------------------

void ShellWindow::registerGuideTargets() {
    // THE guide target registry: every widget a scenario may address, resolved through explicit
    // panel accessors (never findChild — GDE-REQ-0020). A target missing here is caught by the
    // runner's start() preflight as a typed error naming the step and the target.
    // Left panel (program editor) — the heart of the workflow.
    m_guideRunner->registerTarget(GuideTarget::NavRun, m_programEditor->navRunButton());
    m_guideRunner->registerTarget(GuideTarget::NavTeach, m_programEditor->navTeachButton());
    m_guideRunner->registerTarget(GuideTarget::NavFile, m_programEditor->navFileButton());
    m_guideRunner->registerTarget(GuideTarget::NavUi, m_programEditor->navUiButton());
    m_guideRunner->registerTarget(GuideTarget::ProgramList, m_programEditor->programListWidget());
    m_guideRunner->registerTarget(GuideTarget::RunButton, m_programEditor->runButton());
    m_guideRunner->registerTarget(GuideTarget::TeachButton, m_programEditor->teachButton());
    m_guideRunner->registerTarget(GuideTarget::ControllerFileList,
                                  m_programEditor->controllerFileListWidget());
    m_guideRunner->registerTarget(GuideTarget::ControllerLoad,
                                  m_programEditor->controllerLoadButton());
    // Right panel (jog) — how the robot is moved by hand.
    m_guideRunner->registerTarget(GuideTarget::JogEnable, m_jogPanel->enableJogButton());
    m_guideRunner->registerTarget(GuideTarget::JogKey, m_jogPanel->firstJogKey());
    m_guideRunner->registerTarget(GuideTarget::JogHome, m_jogPanel->goHomeButton());
    // Status bar + scene.
    m_guideRunner->registerTarget(GuideTarget::EStop, m_statusBar->eStopButton());
    m_guideRunner->registerTarget(GuideTarget::ViewIso, m_btnViewIso);
}

void ShellWindow::connectGuide() {
    // ===== GuidePanel intents ==============================================================
    connect(m_guidePanel, &GuidePanel::guideStartRequested, this,
            [this](const QString& scenarioId, GuideMode mode) {
        const auto result = m_guideRunner->start(scenarioId, mode);
        if (result.isError()) {
            // The scenario refused to start (typed): the callout names the reason and the
            // missing piece; the panel stays in its idle state (the runner emitted nothing).
            m_guideCallout->showAbort(QStringLiteral("%1 — %2")
                                          .arg(toString(result.error()),
                                               m_guideRunner->lastErrorDetail()));
            positionGuideCallout(nullptr);
            m_guideCallout->show();
            m_guideCallout->raise();
        }
    });
    connect(m_guidePanel, &GuidePanel::guideStopRequested, m_guideRunner, &GuideRunner::stop);

    // ===== Runner feedback =================================================================
    connect(m_guideRunner, &GuideRunner::scenarioStarted,
            m_guidePanel, &GuidePanel::onScenarioStarted);
    connect(m_guideRunner, &GuideRunner::stepEntered, this,
            [this](int stepIndex, int stepCount, const GuideStep& step, QWidget* targetWidget) {
        m_guidePanel->onStepEntered(stepIndex, stepCount);
        // Callout controls by mode: PLAY auto-advances (no buttons); STEP always offers NEXT;
        // TRY offers NEXT on non-click steps and SHOW ME on hands-on click steps.
        const GuideMode mode = m_guideRunner->mode();
        const bool isClickStep = step.action == GuideAction::ClickTarget;
        const bool nextVisible = (mode == GuideMode::Step)
                                 || (mode == GuideMode::Try && !isClickStep);
        const bool showMeVisible = (mode == GuideMode::Try && isClickStep);
        // Highlight frame first (it sits behind the callout in z-order), then the callout docked
        // clear of it.
        positionGuideHighlight(targetWidget);
        m_guideCallout->showStep(stepIndex, stepCount, step.title, step.body,
                                 nextVisible, showMeVisible);
        positionGuideCallout(targetWidget);
        m_guideCallout->show();
        m_guideCallout->raise();
    });
    // TRY idle escalation: the runner asks for the hint; showing it grows the callout, so re-dock.
    connect(m_guideRunner, &GuideRunner::hintEscalated, this, [this](const QString& hintText) {
        m_guideCallout->showHint(hintText);
        positionGuideCallout(m_guideCalloutTarget.data());
        m_guideCallout->raise();
    });
    connect(m_guideRunner, &GuideRunner::scenarioEnded, this,
            [this](GuideOutcome outcome, const QString& detail) {
        m_guidePanel->onScenarioEnded(outcome, detail);
        m_guideHighlight->hide();   // the run is over — nothing is being pointed at
        if (outcome == GuideOutcome::Aborted) {
            m_guideCallout->showAbort(detail);   // stays up until EXIT — aborts must be read
            m_guideCallout->raise();
        } else {
            m_guideCallout->hide();
        }
    });

    // ===== Callout intents =================================================================
    connect(m_guideCallout, &GuideCallout::nextRequested,
            m_guideRunner, &GuideRunner::advanceRequested);
    connect(m_guideCallout, &GuideCallout::showMeRequested,
            m_guideRunner, &GuideRunner::showMeRequested);
    connect(m_guideCallout, &GuideCallout::exitRequested, this, [this]() {
        if (m_guideRunner->isRunning()) {
            m_guideRunner->stop();   // ends as StoppedByUser; scenarioEnded hides the callout
        } else {
            m_guideCallout->hide();  // dismiss a standing abort message
        }
    });
}

void ShellWindow::positionGuideCallout(QWidget* target) {
    m_guideCalloutTarget = target;
    m_guideCallout->adjustSize();
    // The callout may never overlap the native 3D viewport (the QQuickView container overdraws
    // sibling widgets) nor the status-bar E-Stop. The side columns are the only always-safe
    // surfaces, so the callout docks on the column OPPOSITE the target, vertically near it.
    bool targetOnLeftHalf = false;   // no anchor (narration/abort): dock right, below the bar
    int desiredY = m_statusBar->geometry().bottom() + kGuideCalloutGapPx;
    if (target) {
        const QRect targetRect(target->mapTo(this, QPoint(0, 0)), target->size());
        targetOnLeftHalf = targetRect.center().x() < rect().center().x();
        desiredY = targetRect.center().y() - m_guideCallout->height() / 2;
    }
    const int x = targetOnLeftHalf ? width() - m_guideCallout->width() - kGuideCalloutMarginPx
                                   : kGuideCalloutMarginPx;
    int y = qBound(m_statusBar->geometry().bottom() + kGuideCalloutGapPx, desiredY,
                   height() - m_guideCallout->height() - kGuideCalloutMarginPx);
    const QRect eStopRect(m_statusBar->eStopButton()->mapTo(this, QPoint(0, 0)),
                          m_statusBar->eStopButton()->size());
    if (QRect(QPoint(x, y), m_guideCallout->size()).intersects(eStopRect)) {
        y = eStopRect.bottom() + kGuideCalloutGapPx;
    }
    m_guideCallout->move(x, y);
}

void ShellWindow::positionGuideHighlight(QWidget* target) {
    // Narration steps carry no target: nothing is being pointed at, so the frame hides.
    if (!target) {
        m_guideHighlight->hide();
        return;
    }
    // Map the target rect into shell coordinates and inflate it, so the bold frame surrounds the
    // control rather than sitting on its edge. raise() keeps it above the panels but the callout
    // is raised after it, so the instruction card is never obscured by the frame.
    const QRect targetRect(target->mapTo(this, QPoint(0, 0)), target->size());
    m_guideHighlight->setGeometry(targetRect.adjusted(-kGuideHighlightInflatePx,
                                                      -kGuideHighlightInflatePx,
                                                      kGuideHighlightInflatePx,
                                                      kGuideHighlightInflatePx));
    m_guideHighlight->show();
    m_guideHighlight->raise();
}

// ---------------------------------------------------------------------------
// Shell-owned helpers
// ---------------------------------------------------------------------------

int ShellWindow::lookupToolId(const QString& toolName) const {
    for (const HmiToolData& tool : m_lastConfig.tools) {
        if (tool.name == toolName) return tool.id;
    }
    return 0;
}

int ShellWindow::lookupBaseId(const QString& baseName) const {
    for (const HmiBaseData& base : m_lastConfig.bases) {
        if (base.name == baseName) return base.id;
    }
    return 0;
}

void ShellWindow::positionDiagnostics() {
    m_diagnostics->move(kDiagnosticsAnchorXPx,
                        m_statusBar->geometry().bottom() + kDiagnosticsGapPx);
}

void ShellWindow::syncViewportObscuring() {
    // The viewport hosts a native window (QQuickView container) that always paints above sibling
    // widgets, so an open overlay would be punched through by the 3D view. Hiding the viewport while
    // any overlay is up is the boring, driver-safe fix (QQuickWidget compositing crashes on the
    // target AMD GPU - see viewport3d_REQUIREMENTS.md).
    const bool anyOverlayOpen =
        m_settings->isVisible() || m_hal->isVisible() || m_diagnostics->isVisible();
    m_viewport->setVisible(!anyOverlayOpen);
}

void ShellWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_settings->isVisible()) m_settings->resize(size());
    if (m_hal->isVisible()) m_hal->resize(size());
    if (m_guideCallout && m_guideCallout->isVisible()) {
        positionGuideCallout(m_guideCalloutTarget.data());   // keep the dock through resizes
        if (m_guideHighlight->isVisible()) {
            positionGuideHighlight(m_guideCalloutTarget.data());   // keep the frame on its target
        }
    }
}

void ShellWindow::setPresentationMode(bool active) {
    // Only the 3D scene and the status bar stay up while presenting. Entering forces the scene
    // toolbar visible: the UI-tab toolbar switch sits on a panel this mode hides, and the exit
    // control must never vanish with it. Pure visibility - no wiring or state is touched, so
    // every safety control on the status bar (E-STOP, mode, speed) keeps working.
    m_leftCard->setVisible(!active);
    m_rightCard->setVisible(!active);
    if (active) {
        m_viewToolbar->setVisible(true);
    }
}

void ShellWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    // The UI-tab full-screen control must always name its NEXT action (ENTER vs EXIT). The shell
    // owns the window state, so every change - F11, the on-screen button, an OS-driven change -
    // lands here and is pushed down to the panel.
    if (event->type() == QEvent::WindowStateChange && m_programEditor) {
        m_programEditor->setFullScreenActive(isFullScreen());
    }
}

} // namespace hexa
// --- END OF FILE: HexaStudio/app_shell/ShellWindow.cpp ---
