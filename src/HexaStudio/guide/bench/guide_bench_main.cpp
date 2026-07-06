// --- START OF FILE: HexaStudio/guide/bench/guide_bench_main.cpp ---
/**
 * @file guide_bench_main.cpp
 * @brief Standalone bench hosting the REAL GuidePanel + GuideRunner + GuideCallout against
 *        stand-in target widgets. NO shell, NO backend, NO network.
 *
 * The stand-in column mimics the shell's target set for the TOUR scenario (nav chips, program
 * list, RUN/TEACH buttons, jog controls, controller file list + LOAD, E-STOP, ISO view), so every
 * registry entry resolves and the full PLAY/STEP loop can be reviewed offline. The bench draws the
 * highlight frame and docks the callout the same way ShellWindow does (the runner owns neither).
 *
 * Modes: no args = interactive; --selftest = headless smoke that runs TOUR in STEP mode, pumps
 * NEXT, and exits 0 only if the scenario Finishes AND every ClickTarget step actually clicked its
 * stand-in button; --screenshot <file.png> = design-review frames: the chooser ("<file>.png") and
 * a mid-run step with highlight + callout + progress ("<file>_step.png").
 */
#include <QApplication>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QStringList>
#include <QStringListModel>
#include <QTimer>
#include <QVBoxLayout>

#include "GuideCallout.h"
#include "GuidePanel.h"
#include "GuideRunner.h"
#include "GuideScenarios.h"
#include "GuideTypes.h"
#include "HexaTheme.h"
#include "HexaWidgets.h"

using hexa::GuideAction;
using hexa::GuideCallout;
using hexa::GuideMode;
using hexa::GuideOutcome;
using hexa::GuidePanel;
using hexa::GuideRunner;
using hexa::GuideScenario;
using hexa::GuideScenarios;
using hexa::GuideStep;
using hexa::GuideTarget;

namespace {

constexpr int kBenchWidthPx = 1120;
constexpr int kBenchHeightPx = 760;
constexpr int kPanelColumnPx = 300;
constexpr int kCalloutDockMarginPx = 12;
constexpr int kHighlightBorderPx = 5;
constexpr int kHighlightInflatePx = 4;
// Selftest NEXT pump: faster than the post-action pause so presses queue naturally, slow enough
// for animateClick()'s 100 ms press to complete between pumps.
constexpr int kSelftestPumpMs = 250;

void loadAppFonts() {
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/Michroma-Regular.ttf")) == -1) {
        fprintf(stderr, "guide_bench: failed to load Michroma font\n");
    }
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/IBMPlexMono-Regular.ttf")) == -1) {
        fprintf(stderr, "guide_bench: failed to load IBM Plex Mono font\n");
    }
}

