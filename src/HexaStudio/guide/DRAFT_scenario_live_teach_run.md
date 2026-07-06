# Scenario "LIVE TEACH & RUN" — FINALIZED FOR SIGN-OFF (boss direction 2026-07-07)

> **Status:** direction approved by the boss 2026-07-07; this is the finalized scenario for a final
> green light before implementation. NO CODE WRITTEN YET. The guide **actually presses** the jog
> keys and RUN, and the robot **moves** in the 3D scene. It is a NEW scenario card
> `LIVE TEACH & RUN` with the **MOTION** badge; the existing TOUR stays as the no-motion overview.

## Boss decisions taken (2026-07-07)

- **Environment (D1/D2):** all scenarios currently run against the **HAL simulator**. This motion
  scenario runs with the pendant switched to **REAL mode while the HAL layer is a stub**. That
  means `motion.isSimulated == false`, so — per the code — jog ARMS and executes through the real
  planner (the 3D robot moves), while no physical hardware is driven. Same code path would move a
  real robot if a real HAL were attached, so the safety gates below still apply.
- **REAL switch is an explicit step (boss, 2026-07-07):** the demo itself switches the pendant to
  REAL **before** ENABLE JOG (new step 5). It is not a silent precondition — the audience sees the
  SIM→REAL switch and its production confirm dialog. Jog can only arm afterwards.
- **Demo program (D3):** the **controller** program store is fully intact (save AND load). Only the
  old *pendant-local* store was removed (0.6.24). The demo program is an ordinary program saved on
  the controller; the LOAD step pulls it from there. (Earlier "local store gone" framing was wrong.)
- **Spectacle (D4):** **maximum** — full choreography: arm, jog several axes, teach a point, go
  home, load a signature program, run it at full speed with the path tracing live, PRESENTATION
  finale.
- **Jog magnitude (boss, 2026-07-07):** one 15° press is too small — press **each jogged axis
  2–3 times** (idle wait between each) so the move is large and unmistakable.
- **Default mode = TRY / hands-on (boss, 2026-07-07):** the guide's default mode is the one where
  **the human presses the real buttons themselves**. This pulls TRY mode forward (it was P2) and
  makes it the default chip across the guide, not just this scenario. PLAY/STEP remain selectable.
  In TRY the engine highlights the target and WAITS for the user's real click; on idle it escalates
  a hint, and a SHOW ME button presses it for them (a demo must never dead-end). For live motion
  this is also the SAFEST default — a human triggers every motion press and confirms every dialog.

## 1. Why this needs REAL mode (verified in code, not assumed)

`JogPanel`: `armed = m_jogEnabled && !m_isSimulated`; jog keys are disabled and ENABLE JOG cannot
arm while `motion.isSimulated == true` (`JogPanel.cpp:369, 573, 674`). REAL mode makes
`isSimulated == false`, so jog works — the HAL stub underneath keeps it safe.

Also: jog is **one increment per press**, and the keys gate ("anti-stick") until `motion.robotMoving`
rises then falls, i.e. the move completes (`JogPanel.cpp:417-425`). So a visible jog = press → wait
for the robot to stop → press again. The engine needs a **WaitRobotIdle** advance (new).

## 2. Safety gates (§11 — kept even though the HAL is a stub, because the guide can't tell a stub

from real hardware; it only sees `isSimulated == false`):

