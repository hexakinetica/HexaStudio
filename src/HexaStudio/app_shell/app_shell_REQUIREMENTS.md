# app_shell — module requirements (integration foundation)

- **Module:** `src/HexaStudio/app_shell`
- **Side:** pendant (HexaStudio HMI), GUI thread. Qt Widgets only; no network, no RDT, no RobotService.
- **Status:** additive standalone module; the assembled NEW-PRODUCT application running offline.
  NOT integrated with the shipping app (Phase C).
- **Origin:** boss directive (2026-07-03): prepare the integration foundation as a separate module
  with a strong architecture emphasis; small module-interface adjustments allowed for mismatches.
- **Layering exception (explicit):** this is the ONLY module allowed to depend on the feature
  modules — it is the shell layer above them (MODULARIZATION § 5), not a peer. Feature modules
  still never see each other or the backend type.

Version of this requirements set: **1.2**. Shell bench version: **0.1.0**; HexaStudioNG version: **0.2.0** (both printed at startup).

---

## Scope

In scope: the backend contract (`BackendClient`), the composition root (`ShellWindow`) assembling
all five feature modules, and a coherent offline controller (`FakeBackend`) so the WHOLE new-product
HMI runs, is smoke-tested and is design-reviewed without the shipping app, the controller or the
network. Also in scope since v1.1: `RobotServiceAdapter` (the production BackendClient over the shipping
RobotService) and the `HexaStudioNG` executable — the replacement application wired to the REAL
backend stack. Out of scope: the 3D viewport (deferred module) and the retirement of the shipping
HexaStudio (a boss release decision).

---

## Requirements

### SHL-REQ-0001 — The assembled application runs standalone
`app_shell_bench` shall build and run the COMPLETE new-product window (status bar, program editor,
viewport slot, jog panel, settings/diagnostics/HAL overlays) against `FakeBackend` — no RobotService,
no network, no shipping HexaStudio. `run_shell_bench.bat` launches it; the shell version is printed
at startup; `--selftest` exits 0/1; `--screenshot <file.png>` renders the assembled window plus
`<file>_settings.png` and `<file>_hal.png`.
`Verification: Smoke (--selftest, --screenshot)` · `Status: Implemented`

### SHL-REQ-0002 — BackendClient is THE backend contract
`BackendClient` (abstract QObject) is the single seam between the assembled HMI and the controller
side — the ONE sanctioned abstraction of the project (two real implementations: `FakeBackend` now,
`RobotServiceAdapter` at Phase C). Contract rules: the command surface mirrors the shipping
`RobotService` method names 1:1 (the adapter is mechanical forwarding, never a translation layer);
the feedback surface is PUSH-ONLY (implementations publish `robotStateChanged` / `configReceived` /
`halConfigChanged` / `programLoaded` / `remoteFileListReceived` / `tcpSimulatorStateChanged` /
`messageOccurred`; nobody pulls); ids not names cross the seam; only typed DTOs from
`BackendTypes.h` / `ProgramData.h`.
`Verification: Review` · `Status: Implemented`

### SHL-REQ-0003 — ShellWindow is the composition root and the ONLY mediator
Every panel <-> backend connection of the product lives in ONE method (`connectBackend`), readable
top-to-bottom, grouped per module (no hidden data flow — Mandate rule 6). Feature modules never see
each other or the backend type: the shell fans feedback out (one `robotStateChanged` feeds the
status bar, jog, program editor, diagnostics and HAL) and routes intents in. `connectBackend` is
called exactly once (asserted).
`Verification: Review` · `Status: Implemented`

### SHL-REQ-0004 — Shell-owned behaviours are explicit
The mediator behaviours the shipping MainWindow owned stay in the shell, explicit and visible:
name -> id mapping for jog/monitor context against the LAST RECEIVED config; the GO HOME
confirmation (pendant `ConfirmDialog`) plus the one-command PTP home program construction; the
E-Stop TOGGLE semantics for the top bar and diagnostics (reads the last received status) versus the
HAL panel's E-STOP ALL which always ENGAGES; overlay navigation (settings / diagnostics toggle /
settings -> HAL) with window-tracking resize; the application version fed to the status-bar About
box. The shell's copies of status/config are display state only — never a command source of truth.
`Verification: Review + Smoke` · `Status: Implemented`

### SHL-REQ-0005 — Deferred viewport is visible, not silent
The centre column carries an explicit placeholder naming the deferred `viewport3d` module and the
deferral decision. The program editor's view-toggle intents are consumed VISIBLY by the placeholder
(last intent shown) — no silently swallowed signals; they re-route to the viewport at its
integration.
`Verification: Review + Screenshot` · `Status: Implemented`

### SHL-REQ-0006 — FakeBackend is ONE coherent offline controller
The integration fake is a single state machine over one `HmiRobotStatus` / `HmiSystemConfig` world —
NOT a composition of the per-module fakes: a jog command must move the same joints the HAL panel and
the diagnostics observe. Behaviour: jog refused loudly while disarmed (the controller stays the
safety arbiter); program start refused while E-Stop is engaged; program execution stepped on a
deterministic tick; controller-storage emulation for the remote file operations; settings apply
echoes `configReceived` (re-publish semantics) and preserves the HAL runtime mirror; deterministic
sine stats jitter. The per-module fakes remain the right tool for the per-module benches.
`Verification: Review + Smoke` · `Status: Implemented`

