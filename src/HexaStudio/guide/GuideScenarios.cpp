// --- START OF FILE: HexaStudio/guide/GuideScenarios.cpp ---
#include "GuideScenarios.h"

namespace hexa {

namespace {

// Step builders: the table below must read like a storyboard, not like brace soup.
GuideStep narration(const QString& title, const QString& body, int dwellMs = kGuideDefaultDwellMs) {
    GuideStep step;
    step.action = GuideAction::Narrate;
    step.title = title;
    step.body = body;
    step.dwellMs = dwellMs;
    return step;
}

GuideStep highlight(GuideTarget target, const QString& title, const QString& body,
                    int dwellMs = kGuideDefaultDwellMs) {
    GuideStep step;
    step.target = target;
    step.action = GuideAction::HighlightOnly;
    step.title = title;
    step.body = body;
    step.dwellMs = dwellMs;
    return step;
}

// @param hint short "do this now" prompt shown in TRY mode if the user idles before pressing.
GuideStep click(GuideTarget target, const QString& title, const QString& body,
                const QString& hint, int dwellMs = kGuideDefaultDwellMs) {
    GuideStep step;
    step.target = target;
    step.action = GuideAction::ClickTarget;
    step.title = title;
    step.body = body;
    step.hintText = hint;
    step.dwellMs = dwellMs;
    return step;
}

// TOUR — the teach-and-run workflow, NO motion. It shows how a HexaStudio operator moves the
// robot by hand, teaches points, loads a ready program and runs it. Tab switches are real clicks
// through the production signal path (button -> panel intent -> shell mediator); the motion
// controls (jog keys, TEACH, LOAD, RUN) are highlighted and narrated, not pressed, so the tour
// stays safe with a live controller. Actually driving the robot is the motion scenario (phase P3).
GuideScenario buildTour() {
    GuideScenario tour;
    tour.id = QStringLiteral("TOUR");
    tour.name = QStringLiteral("TOUR");
    tour.durationText = QStringLiteral("~2.5 MIN");
    tour.description = QStringLiteral(
        "The teach-and-run workflow: jog the robot by hand, teach points, load a program, run it.");
    tour.involvesMotion = false;
    tour.steps = {
        narration(QStringLiteral("WELCOME TO HEXASTUDIO"),
                  QStringLiteral("This is the operator interface of the HexaKinetica robot. This "
                                 "short tour walks the real workflow - moving the robot, teaching "
                                 "points, loading and running a program - on the live interface."),
                  7000),
        highlight(GuideTarget::EStop, QStringLiteral("SAFETY FIRST"),
                  QStringLiteral("The E-STOP and the machine status never leave the screen. Every "
                                 "safety control stays live throughout the demo."),
                  6000),
        highlight(GuideTarget::ProgramList, QStringLiteral("THE PROGRAM"),
                  QStringLiteral("A robot program is a step list - motions, logic, IO. This is "
                                 "where it is built and where the running line is shown live."),
                  7000),
        // --- Moving the robot by hand (the jog panel is the heart of teaching) ---
        highlight(GuideTarget::JogEnable, QStringLiteral("ARM THE ROBOT"),
                  QStringLiteral("To move the robot by hand you first arm it here. On real "
                                 "hardware this button reads ENABLE JOG, then JOG READY."),
                  7000),
        highlight(GuideTarget::JogKey, QStringLiteral("JOG THE AXES"),
                  QStringLiteral("These keys drive the robot one axis or one direction at a time. "
                                 "The robot moves live in the 3D scene as you press them."),
                  8000),
        highlight(GuideTarget::JogHome, QStringLiteral("GO HOME"),
                  QStringLiteral("And a single press returns the robot to its home pose - the "
                                 "safe starting point for every program."),
                  6000),
        // --- Teaching: open the TEACH tab FIRST, then point at the control on it ---
        click(GuideTarget::NavTeach, QStringLiteral("TEACH POINTS"),
              QStringLiteral("Open the TEACH tab, where jogged poses become program points."),
              QStringLiteral("Press the highlighted TEACH tab on the left sidebar."),
              5000),
        highlight(GuideTarget::TeachButton, QStringLiteral("RECORD A POSE"),
                  QStringLiteral("Jog the robot to a pose, then press TEACH - the current "
                                 "position is recorded as the next point in the program."),
                  7000),
        // --- Loading a ready program from the controller ---
        click(GuideTarget::NavFile, QStringLiteral("PROGRAM LIBRARY"),
              QStringLiteral("Or skip teaching and load a finished program - open the FILE tab."),
              QStringLiteral("Press the highlighted FILE tab on the left sidebar."),
              5000),
        highlight(GuideTarget::ControllerFileList, QStringLiteral("ON THE CONTROLLER"),
                  QStringLiteral("Programs live on the robot controller itself. Any of them can "
                                 "be pulled into the editor here."),
                  7000),
        highlight(GuideTarget::ControllerLoad, QStringLiteral("LOAD IT"),
                  QStringLiteral("LOAD brings the selected program into the editor - its steps "
                                 "fill the list and its whole path is drawn in the 3D scene."),
                  7000),
        // --- Running ---
        click(GuideTarget::NavRun, QStringLiteral("BACK TO RUN"),
              QStringLiteral("Back to the RUN tab, where the loaded program is started."),
              QStringLiteral("Press the highlighted RUN tab on the left sidebar."),
              5000),
        highlight(GuideTarget::RunButton, QStringLiteral("RUN IT"),
                  QStringLiteral("Press RUN and the robot executes the whole program. The "
                                 "trajectory is traced live in 3D as each step runs."),
                  8000),
        click(GuideTarget::ViewIso, QStringLiteral("BEST ANGLE"),
              QStringLiteral("The ISO view gives the clearest angle on the running robot for the "
                             "audience."),
              QStringLiteral("Press the highlighted ISO button under the 3D scene."),
              5000),
        narration(QStringLiteral("THAT'S THE WORKFLOW"),
                  QStringLiteral("Jog, teach, load, run - the whole teach-and-run loop on the "
                                 "real interface. Pick another scenario card and press START, or "
                                 "take the controls yourself."),
                  8000),
    };
    return tour;
}

} // namespace

const QVector<GuideScenario>& GuideScenarios::all() {
    // Built once, on first use. Static storage: GuideRunner keeps a pointer into this vector for
    // the duration of a run (GDE-REQ-0030 - the table IS the persistence).
    static const QVector<GuideScenario> kScenarios = {
        buildTour(),
    };
    return kScenarios;
}

const GuideScenario* GuideScenarios::find(const QString& scenarioId) {
    for (const GuideScenario& scenario : all()) {
        if (scenario.id == scenarioId) {
            return &scenario;
        }
    }
    return nullptr;
}

} // namespace hexa
// --- END OF FILE: HexaStudio/guide/GuideScenarios.cpp ---