// Let queued events, style polish and layout settle before a screen grab.
void settle(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QString withSuffix(const QString& basePath, const QString& suffix) {
    if (basePath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        return basePath.chopped(4) + suffix + QStringLiteral(".png");
    }
    return basePath + suffix + QStringLiteral(".png");
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    loadAppFonts();
    const bool selftest = app.arguments().contains(QStringLiteral("--selftest"));

    QWidget window;
    window.setWindowTitle(QStringLiteral("GUIDE MODULE BENCH (OFFLINE)"));
    window.setAttribute(Qt::WA_StyledBackground, true);
    window.setStyleSheet(QStringLiteral("background-color: %1;").arg(Hexa::Colors::Background));
    window.resize(kBenchWidthPx, kBenchHeightPx);

    QHBoxLayout* rootLayout = new QHBoxLayout(&window);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(10);

    // Left column: the REAL panel, on a shell-style card at the shell's column width.
    QFrame* panelCard = new QFrame(&window);
    panelCard->setObjectName(QStringLiteral("ShellCard"));
    panelCard->setStyleSheet(QStringLiteral(
        "QFrame#ShellCard { background-color: %1; border: 1px solid %2; border-radius: 12px; }")
        .arg(Hexa::Colors::Surface, Hexa::Colors::Hairline));
    panelCard->setFixedWidth(kPanelColumnPx);
    QVBoxLayout* cardLayout = new QVBoxLayout(panelCard);
    cardLayout->setContentsMargins(8, 8, 8, 8);
    GuidePanel* panel = new GuidePanel(panelCard);
    cardLayout->addWidget(panel);
    rootLayout->addWidget(panelCard);

    // Right column: stand-in targets for everything TOUR addresses (nav chips, program list,
    // RUN/TEACH, jog controls, controller list + LOAD, ISO).
    QWidget* standIns = new QWidget(&window);
    QVBoxLayout* standInLayout = new QVBoxLayout(standIns);
    standInLayout->setSpacing(10);
    standInLayout->addWidget(HexaWidgets::createLabelSectionTitle(
        QStringLiteral("STAND-IN TARGETS (SHELL WIDGETS IN THE REAL APP)")));

    QHBoxLayout* navRow = new QHBoxLayout();
    QPushButton* navRun = HexaWidgets::createButtonStd(QStringLiteral("RUN"), standIns, 52, 52);
    QPushButton* navTeach = HexaWidgets::createButtonStd(QStringLiteral("TEACH"), standIns, 52, 52);
    QPushButton* navFile = HexaWidgets::createButtonStd(QStringLiteral("FILE"), standIns, 52, 52);
    QPushButton* navUi = HexaWidgets::createButtonStd(QStringLiteral("UI"), standIns, 52, 52);
    QPushButton* eStop = HexaWidgets::createButtonDanger(QStringLiteral("E-STOP"), standIns, 100, 52);
    for (QPushButton* chip : {navRun, navTeach, navFile, navUi, eStop}) {
        navRow->addWidget(chip);
    }
    navRow->addStretch();
    standInLayout->addLayout(navRow);

    QListView* programList = new QListView(standIns);
    auto* programModel = new QStringListModel(
        QStringList{QStringLiteral("1  PTP  P1"), QStringLiteral("2  LIN  P2"),
                    QStringLiteral("3  SET DO 1 ON"), QStringLiteral("4  LIN  P3")},
        programList);
    programList->setModel(programModel);
    programList->setStyleSheet(Hexa::Styles::ListView);
    programList->setFixedHeight(110);
    standInLayout->addWidget(programList);

    // RUN tab / TEACH tab tool buttons.
    QHBoxLayout* runTeachRow = new QHBoxLayout();
    QPushButton* runButton = HexaWidgets::createButtonPrimary(QStringLiteral("RUN"), standIns, 120, 44);
    QPushButton* teachButton = HexaWidgets::createButtonStd(QStringLiteral("+ TEACH"), standIns, 120, 44);
    runTeachRow->addWidget(runButton);
    runTeachRow->addWidget(teachButton);
    runTeachRow->addStretch();
    standInLayout->addLayout(runTeachRow);

    // Jog controls (how the robot is moved by hand).
    QHBoxLayout* jogRow = new QHBoxLayout();
    QPushButton* jogEnable = HexaWidgets::createButtonStd(QStringLiteral("ENABLE JOG"), standIns, 120, 44);
    QPushButton* jogKey = HexaWidgets::createButtonIcon(QStringLiteral("+"), standIns, 48, 44);
    QPushButton* jogHome = HexaWidgets::createButtonStd(QStringLiteral("GO HOME"), standIns, 100, 44);
    jogRow->addWidget(jogEnable);
    jogRow->addWidget(jogKey);
    jogRow->addWidget(jogHome);
    jogRow->addStretch();
    standInLayout->addLayout(jogRow);

    // Controller program library (FILE tab) + LOAD.
    QListView* controllerList = new QListView(standIns);
    auto* controllerModel = new QStringListModel(
        QStringList{QStringLiteral("DEMO_HEXA_CONTOUR"), QStringLiteral("DEMO_ORBIT"),
                    QStringLiteral("PICK_PLACE_01")},
        controllerList);
    controllerList->setModel(controllerModel);
    controllerList->setStyleSheet(Hexa::Styles::ListView);
    controllerList->setFixedHeight(90);
    standInLayout->addWidget(controllerList);

    QHBoxLayout* fileRow = new QHBoxLayout();
    QPushButton* controllerLoad = HexaWidgets::createButtonStd(QStringLiteral("LOAD"), standIns, 100, 40);
    QPushButton* viewIso = HexaWidgets::createButtonStd(QStringLiteral("ISO"), standIns, 70, 40);
    fileRow->addWidget(controllerLoad);
    fileRow->addStretch();
    fileRow->addWidget(new QLabel(QStringLiteral("SCENE:"), standIns));
    fileRow->addWidget(viewIso);
    standInLayout->addLayout(fileRow);
    standInLayout->addStretch();
    rootLayout->addWidget(standIns, 1);

    // Runner + registry (the shell's registerGuideTargets() equivalent, bench edition).
    GuideRunner runner;
    runner.registerTarget(GuideTarget::NavRun, navRun);
    runner.registerTarget(GuideTarget::NavTeach, navTeach);
    runner.registerTarget(GuideTarget::NavFile, navFile);
    runner.registerTarget(GuideTarget::NavUi, navUi);
    runner.registerTarget(GuideTarget::ProgramList, programList);
    runner.registerTarget(GuideTarget::RunButton, runButton);
    runner.registerTarget(GuideTarget::TeachButton, teachButton);
    runner.registerTarget(GuideTarget::JogEnable, jogEnable);
    runner.registerTarget(GuideTarget::JogKey, jogKey);
    runner.registerTarget(GuideTarget::JogHome, jogHome);
    runner.registerTarget(GuideTarget::ControllerFileList, controllerList);
    runner.registerTarget(GuideTarget::ControllerLoad, controllerLoad);
    runner.registerTarget(GuideTarget::EStop, eStop);
    runner.registerTarget(GuideTarget::ViewIso, viewIso);

    // Highlight frame (the shell owns this in production; the bench replicates it so the runner
    // stays widget-free). Mouse-transparent so clicks reach the target underneath.
    QFrame* highlight = new QFrame(&window);
    highlight->setObjectName(QStringLiteral("GuideHighlight"));
    highlight->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    highlight->setAttribute(Qt::WA_StyledBackground, true);
    highlight->setStyleSheet(QStringLiteral(
        "QFrame#GuideHighlight { background: transparent; border: %1px solid %2; border-radius: 8px; }")
        .arg(kHighlightBorderPx).arg(Hexa::Colors::Active));
    highlight->hide();

    const auto positionHighlight = [highlight, &window](QWidget* target) {
        if (!target) {
            highlight->hide();
            return;
        }
        const QRect rect(target->mapTo(&window, QPoint(0, 0)), target->size());
        highlight->setGeometry(rect.adjusted(-kHighlightInflatePx, -kHighlightInflatePx,
                                             kHighlightInflatePx, kHighlightInflatePx));
        highlight->show();
        highlight->raise();
    };

    GuideCallout* callout = new GuideCallout(&window);
    callout->hide();

    // Wiring: the same shape ShellWindow::connectGuide() uses.
    QObject::connect(panel, &GuidePanel::guideStartRequested, &runner,
                     [&runner, callout, &window](const QString& id, GuideMode mode) {
        const auto result = runner.start(id, mode);
        if (result.isError()) {
            callout->showAbort(QStringLiteral("%1 — %2")
                                   .arg(toString(result.error()), runner.lastErrorDetail()));
            callout->move(window.width() - callout->width() - kCalloutDockMarginPx,
                          kCalloutDockMarginPx);
            callout->show();
            callout->raise();
        }
    });
    QObject::connect(panel, &GuidePanel::guideStopRequested, &runner, &GuideRunner::stop);
    QObject::connect(&runner, &GuideRunner::scenarioStarted, panel, &GuidePanel::onScenarioStarted);
    QObject::connect(&runner, &GuideRunner::stepEntered, panel,
                     [panel, callout, &runner, &window, positionHighlight](
                         int index, int count, const GuideStep& step, QWidget* target) {
        panel->onStepEntered(index, count);
        positionHighlight(target);
        const GuideMode mode = runner.mode();
        const bool isClickStep = step.action == GuideAction::ClickTarget;
        const bool nextVisible = (mode == GuideMode::Step)
                                 || (mode == GuideMode::Try && !isClickStep);
        const bool showMeVisible = (mode == GuideMode::Try && isClickStep);
        callout->showStep(index, count, step.title, step.body, nextVisible, showMeVisible);
        // Dock opposite the target's half; fall back to the bottom-right for narration steps.
        const bool targetOnLeft =
            target && target->mapTo(&window, QPoint(0, 0)).x() + target->width() / 2
                          < window.width() / 2;
        const int x = targetOnLeft ? window.width() - callout->width() - kCalloutDockMarginPx
                                   : kCalloutDockMarginPx;
        callout->move(x, window.height() - callout->height() - kCalloutDockMarginPx);
        callout->show();
        callout->raise();
    });
    QObject::connect(&runner, &GuideRunner::hintEscalated, callout,
                     [callout, &window](const QString& hintText) {
        callout->showHint(hintText);
        callout->move(callout->x(), window.height() - callout->height() - kCalloutDockMarginPx);
        callout->raise();
    });
    QObject::connect(&runner, &GuideRunner::scenarioEnded, panel,
                     [panel, callout, highlight](GuideOutcome outcome, const QString& detail) {
        panel->onScenarioEnded(outcome, detail);
        highlight->hide();
        if (outcome == GuideOutcome::Aborted) {
            callout->showAbort(detail);
        } else {
            callout->hide();
        }
    });
    QObject::connect(callout, &GuideCallout::nextRequested, &runner, &GuideRunner::advanceRequested);
    QObject::connect(callout, &GuideCallout::showMeRequested, &runner, &GuideRunner::showMeRequested);
    QObject::connect(callout, &GuideCallout::exitRequested, &runner, [&runner, callout]() {
        if (runner.isRunning()) {
            runner.stop();
        } else {
            callout->hide();
        }
    });

    const QStringList arguments = app.arguments();
    const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotIndex != -1) {
        // Design-review frames: the chooser at rest, then a mid-run STEP frame with the highlight
        // frame + callout + progress.
        if (screenshotIndex + 1 >= arguments.size()) {
            fprintf(stderr, "guide_bench: --screenshot needs a target file, e.g. guide.png\n");
            return 1;
        }
        const QString basePath = arguments.at(screenshotIndex + 1);
        window.show();
        settle(400);   // fonts/styles/layout settle
        if (!window.grab().save(basePath)) {
            fprintf(stderr, "guide_bench screenshot: cannot write %s\n", qPrintable(basePath));
            return 1;
        }
        // Render the DEFAULT mode (TRY) on a hands-on CLICK step, so the frame shows the SHOW ME
        // fallback and the highlight over the control the user must press. Advance past the
        // leading narration/highlight steps (they NEXT immediately in TRY) to the first click step.
        const auto startResult = runner.start(QStringLiteral("TOUR"), GuideMode::Try);
        if (startResult.isError()) {
            fprintf(stderr, "guide_bench screenshot: start failed: %s — %s\n",
                    qPrintable(toString(startResult.error())),
                    qPrintable(runner.lastErrorDetail()));
            return 1;
        }
        const GuideScenario* tourForShot = GuideScenarios::find(QStringLiteral("TOUR"));
        int leadingNonClick = 0;
        for (const GuideStep& step : tourForShot->steps) {
            if (step.action == GuideAction::ClickTarget) {
                break;
            }
            ++leadingNonClick;
        }
        for (int i = 0; i < leadingNonClick; ++i) {
            runner.advanceRequested();   // NEXT past each leading narration/highlight step
        }
        settle(500);                 // let the highlight frame and callout paint
        const QString stepPath = withSuffix(basePath, QStringLiteral("_step"));
        if (!window.grab().save(stepPath)) {
            fprintf(stderr, "guide_bench screenshot: cannot write %s\n", qPrintable(stepPath));
            return 1;
        }
        runner.stop();
        fprintf(stdout, "guide_bench screenshot: wrote %s and %s\n",
                qPrintable(basePath), qPrintable(stepPath));
        return 0;
    }

    if (selftest) {
        // Headless smoke in the DEFAULT mode (TRY): the bench plays the USER — it presses the real
        // stand-in control on each ClickTarget step and NEXTs the narration/highlight steps —
        // and verifies the scenario Finishes with every hands-on press landing on its target.
        const GuideScenario* tour = GuideScenarios::find(QStringLiteral("TOUR"));
        if (!tour) {
            fprintf(stderr, "guide_bench selftest: TOUR is not in the compiled-in table\n");
            return 1;
        }
        int expectedClicks = 0;
        for (const GuideStep& step : tour->steps) {
            if (step.action == GuideAction::ClickTarget) {
                ++expectedClicks;
            }
        }
        int actualClicks = 0;
        const auto countClick = [&actualClicks]() { ++actualClicks; };
        // The TOUR ClickTarget steps address the nav chips and the ISO preset.
        for (QPushButton* clickable : {navRun, navTeach, navFile, viewIso}) {
            QObject::connect(clickable, &QPushButton::clicked, clickable, countClick);
        }

        // Driver: on each step, act as the user would in TRY — press the highlighted control on a
        // click step (after a short beat), or NEXT past a narration/highlight step.
        QObject::connect(&runner, &GuideRunner::stepEntered, &app,
                         [&runner](int, int, const GuideStep& step, QWidget* target) {
            if (step.action == GuideAction::ClickTarget) {
                QTimer::singleShot(kSelftestPumpMs, target, [target]() {
                    if (auto* button = qobject_cast<QAbstractButton*>(target)) {
                        button->click();   // simulate the user's real press
                    }
                });
            } else {
                QTimer::singleShot(kSelftestPumpMs, &runner, &GuideRunner::advanceRequested);
            }
        });

        int exitCode = 1;
        QObject::connect(&runner, &GuideRunner::scenarioEnded, &app,
                         [&](GuideOutcome outcome, const QString& detail) {
            if (outcome != GuideOutcome::Finished) {
                fprintf(stderr, "guide_bench selftest: outcome %s (%s)\n",
                        qPrintable(toString(outcome)), qPrintable(detail));
            } else if (actualClicks != expectedClicks) {
                fprintf(stderr, "guide_bench selftest: %d of %d hands-on presses landed\n",
                        actualClicks, expectedClicks);
            } else {
                fprintf(stdout, "guide_bench selftest: TOUR finished in TRY, %d/%d hands-on "
                        "presses landed\n", actualClicks, expectedClicks);
                exitCode = 0;
            }
            app.exit(exitCode);
        });
        // Watchdog: never hang the CI if a wait never resolves.
        QTimer::singleShot(30000, &app, [&app]() {
            fprintf(stderr, "guide_bench selftest: TIMEOUT (a step never advanced)\n");
            app.exit(1);
        });

        window.show();   // widgets must be realized for styles/clicks to behave as in production
        const auto startResult = runner.start(QStringLiteral("TOUR"), GuideMode::Try);
        if (startResult.isError()) {
            fprintf(stderr, "guide_bench selftest: start failed: %s — %s\n",
                    qPrintable(toString(startResult.error())),
                    qPrintable(runner.lastErrorDetail()));
            return 1;
        }
        return app.exec();
    }

    window.show();
    return app.exec();
}
// --- END OF FILE: HexaStudio/guide/bench/guide_bench_main.cpp ---
