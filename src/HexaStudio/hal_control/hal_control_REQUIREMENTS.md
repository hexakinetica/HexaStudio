# hal_control — module requirements

- **Module:** `src/HexaStudio/hal_control`
- **Side:** pendant (HexaStudio HMI), GUI thread. Qt Widgets only; no network, no RDT, no RobotService.
- **Status:** additive standalone module, runnable in isolation; NOT integrated into the HexaStudio app.
- **Origin:** split out of the `overlays` module by boss decision (2026-07-03): HAL is a
  direct-hardware commissioning panel with the largest intent contract of the project, its own
  bench-run value for MKS bring-up, and a real hardware domain boundary — the "one overlays module"
  rationale (avoid a lib per THIN overlay) does not apply to it.
- **Policy (per boss):** this module's own requirements live here. Cross-module interactions are listed
  under [§ Cross-module interactions](#cross-module-interactions) and belong to the upper-level files.

Version of this requirements set: **1.1**.

---

## Scope

In scope: a module-owned HAL runtime overlay (`HalPanel`) run standalone against an offline controller
emulation, driven exactly as `MainWindow` drives it. Out of scope: real motor commands, the Motor
Configurator link, the TCP HAL simulator process (shell-launched) and app integration — all
cross-module.

This is a **new-product** module: the shipping `HalOverlay` is NOT modified and NOT used. `HalPanel`
keeps the shipping overlay's contract (so it drops into the same MainWindow wiring) but evolves the
presentation independently. The module reuses the shared `styles` sources, font resources and the
overlays module's `OverlayWidgets` building blocks via reach-through (all `hexa_ui_kit` candidates),
until they are extracted in Phase B.

---

## Requirements

### HAL-REQ-0001 — Standalone, controller-free run
The HAL panel shall build and run in isolation from HexaStudio, the controller and the network, hosted
against `FakeHalController`. A launcher `run_hal_bench.bat` shall start it; `--selftest` shall wire
everything headless and exit 0 on success / 1 on failure. Direct standalone access matters
operationally: MKS bring-up must not require navigating the settings overlay.
`Verification: Review + Smoke (--selftest)` · `Status: Implemented`

### HAL-REQ-0002 — Every backend call is a typed intent signal
`HalPanel` keeps the shipping `HalOverlay` feedback surface (`setHalConfigCurrent(HmiHalConfig)`,
`setTcpSimulatorRunning(bool)`, robot status via `onRobotStateChanged(HmiRobotStatus)` — all PUSHED,
no singleton pull) and its intent signals (`closeRequested`, `applyHalRequested(HmiHalConfig)`,
`tcpHalSimulatorLaunchRequested/StopRequested`), and replaces every direct
`RobotService::instance()` call with a typed signal: `halConnectRequested(host, port)`,
`halDisconnectRequested()`, `halJogRequested(axis, stepDeg)`, `homingRequested(axis | -1)`,
`setZeroRequested(axis)`, `setZeroAllRequested()`, `clearErrorsRequested()`,
`jogArmRequested(armed)`, `eStopRequested()`. The bench wiring shall connect EVERY signal to a
consumer — an intent without a consumer is a dead control on the bench.
`Verification: Review` · `Status: Implemented`

### HAL-REQ-0003 — Safety behaviour preserved verbatim
The shipping HAL safety behaviour is preserved: panel-local jog arm (independent of the jog panel),
automatically DISARMED on hide; every blocked jog names its reason (bridge disconnected / disarmed /
axis homing / axis faulted); E-STOP is gated only on the bridge connection, never on the HAL link
state; per-axis telemetry stale detection (1.5 s) marks the axis title; SET ZERO ALL is one atomic
command (a per-axis loop would collapse in the one-shot command field); the "HAL drives REAL
hardware" warning badge shows while the bridge is up in Simulation view; the temporary MKS bring-up
command gate (bridge connected only) is kept 1:1 including the documented production policy to
restore after validation.
`Verification: Review + Smoke (jog gates asserted)` · `Status: Implemented`

### HAL-REQ-0004 — Restyled to the pendant theme
The shipping overlay's foreign palette (a 1:1 tcp_client_app copy: #1e1e1e, #58a6ff, ad-hoc grays) is
replaced by the Hexa theme: overlay cards (via the shared `OverlayWidgets` blocks), theme borders,
mono position values, and the shared badge palette for axis states (fault = error badge, homing =
warn, enabled = ok, else neutral). Structure (sidebar Connection/Jog-Step | Status, Global Actions,
Axes table with fixed columns) is kept.
`Verification: Review + Screenshot` · `Status: Implemented`

### HAL-REQ-0005 — FakeHalController mirrors the RobotService seam
`FakeHalController` shall serve a demo `HmiHalConfig` whose axis states cover the whole badge variety
(Enabled / Ready / Fault + protection code / Homing / Disabled) plus a demo robot status (bridge up,
REAL view), and consume every HalPanel intent with simple deterministic state changes: connect flips
the transport + HAL status, jog moves the joint and counts at the seam, homing activates the
supervisor mirror, clear-errors resets fault axes, E-Stop latches. Qt Core only.
`Verification: Review + Smoke` · `Status: Implemented`

### HAL-REQ-0006 — Bench renders app-faithful, supports design review
The bench host window shall paint the application background colour and load the application fonts.
`--selftest` shall assert the jog gates through the REAL widgets: jog blocked while disarmed (zero
commands at the seam), reaches the seam after arming (axis and step value checked), stays blocked on
a faulted axis even while armed; and the connect round-trip through the REAL button. The TCP-sim
running state shall round-trip so START/STOP flip like in the app. `--screenshot <file.png>` shall
render the connected panel with the axis badge variety and exit 0.
`Verification: Smoke (--selftest, --screenshot)` · `Status: Implemented`

### HAL-REQ-0007 — HAL is the calibration surface (calibration overlay cancelled)
Robot calibration is performed on the HAL panel — boss decision (2026-07-03): the shipping
CalibrationOverlay duplicated functions the HAL panel already provides and is NOT re-implemented in
the new product. Calibration workflow on this panel: per-axis homing / HOME ALL 1→6 (supervisor
progress mirrored in the status badges), per-axis SET ZERO / atomic SET ZERO ALL (mastering), and
fine discrete jog via the CUSTOM step (0.01–30 deg). Consequences elsewhere: the settings overlay
has no CALIBRATION entry; the jog panel's calibration step mode is removed.
`Verification: Review` · `Status: Implemented`

---

## Cross-module interactions

Not specified here (they cross the module boundary):

| Interaction | Belongs in (upper-level) |
|---|---|
| Intent signals → `RobotService` (`connectHalEndpoint`, `jogJointIncrementalHal`, `startHomingSequence`, `masterAxis`, `setZeroAll`, `clearError`, `setJogEnabled`, `setEStop`, HAL config apply) | HexaStudio app requirements, [[RobotService]] |
| Real motor execution, Motor Configurator link, owner arbitration | HexaMotion / HexaHAL |
| TCP HAL simulator process launch/stop | HexaStudio app requirements (shell) |
| Navigation from the settings overlay (`halOverlayRequested`) | HexaStudio app requirements (shell) |
| App integration: host `HalPanel` in `MainWindow`, remove the shipping `HalOverlay` | HexaStudio app requirements |

---

## Verification summary

GUI smoke: `hal_control_bench --selftest` (exit 0; asserts the jog gates and the connect round-trip
through the real widgets). Design render: `hal_control_bench --screenshot <file.png>`. Interactive:
`run_hal_bench.bat`. Module owns `HalPanel`; reuses styles / fonts / `OverlayWidgets` via
reach-through. Shipping `HalOverlay` untouched.