- **MOTION** badge on the card; **confirm dialog** before start ("LIVE DEMO — this scenario MOVES
  the robot. Continue?"), reusing `ConfirmDialog`.
- **Refuse to start (typed, loud) unless:** controller connected; `isSimulated == false` (else jog
  can never arm — abort "switch to REAL mode: jog is unavailable in simulation"); no fault latched;
  the named demo program exists on the controller.
- **Immediate abort** with a callout diagnostic on E-Stop / backend fault / disconnect / a
  Settings-or-HAL overlay opening mid-run.
- **UNSAVED guard** before TEACH/LOAD replace the operator's work; **return to home** at the end.

## 3. Choreography (maximum spectacle)

Legend — **Action**: NARRATE · HIGHLIGHT · PRESS (virtual click on the real control) · SELECT.
**Advance**: Timer(PLAY)/NEXT(STEP) unless a BACKEND wait is named (mode-independent).

| # | Tab / state | Target | Action | Audience sees | Advance | Motion |
|---|---|---|---|---|---|---|
| 1 | — | — | NARRATE "This demo drives the real motion system. Every move is the production path." | Callout | Timer/NEXT | no |
| 2 | — | E-STOP | HIGHLIGHT | "E-STOP stays live the whole time." | Timer/NEXT | no |
| 3 | RUN | Program list | HIGHLIGHT | "We'll build and run a program, live." | Timer/NEXT | no |
| 4 | — | ISO view | PRESS | Camera frames the robot from the hero angle. | 0.8 s | no |
| 5 | robot READY | SIM/REAL switch | **PRESS → REAL** | Toggle flips to REAL; the production **"SWITCH TO REAL" confirm** appears — the presenter confirms it. Status turns REAL. | BACKEND: mode == REAL (isSimulated false) | no |
| 6 | REAL | ENABLE JOG | **PRESS** | Button → green "JOG READY"; robot armed (only possible now, in REAL). | BACKEND: armed / 1 s | arm |
| 7 | — | Jog step | **PRESS** to a large step (≈15°) | Step reads 15°; "one press = a bold move." | Timer/NEXT | no |
| 8 | armed | Jog key A1+ | **PRESS ×3** | **Robot swings axis 1 through a big arc** (idle wait between each press — one 15° step is too small, so three build a bold move). | **BACKEND: robot idle** (per press) | YES |
| 9 | armed | Jog key A2+ | **PRESS ×3** | **Axis 2 lifts high** — three presses, clearly visible. | **BACKEND: robot idle** (per press) | YES |
| 10 | armed | Jog key A3+ | **PRESS ×2** | **Axis 3 reaches out** — the pose is now well away from home. | **BACKEND: robot idle** (per press) | YES |
| 11 | — | — | NARRATE "Reached entirely by hand — no coordinates typed." | Callout | Timer/NEXT | no |
| 12 | — | TEACH nav | **PRESS** | Left panel → TEACH tab. | 0.9 s | no |
| 13 | TEACH | + TEACH | **PRESS** | **A point appears in the program** at the reached pose. | BACKEND: program grew | no |
| 14 | — | GO HOME | **PRESS** | **Robot glides home.** | **BACKEND: robot idle** | YES |
| 15 | — | FILE nav | **PRESS** | Left panel → FILE (controller) page. | 0.9 s | no |
| 16 | FILE | Controller list | SELECT the signature demo row | The demo program is selected. | 0.9 s | no |
| 17 | FILE | LOAD | **PRESS** | **Program loads: steps fill and the whole path is drawn in the 3D scene** (existing trajectory palette — unchanged). | BACKEND: program loaded | no |
| 18 | — | RUN nav | **PRESS** | Back to the RUN tab. | 0.9 s | no |
| 19 | RUN | RUN | **PRESS** | **Robot executes the whole program at full speed; the trajectory traces live.** | **BACKEND: program finished** (latch) | YES |
| 20 | running | — | NARRATE "This is the real executor — the path draws as each step runs." | Callout during motion | rides #19 | — |
| 21 | finished | PRESENTATION | **PRESS** | HMI collapses to the clean 3D scene — signature finale. | Timer/NEXT | no |
| 22 | — | — | NARRATE "Real mode, jogged, taught, loaded, run — the whole workflow, live." | Callout | Timer/NEXT | no |

Real-motion steps: 8, 9, 10, 14, 19 — each waits on a backend condition, never a bare timer, so the
demo never races the robot. How a step *begins* depends on the mode: in **TRY (default)** the user
presses the real control; in **STEP** the presenter presses NEXT (the engine presses); the backend
wait then holds until the motion completes before the next press is accepted / offered.

**Step 5 wrinkle — the REAL switch has its own confirm dialog.** Pressing the SIM/REAL toggle to
REAL pops the production `ConfirmDialog` "SWITCH TO REAL" (`StatusBarPanel.cpp:383`); `modeChanged`
fires only after it is confirmed, and the toggle is enabled only while the robot is READY. This is
GOOD to show investors (real safety flow) but interacts with the demo mode:
- **TRY mode (the default):** the user presses the toggle and confirms the dialog themselves —
  natural and safe. This is the recommended way to run `LIVE TEACH & RUN`.
- **STEP mode:** the presenter confirms the dialog naturally — also fine.
- **PLAY (auto) mode:** the modal dialog stalls the auto-run. → `LIVE TEACH & RUN` is **not offered
  in PLAY**; PLAY stays for the no-motion TOUR (D6, now resolved by the TRY default).
- Preflight requires the robot READY so the toggle is enabled; otherwise abort "robot not ready to
  switch to REAL".

## 4. New engine capabilities to implement (P3/P4 pulled forward)

1. `PressTarget` allowed on motion controls (SIM/REAL toggle, jog keys, RUN, GO HOME), not just nav.
   A jog PressTarget carries a **repeat count** (press N times, WaitRobotIdle between each) so
   "×3" is one authored step, not three near-identical rows.
2. **WaitMode(REAL)** advance — hold until `top.mode` is REAL and `isSimulated == false` (the
   presenter has confirmed the production SWITCH-TO-REAL dialog); timeout → abort.
3. **WaitRobotIdle** advance — hold until `motion.robotMoving` rises then falls; 5 s timeout → abort.
4. **WaitProgramFinished** advance — latch `prog.isRunning` rising→falling; fault → abort with the
   backend diagnostic (never a false "finished").
5. **WaitProgramLoaded / program-grew** advances — `prog.loadedProgramName` set / step count rose.
6. **SelectListRow(name)** — typed panel accessor to select the controller demo row.
7. New guide targets + accessors: **SIM/REAL toggle** (`StatusBarPanel::modeToggle()`) and the jog
   **STEP** control; the jog ENABLE/key/HOME, RUN, +TEACH, nav and LOAD accessors already exist.
8. **Jog-step preset** — press the STEP control to a known large increment for a visible move.
9. **Motion confirm dialog + preconditions** wired before `start()` (robot READY, controller has
   the demo program, mode switchable).
10. **Abort battery** — E-Stop / fault / disconnect / overlay-open subscriptions during the run.
11. **TRY mode (pulled forward from P2, now the default):** the `GuideMode::Try` value + a third
    chip (default-checked); on a click step the runner connects to the target's real `clicked()`
    and WAITS instead of pressing; an idle timer escalates `hintText`; a **SHOW ME** button on the
    callout performs the press for the user. For a jog "×3" step, TRY waits for the user's real
    press, then WaitRobotIdle, three times. Applies guide-wide (TOUR too), per the boss default.

## 5. Setup prerequisites (outside the code)

- The controller holds the signature demo program (name TBD, e.g. `DEMO_HEXA_CONTOUR`). We save it
  there once during demo setup (controller store, which works).
- The pendant is in REAL mode with the HAL stub active before START (the scenario also checks and
  refuses otherwise).

## 6. Still open (small, non-blocking — pick during implementation)

- Exact axes / speed cap for RUN (proposal: 15° jog step with 2–3 presses per axis on A1/A2/A3,
  RUN at 100 %).
- The signature demo program's name and shape (proposal: `DEMO_HEXA_CONTOUR`, the hexagon).
- Auto-mode gating (§13.2): confirm dialog is deemed sufficient here since the HAL is a stub; if a
  real HAL is ever attached, revisit whether PLAY/STEP must be simulator-gated.
- **D6 — RESOLVED.** Default mode is TRY (hands-on). `LIVE TEACH & RUN` runs in **TRY (default) or
  STEP**, never PLAY (the REAL-switch modal stalls auto-run). PLAY stays for the no-motion TOUR.

## 7. Out of scope

TRY-mode hands-on jogging by the user (later); any new trajectory format; in-app authoring.
