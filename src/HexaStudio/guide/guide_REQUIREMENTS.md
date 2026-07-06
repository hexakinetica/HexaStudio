# guide — module requirements (Phase P1)

> Guided demo system for HexaStudio NG: a GUIDE tab at the bottom of the left-panel sidebar runs
> scripted scenarios over the REAL application UI (highlighted buttons, virtual clicks through the
> production signal paths). Target audience: investor demos and first-contact onboarding.
> Concept approved by the boss 2026-07-06. This file covers the P1 skeleton that is implemented;
> the full concept (TRY mode, trajectory library, motion scenarios) is phased in the top-level
> handover specification (phases P2–P4).

## 1. Scope of P1 (implemented)

- GUIDE nav chip at the BOTTOM of the program-editor sidebar (below the stretch, under FILE/UI).
- GuidePanel chooser page: two-line scenario cards, PLAY/STEP mode chips, START/STOP, outcome line.
- GuideRunner engine: step state machine, PLAY (per-step timers) and STEP (callout NEXT) pacing,
  bold static highlight frame on the current target, virtual clicks via
  `QAbstractButton::animateClick()`.
- GuideCallout floating card: step progress, title, body, NEXT / EXIT, abort display.
- One compiled-in scenario: TOUR (teach-and-run walkthrough, NO motion).
- Explicit target registry built by the shell (`ShellWindow::registerGuideTargets()`).
- **TRY mode (P2, pulled forward — now the DEFAULT):** hands-on hint escalation + SHOW ME.

Out of current scope (later phases): the live-motion scenario `LIVE TEACH & RUN` (jog the robot,
teach, load, run) with its motion engine advances (`WaitMode`/`WaitRobotIdle`/`WaitProgramFinished`,
jog repeat count), the §11 motion gates, and the SHOWCASE finale — see
`DRAFT_scenario_live_teach_run.md` (signed off in direction, implementation next).

## 2. Requirements

- **GDE-REQ-0010 (production path only).** A virtual press MUST be `animateClick()` on the real
  registered widget: the press is visible and the command travels the exact production route
  (button → panel signal → shell mediator → backend). The guide never injects data behind the UI.
- **GDE-REQ-0020 (explicit target registry).** Scenario targets are addressed by the
  `GuideTarget` enum and resolved through a registry the shell builds in ONE readable method from
  explicit panel accessors. `findChild` / objectName lookups are FORBIDDEN (hidden data flow).
- **GDE-REQ-0030 (compiled-in scenarios).** The ONLY scenario source is the static table in
  `GuideScenarios.cpp`. No parser, no runtime format, no user authoring surface of any kind.
- **GDE-REQ-0040 (preflight validation).** At scenario start EVERY addressed target must be
  registered, alive, and (for click steps) a `QAbstractButton`. Any miss is a typed
  `GuideError`; the scenario refuses to start and the error names the step and the target.
  A demo must never dead-end in front of an audience.
- **GDE-REQ-0050 (no silent skips).** A click step whose target is disabled at press time ABORTS
  the scenario with `GuideError::TargetDisabled` (a disabled `animateClick()` is a silent no-op in
  Qt, which would otherwise hide the failure). A target destroyed mid-run aborts as
  `TargetVanished`.
- **GDE-REQ-0060 (highlight = mouse-transparent overlay frame over the target).** The highlight
  is a bold violet frame the HOST (shell/bench) positions OVER the current target — it never
  touches the target's own stylesheet, so it survives widgets that re-style themselves on status
  ticks (the jog ENABLE button restyles ~20×/s; the earlier stylesheet-appended border silently
  vanished on it). The frame is `WA_TransparentForMouseEvents`, so both the virtual click and a
  real user click reach the control underneath. This is NOT the forbidden full-window overlay:
  it is a small frame over one control, and no guide target sits over the 3D viewport, so the
  native window never overdraws it (a full-window overlay WOULD be punched through — see
  `ShellWindow::syncViewportObscuring()`). The GuideRunner owns no widgets and touches no styles;
  it only emits the target on `stepEntered` and the host draws/hides the frame.
- **GDE-REQ-0070 (callout placement).** The callout must never overlap the native viewport
  rectangle and never cover the status-bar E-Stop. The shell docks it on the side column opposite
  the current target (the side columns are the only always-safe surfaces).
- **GDE-REQ-0080 (defined end states).** A run always ends in exactly one of
  Finished / StoppedByUser / Aborted(reason); the panel shows which, the callout shows abort
  reasons until dismissed. EXIT is available on the callout at every step in every mode.
