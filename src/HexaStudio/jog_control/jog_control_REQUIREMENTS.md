# jog_control — module requirements

- **Module:** `src/HexaStudio/jog_control`
- **Side:** pendant (HexaStudio HMI), GUI thread. Qt Widgets only; no network, no RDT, no RobotService.
- **Status:** additive standalone module, runnable in isolation; NOT integrated into the HexaStudio app.
- **Policy (per boss):** this module's own requirements live here. Cross-module interactions are listed
  under [§ Cross-module interactions](#cross-module-interactions) and belong to the upper-level files.

Version of this requirements set: **1.9**.

---

## Scope

In scope: a module-owned jog panel (`JogPanel`) run standalone against an offline controller emulation,
driven exactly as `MainWindow` drives it. Out of scope: mapping jog intent to the wire, real
kinematics/limits, and app integration — all cross-module.

This is a **new-product** module: the shipping `PanelRight` is NOT modified and NOT used. `JogPanel` is
the module's own panel that keeps `PanelRight`'s exact intent/feedback contract (so it drops into the
same wiring) but evolves the layout independently. The tool/base picker is module-owned too
(`FramePickerDialog`; the shipping `FrameSelectionDialog` is not used). The module reuses only the
shared `styles` sources and font resources via reach-through, until they are extracted into
`hexa_ui_kit`.

---

## Requirements

### JOG-REQ-0001 — Standalone, controller-free run
The jog panel shall build and run in isolation from HexaStudio, the controller and the network, hosted
against `FakeJogController`. A launcher `run_jog_bench.bat` shall start it; `--selftest` shall wire
everything headless and exit 0.
`Verification: Review + Smoke (--selftest)` · `Status: Implemented`

### JOG-REQ-0002 — Intent out, status in (no direct backend)
The panel shall emit jog intent (`jogRequested`, `jogEnableRequested`, `coordSystemChanged`,
`goHomeRequested`, `jog/monitorContextChanged`) and consume feedback via slots (`updateState`,
`onConfigReceived`). `stepChanged` was REMOVED during integration-foundation work (boss-approved
interface adjustment, 2026-07-03): it had zero consumers in the shipping MainWindow and in every
bench — a dead intent (rule 7). The jog step stays panel-local (it parameterises `jogRequested`). Deliberate contract deviation from the shipping panel: `setCalibrationMode` and
the fine calibration step set (0.1/1/5 deg) are REMOVED — their only caller was the calibration
overlay, cancelled by the boss (2026-07-03); fine-step calibration jog lives on the HAL panel
(CUSTOM step down to 0.01 deg). It shall not reference `RobotService` or the network.
The bench wiring shall connect EVERY panel signal to a controller consumer — an intent without a
consumer is a dead control on the bench and is not acceptable.
`Verification: Review` · `Status: Implemented (JogPanel)`

### JOG-REQ-0005 — Layout evolves independently, safety behaviour preserved
`JogPanel` may change the layout freely (coordinate frame as a drop-down; arm state SIM/ENABLE/READY/
MOVING folded into the JOG button; jog pad as the elastic element) provided the safety-relevant
behaviour of the shipping panel is preserved: jog is discrete, armed only on REAL hardware, direction
buttons are gated while moving with an absolute watchdog release, and Cartesian orientation axes use a
separate degree step.
`Verification: Review` · `Status: Implemented`

### JOG-REQ-0003 — Feedback drives the UI, panel never branches on it
Motion status (`robotMoving`, `jogEnabled`, joints/TCP, tool/base, frame) shall be display-only: the jog
button gating (DISABLED / READY / MOVING) reflects reported motion; the panel never waits on feedback to
decide an action. The controller remains the final safety arbiter (existing ADR).
`Verification: Review` · `Status: Implemented`

### JOG-REQ-0004 — FakeJogController mirrors the RobotService seam
`FakeJogController` shall cover the panel's COMPLETE intent contract (jog step / enable / mode /
go-home / jog context / monitor context in; config + motion status out), emulate short motion so
gating is exercised, and provide a demo tool/base config for the context selectors. A jog step shall
move the vector the panel displays for the current frame (joints in JOINT, TCP in WORLD/TOOL), so the
desired-value display is exercised in every mode. The demo config shall include joint limits and joint
jog shall clamp against them (raising the transient limit notice), so the at-limit UI (JOG-REQ-0009)
is exercised offline. `setMonitorContext(toolId, baseId)` shall visibly change the monitor pose:
the demo composition is TRANSLATION-ONLY (`actualTcp + toolOffset - baseOffset`), deliberately NOT the
real frame algebra (that lives controller-side in `RobotService::calculateMonitorPose` on shared
`RDT::pose_math`; the bench must not duplicate kinematics). Name→id mapping helpers
(`toolIdByName`/`baseIdByName`) mirror the MainWindow lookup so the bench wiring keeps the app's
shape. `--selftest` shall drive the REAL monitor combos (found by objectName) through the full UI path
(panel -> wiring -> controller seam) and FAIL if the tool selection does not change the monitor pose.
Qt Core only (the fake); the UI-path assertion lives in the bench main.
`Verification: Review + Smoke (--selftest asserts the full UI path)` · `Status: Implemented`