### SHL-REQ-0007 — Integration selftest drives REAL widgets across module boundaries
`--selftest` shall assert, through real widgets end-to-end (panel -> shell wiring -> backend):
speed combo 25% reaches the backend override; jog is refused while disarmed and moves A1 by the
selected step after arming via the REAL JOG button; the settings overlay opens from the status bar,
an applied IP reaches the backend, CLOSE hides it; the top-bar E-Stop toggles engage/release and the
diagnostics history records the E-STOP events; the diagnostics card opens from an operator-style
click on the status indicator; settings -> HAL navigation works and an armed HAL jog moves the SAME
joint state (A1 to 2.0 after the pendant jog's 1.0). Headless note: overlay visibility is asserted
relative to the (unshown) window via `isVisibleTo`.
`Verification: Smoke (--selftest)` · `Status: Implemented`

### SHL-REQ-0008 — Boss-approved interface adjustments (record)
Adjustments made to module interfaces for the integration (boss allowance, 2026-07-03):
`JogPanel::stepChanged` REMOVED (zero consumers in the shipping MainWindow and all benches — dead
intent, Mandate rule 7; jog_control v1.9); objectNames added for the cross-module selftest
(`btnJogArm`, `jogPlus_N`/`jogMinus_N` on the jog pad; `btnSettings`, `lblSystemStatus` on the
status bar; `btnHalRuntime` on the settings overlay) — no behaviour changes.
`Verification: Review` · `Status: Implemented`

### SHL-REQ-0009 — RobotServiceAdapter: the production BackendClient
`RobotServiceAdapter` implements the contract over the shipping `RobotService` by mechanical
forwarding (the 1:1 command-name rule pays off: every override is one line). Three deliberate
non-trivial pieces, each documented in code: (1) `halConfigChanged` is re-published on every
`stateChanged` — the shipping service has no such signal and its HalOverlay PULLED the config; the
pull is CONTAINED in the adapter, consumers stay push-only. (2) Workstation-local endpoint
persistence (QSettings, shipping-compatible keys) is merged into every published config and written
back on `applySettings`, with reconnect ONLY on endpoint change — the exact shipping MainWindow
flow. (3) The adapter takes a `RobotService&` — it never touches the singleton.
`RobotService::trajectoryReceived` stays unconnected: its only consumer is the deferred viewport3d
module; the signal joins the contract together with that module.
`Verification: Review + Smoke (HexaStudioNG --selftest)` · `Status: Implemented`

### SHL-REQ-0010 — HexaStudioNG: the replacement application
`HexaStudioNG` is the new-product executable: composition-root duties only (meta types, fonts,
shared logger, version banner, resolving `RobotService::instance()` ONCE — the last singleton
touchpoint, dying with the shipping app; controller connect from workstation settings; the TCP HAL
simulator QProcess with shipping-compatible arguments, state pushed via
`notifyTcpSimulatorState`). It links the real backend stack (rdt_bridge / network_driver /
StateData / rdt_protocol / robot_model) and deliberately NOT Qt Quick/Quick3D (viewport deferred).
`--selftest` proves the real-backend assembly constructs and wires (exit 0). The shipping
HexaStudio executable stays untouched and buildable in parallel.
`Verification: Smoke (--selftest) + Review` · `Status: Implemented`

### SHL-REQ-0011 — End-to-end probe against a real controller
`HexaStudioNG --probe <seconds> [--screenshot <file.png>]` shall run the full application for the
given time and exit 0 only if the controller bridge is CONNECTED (1 otherwise), optionally saving a
render — CI-friendly evidence that the whole chain UI -> RobotServiceAdapter -> RobotService ->
RDT -> controller is alive. VALIDATED 2026-07-03 against a live local HexaCore (RDT server
127.0.0.1:30002): connect + full resync (config v3, program v1, trajectory v1) in under a second;
the UI showed SYSTEM READY, the REAL controller tool/base names (Flange/World) and the jog panel's
SIM-only gate (the controller starts in SIM) — production safety behaviour confirmed end-to-end.
`Verification: Smoke (--probe, exit code) + Screenshot` · `Status: Implemented + Validated locally`

---

## Cross-module interactions

| Interaction | Belongs in (upper-level) |
|---|---|
| Retiring the shipping HexaStudio executable in favour of HexaStudioNG | Boss release decision |
| Killing `RobotService::instance()` entirely (needs shipping-source edits) | Phase C proper |
| viewport3d module hosting + view-toggle re-routing | viewport3d integration (deferred) |
| `hexa_ui_kit` / `hexa_contracts` extraction (reach-through retirement) | Phase B |

---

## Verification summary

Integration smoke: `app_shell_bench --selftest` (exit 0; 5 cross-module UI-path assertions). Design
render: `app_shell_bench --screenshot <file.png>` (assembled window + settings + HAL frames).
Interactive: `run_shell_bench.bat` — the full new-product HMI offline. Shipping app untouched.