- **GDE-REQ-0085 (presentation-distance readability, boss review 2026-07-06, second pass).**
  The callout is read from across the room during investor demos: its type runs well above the
  panel-instrument scale (title 20 px, body 16 px, step counter 15 px accent), and scenario
  progress is visible at a glance without reading — a filled accent progress bar on the callout,
  mirrored on the GuidePanel while a run is active. Text containers must never clip: wrapped
  guide text carries a REAL QFont (a stylesheet font is invisible to size hints — house
  precedent, HexaWidgets status label), runs at a FIXED width (exact word-wrap
  height-for-width), and its container takes exactly the height the text needs. Fixed text-block
  heights are forbidden (clipped twice under real font metrics).
- **GDE-REQ-0086 (violet guide accent, boss decision 2026-07-06).** The guide's accent colour is
  VIOLET (`Hexa::Colors::Active`) everywhere — highlight pulse, step counter, progress fills,
  NEXT, selected card, checked pace chip — deliberately distinct from the teal Primary the
  target controls themselves use, so the "you are being pointed at" cue never blends with a
  checked/hovered target state. Recorded trade-off: violet elsewhere in the HMI means "motion
  active"; the boss accepted the double duty for the guide surface.
- **GDE-REQ-0087 (highlight strength, boss review 2026-07-06, pass 3).** The target highlight is
  a bold STATIC violet frame (5 px) — it reads from across the room by weight, not by motion.
  Blinking/pulsing is FORBIDDEN: it was tried and rejected as eye-searing, worst over the red
  E-STOP. Whenever the guide points at anything, the frame is fat — no thin hints.
- **GDE-REQ-0090 (TRY is the default, hands-on mode, boss 2026-07-07).** The default mode is TRY:
  on a ClickTarget step the runner does NOT press the button — it connects to the target's real
  `clicked()` and waits for the USER. After `kIdleHintMs` idle it emits the step's `hintText`
  (callout shows it in the accent), and a callout **SHOW ME** button presses the target for the
  user (a demo must never dead-end, GDE-REQ-0040). Non-ClickTarget steps advance on NEXT (as STEP).
  PLAY/STEP remain selectable. The user still cannot press a disabled control — that aborts loudly
  rather than waiting on a click that can never come.
- **GDE-REQ-0089 (point at the real control, boss review pass 4).** A step must highlight the
  SPECIFIC control the operator would touch (a jog key, the RUN button, the LOAD button), never a
  whole panel/card as a stand-in. The TOUR content is the teach-and-run workflow — jog the robot,
  teach a point, load a program, run it — NOT a tour of view presets. Exactly one view-preset
  interaction (ISO framing) is permitted, at the end, framed as supporting the running robot.
- **GDE-REQ-0088 (narrate what is on screen, boss review pass 3).** A step's text must describe
  what the audience currently sees. A page/tab a step talks about must be OPENED by a preceding
  click step — never narrated first and opened at the step's end (TOUR: the UI tab opens, THEN
  the scene-layers step tells its story).
- **GDE-REQ-0090 (module isolation).** Feature modules never see the guide module.
  `ProgramEditorPanel` hosts the page through the opaque `installGuidePage(QWidget*)`; all wiring
  lives in the shell (`connectGuide()`), next to `connectBackend`.

## 3. Contracts

GuidePanel intents: `guideStartRequested(scenarioId, GuideMode)`, `guideStopRequested()`.
GuidePanel feedback slots: `onScenarioStarted`, `onStepEntered(index, count)`, `onScenarioEnded`.
GuideRunner: `start() -> RDT::Result<void, GuideError>` (+ `lastErrorDetail()`), `stop()`,
`advanceRequested()`; signals `scenarioStarted`, `stepEntered(index, count, step, targetWidget)`,
`scenarioEnded(outcome, detail)`.
GuideCallout: `showStep(...)` / `showAbort(reason)`; signals `nextRequested`, `exitRequested`.

## 4. Bench

`guide_bench` hosts the REAL GuidePanel + GuideRunner + GuideCallout against stand-in target
widgets (no shell, no backend, no network). `--selftest` runs TOUR headlessly in STEP mode,
pumping NEXT, and exits 0 only if the scenario Finishes and every ClickTarget step actually
clicked its stand-in. `--screenshot <file.png>` renders design-review frames (chooser +
mid-run step with highlight/callout/progress). Launch: `run_guide_bench.bat`.
