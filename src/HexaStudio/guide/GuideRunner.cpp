// --- START OF FILE: HexaStudio/guide/GuideRunner.cpp ---
#include "GuideRunner.h"
#include "GuideScenarios.h"

#include <QAbstractButton>
#include <QWidget>

namespace hexa {

namespace {
// Post-action pause: animateClick() holds the pressed look for ~100 ms; this pause keeps the
// step on screen long enough for the audience to see what the press changed.
constexpr int kPostActionPauseMs = 900;
// TRY idle escalation: if the user has not pressed the highlighted control within this long, the
// callout shows the step's hint (before the SHOW ME fallback).
constexpr int kIdleHintMs = 6000;
} // namespace

GuideRunner::GuideRunner(QObject* parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &GuideRunner::onTimerElapsed);
}

void GuideRunner::registerTarget(GuideTarget target, QWidget* widget) {
    m_targets.insert(target, QPointer<QWidget>(widget));
}

RDT::Result<void, GuideError> GuideRunner::start(const QString& scenarioId, GuideMode mode) {
    m_lastErrorDetail.clear();
    if (isRunning()) {
        m_lastErrorDetail = m_scenario->id;
        return GuideError::AlreadyRunning;
    }
    const GuideScenario* scenario = GuideScenarios::find(scenarioId);
    if (!scenario) {
        m_lastErrorDetail = scenarioId;
        return GuideError::UnknownScenario;
    }
    if (scenario->steps.isEmpty()) {
        m_lastErrorDetail = scenarioId;
        return GuideError::EmptyScenario;
    }
    // Preflight (GDE-REQ-0040): EVERY addressed target must be registered, alive and — for click
    // steps — a real button, BEFORE the first step runs. A demo must never dead-end mid-run in
    // front of an audience when the defect was knowable at start.
    for (int i = 0; i < scenario->steps.size(); ++i) {
        const GuideStep& step = scenario->steps[i];
        if (step.action == GuideAction::Narrate) {
            continue;
        }
        QWidget* widget = widgetFor(step.target);
        if (!widget) {
            m_lastErrorDetail =
                QStringLiteral("step %1: %2").arg(i + 1).arg(toString(step.target));
            return GuideError::TargetMissing;
        }
        if (step.action == GuideAction::ClickTarget && !qobject_cast<QAbstractButton*>(widget)) {
            m_lastErrorDetail =
                QStringLiteral("step %1: %2").arg(i + 1).arg(toString(step.target));
            return GuideError::TargetNotClickable;
        }
    }

    m_scenario = scenario;
    m_mode = mode;
    emit scenarioStarted(scenario->id, mode, scenario->steps.size());
    enterStep(0);
    return RDT::Result<void, GuideError>::Success();
}

void GuideRunner::stop() {
    if (!isRunning()) {
        return;
    }
    endScenario(GuideOutcome::StoppedByUser, QString());
}

void GuideRunner::advanceRequested() {
    // NEXT completes a presenter-paced step. It applies to STEP always and to TRY's non-ClickTarget
    // steps (those sit in Presenting); a TRY ClickTarget step sits in AwaitingUserClick and is
    // driven by the real press, not NEXT. PLAY ignores NEXT, and the post-action pause is a visual,
    // not a wait state — so extra presses are ignored rather than queued.
    if (!isRunning() || m_phase != Phase::Presenting || m_mode == GuideMode::Play) {
        return;
    }
    completePresentation();
}

void GuideRunner::showMeRequested() {
    // TRY fallback: press the awaited button for the user so a demo can never dead-end.
    if (!isRunning() || m_phase != Phase::AwaitingUserClick) {
        return;
    }
    QAbstractButton* button = m_awaitedButton.data();
    if (!button) {
        abortScenario(GuideError::TargetVanished,
                      toString(m_scenario->steps[m_stepIndex].target));
        return;
    }
    if (!button->isEnabled()) {
        abortScenario(GuideError::TargetDisabled,
                      toString(m_scenario->steps[m_stepIndex].target));
        return;
    }
    button->animateClick();   // fires clicked() -> onUserClicked() completes the step
}

void GuideRunner::onUserClicked() {
    // The user (or SHOW ME) pressed the real button; the production intent has already travelled.
    if (m_phase != Phase::AwaitingUserClick) {
        return;
    }
    endUserWait();
    m_phase = Phase::PostAction;
    m_timer.start(kPostActionPauseMs);
}

void GuideRunner::onTimerElapsed() {
    if (!isRunning()) {
        return;
    }
    switch (m_phase) {
        case Phase::Presenting:
            completePresentation();   // PLAY dwell elapsed
            break;
        case Phase::PostAction:
            advanceToNextStep();
            break;
        case Phase::AwaitingUserClick:
            // TRY idle: the user has not pressed yet — surface the hint (fires once; the wait
            // continues until the real press or SHOW ME).
            emit hintEscalated(m_scenario->steps[m_stepIndex].hintText);
            break;
        case Phase::Idle:
            break;
    }
}

