// --- START OF FILE: HexaStudio/guide/GuideRunner.h ---
/**
 * @file GuideRunner.h
 * @brief The guided-demo engine: step state machine, per-mode pacing, target resolution,
 *        highlight pulse, virtual clicks. QObject with no widgets of its own — the shell owns it
 *        and routes its feedback to GuidePanel/GuideCallout (mediator rules).
 *
 * Pacing policies: PLAY advances on each step's dwell timer; STEP advances only on
 * advanceRequested() (the callout NEXT button); TRY (the default) is hands-on — on a ClickTarget
 * step the runner does NOT press the button, it connects to the target's real clicked() and WAITS
 * for the user, escalates hintText on idle, and lets the callout SHOW ME press it as a fallback.
 * In PLAY/STEP a ClickTarget step presses the real button via QAbstractButton::animateClick() (the
 * production signal path); a short post-action pause then lets the audience see the effect.
 * Non-ClickTarget steps (narration, highlight-only) advance on the timer (PLAY) or NEXT (STEP/TRY).
 *
 * Failure policy: every failure is typed (GuideError), loud, and ends the run in a defined state
 * (GDE-REQ-0040/0050/0080). Preflight at start() validates EVERY addressed target; mid-run, a
 * vanished target or a disabled button aborts the scenario — never a silent skip.
 */
#ifndef HEXA_GUIDE_RUNNER_H
#define HEXA_GUIDE_RUNNER_H

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include "GuideTypes.h"
#include "result.h"   // RDT::Result

class QAbstractButton;
class QWidget;

namespace hexa {

class GuideRunner : public QObject {
    Q_OBJECT
public:
    explicit GuideRunner(QObject* parent = nullptr);

    /// @brief Register the widget a GuideTarget resolves to. QPointer-held: a destroyed target
    /// reads as "missing" (typed error), never as a dangling pointer.
    void registerTarget(GuideTarget target, QWidget* widget);

    /// @brief Start a scenario. Fails typed and does NOT start when the id is unknown, the run is
    /// busy, or any addressed target is missing/unclickable (preflight, GDE-REQ-0040).
    RDT::Result<void, GuideError> start(const QString& scenarioId, GuideMode mode);

    bool isRunning() const { return m_scenario != nullptr; }
    GuideMode mode() const { return m_mode; }

    /// @brief Operator-facing detail of the last start() failure (scenario id, step + target).
    const QString& lastErrorDetail() const { return m_lastErrorDetail; }

public slots:
    /// @brief Operator EXIT/STOP: ends the run as StoppedByUser. Safe to call when idle (no-op).
    void stop();

    /// @brief Callout NEXT: completes a presenter-paced step (STEP always; TRY only for
    /// non-ClickTarget steps, which have no button to press). Ignored in PLAY and during a pause.
    void advanceRequested();

    /// @brief Callout SHOW ME (TRY only): presses the awaited button for the user, so a demo can
    /// never dead-end. No-op unless a TRY ClickTarget step is currently awaiting the user.
    void showMeRequested();

signals:
    void scenarioStarted(const QString& scenarioId, GuideMode mode, int stepCount);
    /// @param targetWidget resolved target of the step (nullptr for narration) — the shell uses
    /// it to place the callout; ownership stays with the widget tree.
    void stepEntered(int stepIndex, int stepCount, const GuideStep& step, QWidget* targetWidget);
    /// @brief TRY idle escalation: the user has not pressed the highlighted control; show the hint.
    void hintEscalated(const QString& hintText);
    void scenarioEnded(GuideOutcome outcome, const QString& detail);

private slots:
    void onTimerElapsed();
    /// @brief The awaited button was pressed (by the user OR by SHOW ME) in TRY mode.
    void onUserClicked();

private:
    // Step phases: Presenting (highlight + callout up, waiting for the timer/NEXT pacing);
    // AwaitingUserClick (TRY ClickTarget — connected to the real clicked(), idle-hint timer armed);
    // PostAction (a ClickTarget step's press landed; short pause for the effect).
    enum class Phase { Idle, Presenting, AwaitingUserClick, PostAction };

    void enterStep(int index);
    void completePresentation();   // pacing satisfied -> perform the step's action
    void beginUserWait(QAbstractButton* button);   // TRY: connect clicked() + arm the hint timer
    void endUserWait();                            // disconnect + stop the hint timer
    void advanceToNextStep();
    void endScenario(GuideOutcome outcome, const QString& detail);
    void abortScenario(GuideError error, const QString& detail);
    QWidget* widgetFor(GuideTarget target) const;

    QHash<GuideTarget, QPointer<QWidget>> m_targets;

    const GuideScenario* m_scenario = nullptr;   // points into GuideScenarios::all() static storage
    GuideMode m_mode = GuideMode::Play;
    int m_stepIndex = -1;
    Phase m_phase = Phase::Idle;
    QTimer m_timer;                 // single-shot; PLAY dwell / post-action pause / TRY idle-hint
    QString m_lastErrorDetail;
    // TRY: the button the user must press this step, and the live clicked() connection to it.
    QPointer<QAbstractButton> m_awaitedButton;
    QMetaObject::Connection m_clickConnection;
    // The current step's target rides out on stepEntered(); the host (shell/bench) draws the
    // highlight frame over it. The runner deliberately owns NO widgets and touches NO styles.
};

} // namespace hexa

#endif // HEXA_GUIDE_RUNNER_H
// --- END OF FILE: HexaStudio/guide/GuideRunner.h ---
