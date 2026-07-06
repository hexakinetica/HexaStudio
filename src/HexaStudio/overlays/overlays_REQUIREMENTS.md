# overlays — module requirements

- **Module:** `src/HexaStudio/overlays`
- **Side:** pendant (HexaStudio HMI), GUI thread. Qt Widgets only; no network, no RDT, no RobotService.
- **Status:** additive standalone module, runnable in isolation; NOT integrated into the HexaStudio app.
- **Scope note:** settings and diagnostics overlays done — the module is COMPLETE. The HAL
  runtime was SPLIT OUT into the `hal_control` module (boss decision 2026-07-03): HAL is a
  direct-hardware domain with the largest intent contract, not a thin overlay — the "one overlays
  module" rationale applies to thin overlays only.
- **Policy (per boss):** this module's own requirements live here. Cross-module interactions are listed
  under [§ Cross-module interactions](#cross-module-interactions) and belong to the upper-level files.

Version of this requirements set: **1.4**.

---

## Scope

In scope: module-owned settings and diagnostics overlays (`SettingsPanel`,
`DiagnosticsPanel` + the widget-free `DiagnosticsLog` view-model) run standalone against an offline
controller emulation, driven exactly as `MainWindow` drives them. Out of scope: the HAL runtime
(the separate `hal_control` module), real config persistence and app integration — all
cross-module. The calibration overlay is CANCELLED (boss decision 2026-07-03): calibration is
performed on the HAL panel.

This is a **new-product** module: the shipping `SettingsOverlay` and `DiagnosticsOverlay` are NOT
modified and NOT used. `SettingsPanel` and `DiagnosticsPanel` keep the shipping contracts (so they
drop into the same MainWindow wiring) but evolve the presentation and behaviour independently. `OverlayWidgets` (card / field style / caption) is owned
here and shared with `hal_control` via reach-through (hexa_ui_kit candidate). The module reuses only the shared
`styles` sources and font resources via reach-through, until they are extracted into `hexa_ui_kit`.

---

## Requirements

### OVL-REQ-0001 — Standalone, controller-free run
The settings overlay shall build and run in isolation from HexaStudio, the controller and the network,
hosted against `FakeOverlaysController`. A launcher `run_overlays_bench.bat` shall start it;
`--selftest` shall wire everything headless and exit 0 on success / 1 on failure.
`Verification: Review + Smoke (--selftest)` · `Status: Implemented`

### OVL-REQ-0002 — Intent out, config in (no direct backend)
The overlay shall emit intent (`closeRequested`, `applyRequested(HmiSystemConfig)`,
`halOverlayRequested`) and consume the config via the `setConfig` slot. Deliberate contract
deviation from the shipping overlay: `calibrationOverlayRequested` is dropped — the calibration
overlay was cancelled (boss decision 2026-07-03; calibration happens on the HAL panel). It shall not reference `RobotService` or the network. The bench wiring shall connect EVERY
overlay signal to a consumer — an intent without a consumer is a dead control on the bench.
`Verification: Review` · `Status: Implemented`

### OVL-REQ-0003 — Config is pushed, never pulled
The host pushes the config (`setConfig`); the overlay shall not pull it (the shipping overlay calls
`RobotService::instance()->getConfig()` in `showEvent` — that singleton reach is removed). Editors
stage changes in a private copy; APPLY emits the staged copy; the authoritative config returns via
`setConfig` (backend echo). Studio-local endpoint persistence (QSettings) is NOT read in the module:
merging workstation-local values into the config is the shell's job (cross-module item).
`Verification: Review + Smoke (apply round-trip asserted)` · `Status: Implemented`

### OVL-REQ-0004 — Real categories only; cross-overlay links are explicit buttons
The category list shall contain ONLY rows that open an editor page (NETWORK, AXIS LIMITS, TOOLS,
BASES, ROBOT VISUAL — a typed enum, no magic row numbers). The HAL runtime is a separate overlay and
shall be reachable via an explicit, labelled navigation button under the list — never via a fake list
row that bounces the selection back (the shipping "trampoline" rows). There is NO calibration entry:
the calibration overlay was cancelled (see OVL-REQ-0002).
`Verification: Review + Screenshot` · `Status: Implemented`

### OVL-REQ-0005 — Themed editors (one visual language, monochrome)
Every editor page shall use the shared building blocks: an inset section card with a header-styled
title (no naked `QGroupBox`), ONE shared field style for all inputs (dark inset, theme border, white
mono value — no ad-hoc inline widget styles), and a one-line muted caption describing the page (the
fixed INFO side column of the shipping overlay is removed). Axis limits shall be ONE aligned MIN/MAX
table, not six group boxes. Monochrome per the standing boss decision — no new colour coding.
`Verification: Review + Screenshot` · `Status: Implemented`

### OVL-REQ-0006 — Tool/Base frame editors preserve the shipping contract
The Tool/Base editors shall keep the shipping behaviour: 10 id slots in a combo, CREATE for an empty
slot, name + X/Y/Z (mm) / Rx/Ry/Rz (deg) offsets for an existing frame, DELETE for every frame except
id 0 (the identity default). One shared implementation for the structurally-identical Tool/Base lists.
`Verification: Review + Screenshot (GRIPPER slot rendered)` · `Status: Implemented`

### OVL-REQ-0007 — FakeOverlaysController mirrors the RobotService seam
`FakeOverlaysController` shall serve a demo config that populates every editor page (tools, bases,
limits, network, robot visual) and consume `applyRequested`, echoing the applied config back via
`configReceived` the way the real backend re-publishes settings after a controller acknowledge. For
the diagnostics seam it shall serve a demo `HmiRobotStatus` (healthy baseline, HAL NotConnected)
and deterministic scenario injectors (`injectHalWarning`, `injectSafetyFault`, `recoverAll`,
`clearError`, `toggleEStop`) — no randomness. (The HAL runtime seam lives in the hal_control
module's `FakeHalController`.) Qt Core only.
`Verification: Review + Smoke` · `Status: Implemented`

### OVL-REQ-0008 — Bench renders app-faithful, supports design review
The bench host window shall paint the application background colour and load the application fonts.
`--selftest` shall edit the network IP through the REAL editor widget, click the REAL APPLY button,
assert the value reaches the controller seam, and cycle every category row (editor rebuild path).
`--selftest` shall additionally assert the diagnostics behaviour through the REAL widgets:
`healthForStatus("NotConnected")` is not Healthy; RESET ERROR disarmed with no fault and armed on
fault; a safety fault produces log events mirrored in the list; RESET ERROR reaches the controller
seam; the E-Stop button toggles engage/release; CLEAR LOG empties the view log only. Interactive
`--diagnostics` shows the diagnostics panel with a deterministic cycling fault scenario.
`--screenshot <file.png>` shall render four frames: NETWORK (default), `<file>_limits.png`,
`<file>_tools.png` (GRIPPER slot selected) and `<file>_diag.png` (staged fault history) and exit 0.
`Verification: Smoke (--selftest, --screenshot)` · `Status: Implemented`

### OVL-REQ-0009 — Diagnostics: same contract, pushed status
`DiagnosticsPanel` keeps the shipping `DiagnosticsOverlay` contract (drop-in):
`updateStatus(HmiRobotStatus)` in; `clearErrorRequested` / `eStopRequested` / `closeRequested` out.
The E-Stop button mirrors the top-bar toggle semantics (engage/release, text flips to RESET E-STOP).
RESET ERROR shall be armed ONLY while a controller error is latched (the shipping overlay showed a
permanently red button).
`Verification: Review + Smoke (reset gating asserted)` · `Status: Implemented`

### OVL-REQ-0010 — DiagnosticsLog: widget-free history view-model
Diagnostics history lives in the Qt-Core-only `DiagnosticsLog` (same pattern as program_editor's
ProgramBuilder): it consumes `HmiRobotStatus` snapshots and records state TRANSITIONS as typed,
timestamped events (bridge connection, E-Stop edges, latched faults incl. the failing axis,
planner/safety/interpolator/HAL status changes, program start/stop, telemetry gaps). Behaviour
contract: the FIRST snapshot is adopted silently as the baseline EXCEPT already-active problems
(engaged E-Stop, latched fault, disconnected bridge — logged immediately); the log is bounded
(200 events, oldest dropped) and NEVER clears itself. Severity and per-subsystem health are typed
enums classified in ONE function (`healthForStatus`); "NotConnected" and empty classify as Inactive
— a dead link must never render healthy (fixed shipping bug); UNKNOWN status text classifies as
Error by policy (an unrecognised controller state must alarm, not blend in).
`Verification: Review + Smoke (classification + event assertions)` · `Status: Implemented`

### OVL-REQ-0011 — Diagnostics behaviour: annunciator, stale watchdog, explicit actions
The panel shows a SUBSYSTEMS annunciator (BRIDGE / PLANNER / SAFETY / INTERP / HAL badges, shared
badge palette), an alert strip with the decoded fault message (failing axis named), and the EVENT
LOG (newest first, mono, severity-coloured). Telemetry stale watchdog: if no status arrives within
1.5 s, every badge drops to "---", the message says exactly that, and the gap is recorded in the
log (once per episode; a never-started stream is not a gap). CLEAR LOG is a VIEW-LOCAL action and
never touches controller latches; RESET ERROR is the controller intent.
`Verification: Review + Screenshot + Smoke` · `Status: Implemented`

---

## Cross-module interactions

Not specified here (they cross the module boundary):

| Interaction | Belongs in (upper-level) |
|---|---|
| `applyRequested` → RobotService settings write / controller acknowledge / re-publish | HexaStudio app requirements, [[RobotService]] |
| Studio-local endpoint persistence (QSettings merge before `setConfig`) | HexaStudio app requirements (shell) |
| `halOverlayRequested` / `calibrationOverlayRequested` → shell overlay navigation | HexaStudio app requirements |
| App integration: host `SettingsPanel` in `MainWindow`, remove the shipping `SettingsOverlay` | HexaStudio app requirements |

---

## Verification summary

GUI smoke: `overlays_bench --selftest` (exit 0; asserts the apply round-trip through the real editor
widgets and cycles every category). Design render: `overlays_bench --screenshot <file.png>` (three
frames). Interactive: `run_overlays_bench.bat`. Module owns `SettingsPanel`; reuses only styles /
shared font resources via reach-through. Shipping `SettingsOverlay` untouched.