void GuideRunner::enterStep(int index) {
    endUserWait();   // defensive: never carry a click connection into a new step
    m_stepIndex = index;
    const GuideStep& step = m_scenario->steps[index];
    QWidget* widget = nullptr;
    if (step.action != GuideAction::Narrate) {
        widget = widgetFor(step.target);
        if (!widget) {
            abortScenario(GuideError::TargetVanished, toString(step.target));
            return;
        }
    }
    // The highlight frame is drawn by the host (shell/bench) over targetWidget: it never touches
    // the target's own stylesheet, so it survives widgets that re-style themselves on status
    // ticks (e.g. the jog ENABLE button) — the earlier stylesheet-append highlight silently
    // vanished on those. A null widget means "hide the highlight" (narration step).
    emit stepEntered(index, m_scenario->steps.size(), step, widget);

    // TRY hands-on: a ClickTarget step waits for the USER to press the real button.
    if (m_mode == GuideMode::Try && step.action == GuideAction::ClickTarget) {
        auto* button = qobject_cast<QAbstractButton*>(widget);
        if (!button) {
            abortScenario(GuideError::TargetNotClickable, toString(step.target));
            return;
        }
        if (!button->isEnabled()) {
            // The user cannot press a disabled control — abort loudly (GDE-REQ-0050), never wait
            // forever on a click that can never come.
            abortScenario(GuideError::TargetDisabled, toString(step.target));
            return;
        }
        beginUserWait(button);
        return;
    }

    m_phase = Phase::Presenting;
    if (m_mode == GuideMode::Play) {
        m_timer.start(step.dwellMs);
    }
    // STEP, and TRY's non-ClickTarget steps: wait for advanceRequested() (the callout NEXT button).
}

void GuideRunner::completePresentation() {
    const GuideStep& step = m_scenario->steps[m_stepIndex];
    if (step.action == GuideAction::ClickTarget) {
        QWidget* widget = widgetFor(step.target);
        if (!widget) {
            abortScenario(GuideError::TargetVanished, toString(step.target));
            return;
        }
        auto* button = qobject_cast<QAbstractButton*>(widget);
        if (!button) {
            // Preflight already checked this; re-check because the registry is re-writable.
            abortScenario(GuideError::TargetNotClickable, toString(step.target));
            return;
        }
        if (!button->isEnabled()) {
            // A disabled animateClick() is a silent no-op in Qt. The guide never skips a step
            // silently (GDE-REQ-0050): abort loudly with the target named.
            abortScenario(GuideError::TargetDisabled, toString(step.target));
            return;
        }
        button->animateClick();   // the production path: button -> panel intent -> shell -> backend
        m_phase = Phase::PostAction;
        m_timer.start(kPostActionPauseMs);
        return;
    }
    advanceToNextStep();
}

void GuideRunner::beginUserWait(QAbstractButton* button) {
    m_phase = Phase::AwaitingUserClick;
    m_awaitedButton = button;
    // Single live connection to the REAL button: a user press (or SHOW ME) completes the step.
    m_clickConnection = connect(button, &QAbstractButton::clicked,
                                this, &GuideRunner::onUserClicked);
    m_timer.start(kIdleHintMs);   // idle -> escalate the hint (onTimerElapsed)
}

void GuideRunner::endUserWait() {
    if (m_clickConnection) {
        disconnect(m_clickConnection);
        m_clickConnection = QMetaObject::Connection();
    }
    m_awaitedButton.clear();
    m_timer.stop();
}

void GuideRunner::advanceToNextStep() {
    if (m_stepIndex + 1 >= m_scenario->steps.size()) {
        endScenario(GuideOutcome::Finished, QString());
        return;
    }
    enterStep(m_stepIndex + 1);
}

void GuideRunner::endScenario(GuideOutcome outcome, const QString& detail) {
    m_timer.stop();
    endUserWait();   // drop any live click connection / awaited button
    m_scenario = nullptr;
    m_stepIndex = -1;
    m_phase = Phase::Idle;
    // The host hides the highlight frame on scenarioEnded (no stylesheet to restore here).
    emit scenarioEnded(outcome, detail);
}

void GuideRunner::abortScenario(GuideError error, const QString& detail) {
    endScenario(GuideOutcome::Aborted,
                detail.isEmpty() ? toString(error)
                                 : QStringLiteral("%1 (%2)").arg(toString(error), detail));
}

QWidget* GuideRunner::widgetFor(GuideTarget target) const {
    return m_targets.value(target).data();
}

} // namespace hexa
// --- END OF FILE: HexaStudio/guide/GuideRunner.cpp ---
