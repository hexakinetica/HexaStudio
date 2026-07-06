// --- START OF FILE: HexaStudio/guide/GuideTypes.h ---
/**
 * @file GuideTypes.h
 * @brief Shared value types of the guided-demo module: modes, targets, steps, scenarios, errors.
 *
 * Everything here is plain data (no QObject): the scenario table (GuideScenarios) is built from
 * these structs at static-init time, and the runner/panel/callout exchange them by value. All
 * toString() helpers are inline so the module ships no extra translation unit for diagnostics.
 */
#ifndef HEXA_GUIDE_TYPES_H
#define HEXA_GUIDE_TYPES_H

#include <QString>
#include <QVector>

namespace hexa {

/// @brief Demo pacing. Play = self-running (per-step timers); Step = presenter-paced (callout
/// NEXT); Try = hands-on (the user presses the real button; the runner waits for the actual
/// click, escalates a hint on idle, and offers SHOW ME). Try is the DEFAULT mode (boss decision
/// 2026-07-07): a first-contact user learns by doing.
enum class GuideMode {
    Play,
    Step,
    Try
};

inline QString toString(GuideMode mode) {
    switch (mode) {
        case GuideMode::Play: return QStringLiteral("PLAY");
        case GuideMode::Step: return QStringLiteral("STEP");
        case GuideMode::Try:  return QStringLiteral("TRY");
    }
    return QStringLiteral("UNKNOWN MODE");
}

/// @brief Every widget a scenario may address. The shell resolves each value to a real widget in
/// ShellWindow::registerGuideTargets() (explicit accessors, never findChild — GDE-REQ-0020).
/// P1 carries exactly the targets the TOUR scenario needs; later phases extend the enum together
/// with the scenarios that need the new targets.
enum class GuideTarget {
    None,               ///< Narration-only steps carry no target.
    NavRun,             ///< Program-editor sidebar: RUN tab chip.
    NavTeach,           ///< Program-editor sidebar: TEACH tab chip.
    NavFile,            ///< Program-editor sidebar: FILE tab chip.
    NavUi,              ///< Program-editor sidebar: UI (interface settings) tab chip.
    ProgramList,        ///< The program step list (highlight only — not a button).
    RunButton,          ///< RUN tab: the RUN/RESUME program button.
    TeachButton,        ///< TEACH tab: the + TEACH point button.
    EStop,              ///< Status-bar E-STOP (highlight only; the callout must never cover it).
    JogEnable,          ///< Jog panel: ENABLE JOG (arm) button.
    JogKey,             ///< Jog panel: a representative jog direction key.
    JogHome,            ///< Jog panel: GO HOME button.
    ControllerFileList, ///< FILE tab: the controller program list (highlight only).
    ControllerLoad,     ///< FILE tab: the controller LOAD button.
    ViewIso             ///< Scene toolbar: ISO view preset (the one framing interaction).
};

inline QString toString(GuideTarget target) {
    switch (target) {
        case GuideTarget::None:               return QStringLiteral("NONE");
        case GuideTarget::NavRun:             return QStringLiteral("NAV RUN");
        case GuideTarget::NavTeach:           return QStringLiteral("NAV TEACH");
        case GuideTarget::NavFile:            return QStringLiteral("NAV FILE");
        case GuideTarget::NavUi:              return QStringLiteral("NAV UI");
        case GuideTarget::ProgramList:        return QStringLiteral("PROGRAM LIST");
        case GuideTarget::RunButton:          return QStringLiteral("RUN BUTTON");
        case GuideTarget::TeachButton:        return QStringLiteral("TEACH BUTTON");
        case GuideTarget::EStop:              return QStringLiteral("E-STOP");
        case GuideTarget::JogEnable:          return QStringLiteral("ENABLE JOG");
        case GuideTarget::JogKey:             return QStringLiteral("JOG KEY");
        case GuideTarget::JogHome:            return QStringLiteral("GO HOME");
        case GuideTarget::ControllerFileList: return QStringLiteral("CONTROLLER FILE LIST");
        case GuideTarget::ControllerLoad:     return QStringLiteral("CONTROLLER LOAD");
        case GuideTarget::ViewIso:            return QStringLiteral("VIEW ISO");
    }
    return QStringLiteral("UNKNOWN TARGET");
}

/// @brief What a step does with its target once the highlight and callout are up.
enum class GuideAction {
    Narrate,        ///< Callout text only; no target, no highlight.
    HighlightOnly,  ///< Pulse the target; never press it (areas, safety controls).
    ClickTarget     ///< Pulse, dwell, then animateClick() through the production path.
};

/// @brief Typed guide failures. Every value names a state the operator can act on; the runner's
/// lastErrorDetail() carries the step/target/scenario name for the callout (GDE-REQ-0040/0050).
enum class GuideError {
    UnknownScenario,     ///< start(): the id is not in the compiled-in table.
    EmptyScenario,       ///< start(): the scenario has no steps (authoring defect).
    AlreadyRunning,      ///< start(): a scenario is active; stop it first.
    TargetMissing,       ///< preflight: a step's target is not registered or already destroyed.
    TargetNotClickable,  ///< preflight: a ClickTarget step's widget is not a QAbstractButton.
    TargetDisabled,      ///< run: the button is gated at press time (a disabled animateClick()
                         ///< is a silent no-op — the guide aborts loudly instead, GDE-REQ-0050).
    TargetVanished       ///< run: the target widget was destroyed mid-scenario.
};

inline QString toString(GuideError error) {
    switch (error) {
        case GuideError::UnknownScenario:
            return QStringLiteral("Scenario is not in the compiled-in table");
        case GuideError::EmptyScenario:
            return QStringLiteral("Scenario has no steps");
        case GuideError::AlreadyRunning:
            return QStringLiteral("Another scenario is already running");
        case GuideError::TargetMissing:
            return QStringLiteral("Guide target is not registered or was destroyed");
        case GuideError::TargetNotClickable:
            return QStringLiteral("Guide target is not a clickable button");
        case GuideError::TargetDisabled:
            return QStringLiteral("Guide target is disabled and cannot be pressed");
        case GuideError::TargetVanished:
            return QStringLiteral("Guide target was destroyed mid-scenario");
    }
    return QStringLiteral("Unknown guide error");
}

/// @brief The exactly-one way every run ends (GDE-REQ-0080).
enum class GuideOutcome {
    Finished,
    StoppedByUser,
    Aborted
};

inline QString toString(GuideOutcome outcome) {
    switch (outcome) {
        case GuideOutcome::Finished:      return QStringLiteral("FINISHED");
        case GuideOutcome::StoppedByUser: return QStringLiteral("STOPPED");
        case GuideOutcome::Aborted:       return QStringLiteral("ABORTED");
    }
    return QStringLiteral("UNKNOWN OUTCOME");
}

/// @brief Default PLAY-mode display time of one step (reading pace for two short sentences).
inline constexpr int kGuideDefaultDwellMs = 6000;

/// @brief One scenario step. dwellMs applies to PLAY pacing only (STEP waits for NEXT). hintText
/// is shown in TRY mode when the user idles on a ClickTarget step (escalation before SHOW ME).
struct GuideStep {
    GuideTarget target = GuideTarget::None;
    GuideAction action = GuideAction::Narrate;
    QString title;
    QString body;
    QString hintText;
    int dwellMs = kGuideDefaultDwellMs;
};

/// @brief One selectable scenario card. involvesMotion drives the MOTION badge (and, from phase
/// P3 on, the motion confirm dialog before start).
struct GuideScenario {
    QString id;             ///< Stable id the panel/runner exchange (e.g. "TOUR").
    QString name;           ///< Card line 1.
    QString durationText;   ///< Card line 2 prefix (e.g. "~2 MIN").
    QString description;    ///< Card line 2.
    bool involvesMotion = false;
    QVector<GuideStep> steps;
};

} // namespace hexa

#endif // HEXA_GUIDE_TYPES_H
// --- END OF FILE: HexaStudio/guide/GuideTypes.h ---