### JOG-REQ-0006 — Desired value framed between the jog keys (per axis)
Each axis row of the jog pad shall show the DESIRED (commanded) value — `displayJoints` in JOINT mode,
`displayTcp` in WORLD/TOOL — in a framed, mono-font cell placed between the minus and plus jog keys.
The cell carries a violet left stripe (`Hexa::Colors::Active`) as the per-axis identity mark. The cell
shall be as narrow as the real worst-case values allow (`X -1234.56`, `Rx -179.99`, `A1 -170.00` in
15 px mono); the minus/plus keys absorb ALL remaining row width (the widest possible touch keys).
The value must stay readable in all panel states. The ACTUAL values are shown separately by the
passive monitor (JOG-REQ-0007).
`Verification: Review + Screenshot` · `Status: Implemented`

### JOG-REQ-0009 — At-limit highlight on the desired-value cell (joint space)
When a joint axis' displayed desired value sits at its configured limit
(`HmiSystemConfig::axisLimits`, band 0.05 deg), the cell background and border shall turn amber (the
panel's warning colour). Display-only: the controller remains the safety arbiter; axes without a
configured limit are never highlighted; Cartesian modes clear all highlights (axis limits are
joint-space; unreachable Cartesian targets arrive via `jogNotice`).
`Verification: Review + Screenshot` · `Status: Implemented`

### JOG-REQ-0010 — Module-owned frame picker (tool/base selection)
Tool/base selection shall use the module-owned `FramePickerDialog` (the shipping
`FrameSelectionDialog` stays untouched and unused). Presentation requirements: all frames in a flat
list (single tap selects and previews, double tap confirms; no combo box); the selected frame's offset
in an instrument-style grid — X/Y/Z (mm) left, Rx/Ry/Rz (deg) right, mono-font values right-aligned,
matching the monitor card; no derived/duplicated text rows (the old "Comment" mirrored the name);
frameless window on the application background, centred over the host. The call contract stays
identical to the shipping dialog (`setToolData`/`getSelectedToolId`, `setBaseData`/`getSelectedBaseId`)
so the panel call sites are drop-in. `--screenshot` shall render the picker as a third frame
(`<file>_tool_picker.png`) with a non-identity frame selected.
`Verification: Review + Screenshot` · `Status: Implemented`

### JOG-REQ-0007 — Passive monitor as a framed instrument card
The passive monitor (ACTUAL joints or monitor pose per the selected tool/base context) shall be a framed
Surface card with two label/value columns, mono-font values right-aligned, collapsible via its title
button. It shall remain visually distinct from the desired-value cells of the jog pad. The value labels
shall always match the selected monitor base: A1..A6 for JOINT (including the initial default),
X/Y/Z/A/B/C for Cartesian bases. The tool selector shall stay ENABLED in every state (a disabled
selector reads as a broken control): with base JOINT the selection is remembered and takes effect once
a Cartesian base is chosen. Collapsing the monitor shall NOT reflow the rest of the panel: the card
keeps its slot (retain-size-when-hidden), so the jog pad geometry is identical in both states.
`Verification: Review + Screenshot (open vs collapsed frames)` · `Status: Implemented`

### JOG-REQ-0008 — Bench renders app-faithful, supports design review
The bench host window shall paint the application background colour (`Hexa::Colors::Background`) and
load the application fonts, so the translucent panel is reviewed exactly as it will look in HexaStudio.
The bench window shall default to the right-panel column size MainWindow allocates (300 px wide).
`--screenshot <file.png>` shall render the panel into a PNG and exit 0 (design review without a manual
run); the staged state is armed (READY) with axis A1 driven into its +limit, so a single render
exercises the desired value, the axis stripe, the at-limit highlight and the limit notice. A second
frame `<file>_collapsed.png` shall be rendered with the monitor collapsed via its real title button,
so the no-reflow requirement (JOG-REQ-0007) is verifiable by comparing the two frames.
`Verification: Smoke (--screenshot) + Review` · `Status: Implemented`

---

## Interaction structure (programmatic interface)

The module interface is the `JogPanel` Qt signal/slot contract. The panel computes NO robot data; a
mediator owns all connections (`MainWindow` in the app, `wire()` on the bench):

```
   JogPanel (UI, no math)          mediator (MainWindow / wire)         controller seam
   ---------------------          -----------------------------        -----------------------------
   signals (intent, out):
     jogRequested(axis, incr)  ->  direct                          ->  jogJointIncremental
     jogEnableRequested(on)    ->  direct                          ->  setJogEnabled
     coordSystemChanged(mode)  ->  direct                          ->  setJogMode
     goHomeRequested()         ->  confirm dialog + PTP program    ->  startProgram
     jogContextChanged(T,B)    ->  name -> id lookup (config)      ->  setJogContext(toolId, baseId)
     monitorContextChanged(T,B)->  name -> id lookup (config)      ->  setMonitorContext(toolId, baseId)
   slots (feedback, in):
     updateState(status)       <-  direct                          <-  statusChanged(HmiMotionStatus)
     onConfigReceived(config)  <-  direct                          <-  configReceived(HmiSystemConfig)
```

Context signals carry user-facing NAMES; the mediator maps them to ids — the panel never holds the
authoritative tool/base tables.

**Where and why the monitor data is computed** (production path, `RobotService` status tick):

| Status field | Source | Computed as | Shown by |
|---|---|---|---|
| `actualJoints` | feedback `joint_actual` | raw telemetry | monitor, base = JOINT |
| `monitorPose` | feedback `cartesian_actual` (world flange) | `calculateMonitorPose(actual, monitorToolId, monitorBaseId)` = `T_base⁻¹·T_flange·T_tool` | monitor, Cartesian base |
| `displayJoints` | command `joint_target` | raw commanded target | jog pad, JOINT mode |
| `displayTcp` | command `cartesian_target` (world flange) | `calculateMonitorPose(command, jogToolId, jogBaseId)` | jog pad, WORLD/TOOL mode |

Rationale (existing ADRs, `RobotService.cpp`):
- **Frame math is client-side** (`RobotService::calculateMonitorPose` on shared `RDT::pose_math`, the
  same source of truth as HexaMotion's FrameTransformer): changing the monitor Tool/Base re-expresses
  the already-received world-flange telemetry locally, with no controller round-trip. A hand-rolled
  matrix mirror was removed earlier to eliminate divergence risk — do not re-add math elsewhere.
- **Jog pad shows COMMAND, monitor shows ACTUAL**: the commanded target updates instantly on a jog
  click (perceived responsiveness), while actual telemetry catches up physically. Command and
  feedback are separate vectors by design and must never be merged.
- **The panel selects and formats, nothing more**: `updateState` picks which status vector to show
  and renders it; the only panel-side computation is the display-only at-limit comparison
  (JOG-REQ-0009) against `HmiSystemConfig::axisLimits`.

## Cross-module interactions

Not specified here (they cross the module boundary):

| Interaction | Belongs in (upper-level) |
|---|---|
| Panel jog intent → `RobotService::jogJointIncremental` / `setJogEnabled` / `setJogMode`; go-home; tool/base name→id mapping | `requirements/…/HexaStudio/PanelRight.md`, [[RobotService]] |
| Real jog execution, kinematics, axis limits, motion ownership | HexaMotion / `RDT_Protocol` |
| App integration: host `PanelRight` in `MainWindow`, wire to `RobotService` | HexaStudio app requirements |

---

## Verification summary

GUI smoke: `jog_control_bench --selftest` (exit 0). Design render: `jog_control_bench --screenshot
<file.png>` (exit 0, PNG written). Interactive: `run_jog_bench.bat`.
Module owns `JogPanel` and `FramePickerDialog`; reuses only styles / shared font resources via
reach-through. Shipping `PanelRight` and `FrameSelectionDialog` untouched.
