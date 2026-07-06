# HexaStudio Modularization — Handoff & Roadmap

> **STATUS (2026-07-06, NG 0.6.0): the modularization is COMPLETE.** The shipping HexaStudio is
> ARCHIVED to repo `legacy/HexaStudio/` (sources, CMake target, PanelView3D.qml, meshes, HANDOVER);
> `HexaStudioNG` is THE product, assembled purely from the module graph with ZERO `src/` reach-through
> (ProgramDelegate/ProgramModel are owned by `program_editor`; theme via `hexa_ui_kit`; DTOs via
> `hexa_contracts`; backend via `hexa_backend`; 3D via `viewport3d`).


**Audience:** the team taking over the HexaStudio panel-modularization effort. Start with
`HANDOVER.md` (executive summary + prioritized backlog); this file is the detailed reference.
**Status:** Phase A complete (five feature modules) + the app_shell integration foundation: the assembled new-product HMI runs offline, and `HexaStudioNG` is E2E-validated against a live local HexaCore. NOT integrated with the shipping app (Phase C pending).
**Working branch:** `fix/mks-audit-batch` (earlier module states committed; the latest session's work is uncommitted — see [§9](#9-open-items--handoff-notes)).

---

## 1. What this effort is

We are rebuilding the HexaStudio HMI as a set of **independent, bench-runnable modules** (a *new product*).
Each panel becomes its own module in `src/HexaStudio/<module>/`, buildable and runnable standalone via a
`.bat`, driven by an offline `Fake*Controller` instead of the real backend. The **shipping HexaStudio
release is not modified** during this phase; integration is a later, explicitly-approved step.

The first module, `program_editor`, is the proven template. `jog_control` is the second, built to the
same recipe.

---

## 2. Ground rules (locked decisions from the boss)

1. **New product; do NOT touch the shipping app.** No edits to `src/HexaStudio/src/**` (MainWindow,
   PanelLeft/PanelRight, RobotService, overlays). Modules are additive folders only.
2. **Each panel is re-implemented in its module** (a module-owned panel), **not** a thin wrapper around
   the shipping panel. `program_editor/ProgramEditorPanel` replaces `PanelLeft`; `jog_control/JogPanel`
   replaces `PanelRight`. Full architectural freedom, as long as it satisfies the Simplicity Mandate.
3. **Keep the same signal/slot contract** the shipping panel exposes, so the module panel drops into the
   existing `MainWindow` wiring unchanged at integration (see [§8](#8-panel--backend-contracts)).
4. **Shared widgets and DTOs come from the module libs** (`hexa_ui_kit` for theme/widgets/keyboard,
   `hexa_contracts` for `BackendTypes.h`/`ProgramData.h`). Reach-through into `src/` is CLOSED
   (v0.2.7); the only remaining exception is the legacy `ProgramDelegate`/`ProgramModel` compile
   units, documented in the CMake files that carry them. Do not fork shared widgets.
5. **Documentation policy:** a module's *own* requirements live inside the module
   (`<module>_REQUIREMENTS.md`, filename contains the module name). *Cross-module interactions* are only
   referenced from there and belong to the upper-level requirement files.
6. **Simplicity Mandate (project CLAUDE.md):** remove-or-leave-alone; no speculative generality; no
   interface for a single implementation (the backend contract is the one justified abstraction —
   RobotService + Fake); typed `RDT::Result` errors, **no** `std::optional`/`std::variant`/exceptions in
   public surfaces; no magic numbers/strings; safety explicit and diagnosable.
7. **The calibration overlay is CANCELLED for the new product** (boss, 2026-07-03): its functions
   (mastering/set-zero, homing) already exist on the HAL panel — calibration is performed in
   `hal_control` (per-axis homing/HOME ALL, SET ZERO (ALL), fine CUSTOM jog step 0.01–30 deg).
   The shipping `calibrationoverlay` stays untouched in the shipping shell only.

---

## 3. The module recipe (how to create the next one)

A module folder `src/HexaStudio/<module>/` contains:

```
<module>/
├─ CMakeLists.txt                 # a <module>_bench exe target (+ optional gtest targets)
├─ <Module>Panel.{h,cpp}          # module-owned panel: same signal/slot contract, evolved freely
├─ run_<module>_bench.bat         # launcher (copy program_editor/run_panel_bench.bat, change exe name)
├─ <module>_REQUIREMENTS.md       # module requirements (filename must contain the module name)
├─ bench/
│  ├─ Fake<X>Controller.{h,cpp}   # offline emulation of the RobotService seam the panel needs
│  └─ <module>_bench_main.cpp     # hosts the panel + Fake, wired the way MainWindow will; --selftest
└─ test/                          # optional gtest for widget-free logic (Qt Core only)
```

- **CMake:** `add_executable(<module>_bench ...)` compiling the module panel + `bench/*` + reach-through
  shared sources (`../src/styles/HexaWidgets.*`, `../src/panels/.../<Dialog>.*` as needed). Link
  `Qt6::Core Qt6::Widgets`. No network, no RobotService, no HexaStudio.
- **Register** with ONE additive line in the root `CMakeLists.txt` inside `if(HEXA_BUILD_STUDIO)`:
  `add_subdirectory("${HEXA_SOURCE_DIR}/HexaStudio/<module>")`.
- **`--selftest`** in `<module>_bench_main.cpp` wires everything headless, exercises one intent, prints
  `... --selftest exit code: 0`, and returns 0. Used as a CI smoke check (`QT_QPA_PLATFORM=offscreen`).
- **Fake controller** mirrors only the subset of the RobotService surface the panel needs (intent slots
  in, status/config signals out), emulates enough behaviour to exercise the UI (e.g. short motion so a
  MOVING→READY indicator flips). Qt Core only; deterministic (tests can drive it directly).

See [§7](#7-build--run) for build/run commands.

---

## 4. What has been done

### 4.1 Module #1 — `program_editor` (replaces PanelLeft)

Standalone program/trajectory authoring module. **Complete and proven in isolation.**

| File | Role |
|---|---|
| `ProgramBuilder.{h,cpp}` | Single authoring authority. Owns `QVector<ProgramCommand>`; validated construction/edit; typed `RDT::Result<…, ProgramError>`; canonical `k*` param keys; bounded snapshot undo/redo; `validate()`; `ProgramResetReason` (ExternalLoad vs LocalEdit) on `programReset`. Qt Core only. |
| `ProgramEditorModel.{h,cpp}` | Thin `QAbstractListModel` adapter over the builder. Role values **derived** from `panels/left/ProgramModel::Roles` (no hand-synced duplicate) + `IssueRole`; `IssueMarker` enum for severity. |
| `ProgramEditorPanel.{h,cpp}` | Builder-based, RobotService-free panel. RUN gate (blocks RUN on Error issues), undo/redo + reorder + dirty marker, magic-string-free, `NavId/MainPage/EditPage` enums, property-based CMD dispatch. |
| `ProgramIssueDelegate.{h,cpp}` | Extends the production `ProgramDelegate` to draw a validation marker from `IssueRole` (guarded so the shipping delegate/PanelLeft are unaffected). |
| `bench/FakeController.{h,cpp}` | Offline HexaMotion emulation (run/pause/resume/stop, execution line, remote file list); tick-driven, deterministic. |
| `bench/panel_bench_main.cpp` | Hosts the panel + FakeController wired like MainWindow; `--selftest`. |
| `test/*` | gtest: builder (21), model (5), fake controller (5) = **31**, all green. |

> `ProgramStorage` (pendant-local JSON persistence, WORKSTATION tab) was removed 2026-07-06 by boss
> directive: the controller is the ONLY program store (`programs_dir` in
> `hexacore_runtime_config.json`); the pendant lists/loads/saves programs exclusively over the
> backend seam.

Delivered feature increment (the "§1 increment"): author-side RUN gate; reset-origin signal so undo/redo
re-upload while controller loads do not; undo/redo/reorder UI + dirty marker; validation-severity marker;
magic-string cleanup. Plus a code-review pass that removed dead APIs (`rename`, `hasCopy`), made
`addMotionPoint` validate (reject pose-less) and `addComment` return a plain index, unified the role
contract, and named the severity ints.

### 4.2 Module #2 — `jog_control` (replaces PanelRight)

Standalone jog panel. **Builds, `--selftest` exits 0, runnable via bat. Design approved by the boss
(2026-07-02, requirements v1.7).**

| File | Role |
|---|---|
| `JogPanel.{h,cpp}` | Module-owned jog panel; same contract as `PanelRight`. Layout evolved: coordinate frame (JOINT/WORLD/TOOL) as a drop-down; arm state (SIM/ENABLE/READY/MOVING) folded into the JOG button; full-width pendant-style jog pad `[-] [axis \| desired value] [+]` — the DESIRED (commanded) value framed in mono font between wide jog keys, violet identity stripe per axis, amber cell highlight when a joint axis sits at its configured limit (from `HmiSystemConfig::axisLimits`, display-only, joint-space); passive monitor as a framed Surface card with right-aligned mono values and labels that always match the selected base; collapsing the monitor keeps its slot (retain-size-when-hidden) so the jog pad never reflows. Safety preserved verbatim: discrete jog, armed only on REAL, anti-stick button gating with 2000 ms watchdog, separate Cartesian orientation step. |
| `FramePickerDialog.{h,cpp}` | Module-owned tool/base picker (shipping `FrameSelectionDialog` unused): flat list (tap = select + preview, double-tap = confirm), offset preview as an instrument grid (X/Y/Z mm left, Rx/Ry/Rz deg right, mono), no fake "Comment" row, frameless on the app background. Same call contract as the shipping dialog (drop-in). |
| `bench/FakeJogController.{h,cpp}` | Offline jog controller covering the panel's COMPLETE intent contract: jog step/enable/mode/home + jog/monitor context in; motion status + demo tool/base config out; emulates short motion so gating flips. A jog step moves the frame-appropriate vector (joints in JOINT, TCP in WORLD/TOOL); joint jog clamps at demo limits and raises the limit notice; `setMonitorContext` recomputes a demo monitor pose (translation-only `actualTcp + tool − base`; real algebra stays controller-side in `RobotService::calculateMonitorPose`). `toolIdByName`/`baseIdByName` mirror MainWindow's name→id lookup. |
| `bench/jog_bench_main.cpp` | Hosts `JogPanel` + FakeJogController wired like MainWindow — every panel signal has a consumer (context signals name-mapped exactly like the MainWindow lambdas); `--selftest` drives the REAL monitor combos through the full UI path and fails if the tool selection does not change the monitor pose; `--screenshot <file.png>` renders a staged state (armed + A1 at its limit) plus `<file>_collapsed.png` (monitor collapsed) and `<file>_tool_picker.png` (module picker with GRIPPER selected) and exits (design + layout-stability review). |

Note: `JogPanel` keeps `WA_TranslucentBackground` (paints no background of its own) — both hosts
(MainWindow and the bench) paint the application background colour.

### 4.3 Module #3 — `status_bar` (replaces PanelTop)

Standalone top status bar. **Builds, `--selftest` exits 0, runnable via bat.**

| File | Role |
|---|---|
| `StatusBarPanel.{h,cpp}` | Module-owned status bar; same contract as `PanelTop` (`modeChanged`/`eStopRequested`/`settingsRequested`/`speedChanged`/`diagnosticsRequested` out; `updateState(HmiTopStatus, isMoving)` in). NO RobotService singleton: About-box data via new `onConfigReceived(HmiSystemConfig)` slot + `setAppVersion` (host-owned). Honest stats: monochrome framed instrument cells for CPU/TEMP/NETWORK only (no colour coding — boss decision); VOLTAGE/CYCLE TIME not displayed until the DTO reports them (shipping panel hardcodes placeholders). Control-group gaps are fixed spacer items (part of the layout minimum — never collapse on resize/DPI change). Narrowable to the honest content minimum: no fixed left-section width (shipping reserved 400 px), only the VISIBLE centre-stack page contributes to the minimum (hidden pages get the Ignored size policy), and buttons use bounded-shrink widths (SETTINGS/STATS 84→64, E-STOP 120→104). The system-stats page is named STATS, not "monitor" (the jog panel owns the position monitor). Safety verbatim: REAL-mode confirm, switch locked while moving/E-STOP/disconnected, speed >50 % confirm with revert, E-STOP/RESET danger styling, traffic-light precedence. |
| `ConfirmDialog.{h,cpp}` | Module-owned pendant-styled safety confirmation (replaces `QMessageBox::question`): frameless on app background, warning-coloured title/border, CANCEL = focused safe default, danger-styled confirm with an explicit action label ("SWITCH TO REAL", "APPLY 75%"). |
| `bench/FakeTopController.{h,cpp}` | Offline controller: mode/speed/E-Stop in; `HmiTopStatus` + isMoving + demo config out; deterministic sine jitter animates CPU/TEMP/NETWORK (reproducible renders). |
| `bench/status_bar_bench_main.cpp` | Hosts `StatusBarPanel` + FakeTopController wired like MainWindow (E-Stop toggle lambda mirrored; settings/diagnostics acknowledged on the console); `--selftest` asserts the speed UI path and the E-Stop→RESET feedback path; `--screenshot <file.png>` renders controls / `<file>_stats.png` / `<file>_estop.png` / `<file>_confirm.png` (safety dialog) / `<file>_narrow.png` (minimum-width check). |

### 4.4 Module #4 — `overlays` (settings + diagnostics; replaces SettingsOverlay + DiagnosticsOverlay)

Standalone settings + diagnostics overlays. **Builds, `--selftest` exits 0, runnable via bat.
Module COMPLETE.** The HAL runtime was split out into the `hal_control` module (§ 4.5, boss
decision 2026-07-03).

| File | Role |
|---|---|
| `SettingsPanel.{h,cpp}` | Module-owned settings overlay; same contract as the shipping `SettingsOverlay` (`closeRequested`/`applyRequested(HmiSystemConfig)`/`halOverlayRequested`/`calibrationOverlayRequested` out; `setConfig` in). NO singleton: config is pushed (the shipping overlay pulled `RobotService::instance()` in showEvent); the QSettings endpoint merge is left to the shell. Real categories only (typed enum) — HAL/CALIB are explicit nav buttons, not trampoline list rows. Themed editors: inset section cards (no naked QGroupBox), ONE shared dark-mono field style for all inputs, one-line captions instead of the fixed INFO column, axis limits as ONE aligned MIN/MAX table. Tool/Base editors keep the shipping slot contract (10 slots, CREATE/DELETE, id 0 undeletable). |
| `DiagnosticsLog.{h,cpp}` | Widget-free diagnostics view-model (Qt Core): consumes HmiRobotStatus snapshots, records state TRANSITIONS as typed timestamped events (bridge/E-Stop/faults/subsystems/program/telemetry gaps); bounded 200; baseline adopted silently except already-active problems; ONE typed classification (`healthForStatus`) — NotConnected/empty = Inactive (never healthy — fixed shipping bug), unknown text = Error by policy. |
| `DiagnosticsPanel.{h,cpp}` | Module-owned diagnostics card; same contract as the shipping DiagnosticsOverlay (`updateStatus` in; `clearErrorRequested`/`eStopRequested`/`closeRequested` out). SUBSYSTEMS annunciator (badge palette, full names), decoded-fault alert strip, EVENT LOG (newest first, severity-coloured) backed by DiagnosticsLog, telemetry stale watchdog (badges drop to "---", gap logged once per episode), RESET ERROR armed only while latched, CLEAR LOG view-local. |
| `OverlayWidgets.{h,cpp}` | Shared themed building blocks (inset section card, dark mono field style, muted caption); owned here, shared with `hal_control` via reach-through — hexa_ui_kit candidate. |
| `bench/FakeOverlaysController.{h,cpp}` | Offline controller: serves a demo config populating every page; `applyConfig` stores and echoes `configReceived` (backend re-publish semantics). |
| `bench/overlays_bench_main.cpp` | Hosts `SettingsPanel` + FakeOverlaysController wired like MainWindow; `--selftest` edits the network IP through the REAL widgets, clicks APPLY and asserts the seam, then cycles all categories; `--screenshot <file.png>` renders NETWORK / `<file>_limits.png` / `<file>_tools.png`. |

### 4.5 Module #5 — `hal_control` (replaces HalOverlay)

Standalone HAL runtime / commissioning panel. **Builds, `--selftest` exits 0, runnable via bat.**
Split out of `overlays` (boss decision 2026-07-03): a direct-hardware domain with the largest intent
contract of the project and its own bench-run value for MKS bring-up.

| File | Role |
|---|---|
| `HalPanel.{h,cpp}` | Module-owned HAL runtime overlay; keeps the shipping `HalOverlay` feedback surface (`setHalConfigCurrent`/`setTcpSimulatorRunning`/status pushed) and intent signals, and replaces every `RobotService::instance()` call with a typed intent signal (connect/disconnect, HAL jog, homing, set-zero(-all), clear-errors, jog-arm, E-Stop). Safety verbatim: panel-local jog arm disarmed on hide, jog gates with named reasons, E-STOP gated only on the bridge, stale-row detection, atomic SET ZERO ALL, "HAL drives REAL hardware" warning, MKS bring-up gate kept 1:1 incl. the documented production policy. Restyled from the foreign tcp_client_app palette to the Hexa theme (overlay cards via reach-through `OverlayWidgets`, badge palette for axis states, mono positions). |
| `bench/FakeHalController.{h,cpp}` | Offline controller: demo axis-state badge variety + robot status; deterministic handlers for every HalPanel intent; jog counters for the gate assertions. |
| `bench/hal_control_bench_main.cpp` | Hosts `HalPanel` + FakeHalController wired like MainWindow; `--selftest` asserts the jog gates (blocked disarmed, seam after arm with axis+step checked, blocked on faulted axis) and the connect round-trip; `--screenshot <file.png>` renders the connected panel. |

### 4.6 Module #6 — `app_shell` (integration foundation; the assembled new product)

The composition root + THE backend contract. **Builds, `--selftest` exits 0, runnable via bat: the
COMPLETE new-product HMI runs offline.** Boss directive 2026-07-03: integration foundation as a
separate module, architecture-first; small module-interface adjustments allowed (record in
SHL-REQ-0008: JogPanel::stepChanged removed as a dead intent; selftest objectNames added).

| File | Role |
|---|---|
| `BackendClient.h` | THE backend contract (abstract QObject) — the one sanctioned abstraction (impls: `FakeBackend` now, `RobotServiceAdapter` at Phase C). Command surface mirrors RobotService names 1:1; feedback is push-only; ids not names; typed DTOs only. |
| `ShellWindow.{h,cpp}` | Composition root + mediator: assembles status bar / program editor / viewport slot (explicit deferred placeholder) / jog panel + settings, diagnostics and HAL overlays. ALL wiring in one `connectBackend` method, grouped per module; shell-owned behaviours explicit (name→id mapping, GO HOME confirm + home program, E-Stop toggle vs HAL engage, overlay navigation, app version). The ONLY module allowed to depend on feature modules (shell layer, not a peer). |
| `bench/FakeBackend.{h,cpp}` | ONE coherent offline controller over a single status/config world (deliberately NOT a composition of the per-module fakes): jog refused while disarmed, program refused under E-Stop, tick-stepped program execution, controller-storage emulation, config re-publish, deterministic jitter. |
| `bench/app_shell_bench_main.cpp` | Runs the assembled app; prints the shell version at startup; `--selftest` = 5 cross-module UI-path assertions (speed, jog arm+jog, settings open/apply/close, E-Stop toggle + diagnostics history + status-indicator click, settings→HAL nav + armed HAL jog on the SAME joint state); `--screenshot` renders the window + `_settings` + `_hal`. |
| `RobotServiceAdapter.{h,cpp}` | The PRODUCTION BackendClient over the shipping RobotService: mechanical 1:1 forwarding; halConfigChanged re-published per stateChanged (the pull contained in the adapter); QSettings endpoint merge + write-back with reconnect-on-change (shipping apply flow); takes `RobotService&` — no singleton use. |
| `main_ng.cpp` → **HexaStudioNG.exe** | The replacement application: composition-root duties only; resolves the singleton ONCE; TCP HAL simulator QProcess (shipping-compatible args); links the real backend stack WITHOUT Qt Quick/Quick3D; version 0.2.0 printed at startup; `--selftest` smokes the real-backend assembly. Shipping HexaStudio stays untouched and buildable in parallel. |

---

## 5. Target architecture (for the integration phase)

Layers; dependencies point **down** and must stay acyclic:

```
HexaStudio app (main.cpp, MainWindow)   = composition root + mediator (all wiring)
        │ signals(intent) / slots(feedback)
        ▼
feature modules:  program_editor · jog_control · viewport3d · overlays
        │
        ▼
hexa_contracts (BackendTypes/ProgramData + backend facade)  +  hexa_ui_kit (styles + cyberkeyboard)
        │                                                                   ▲
        ▼                                              hexa_backend (RobotService) ──┘
foundation libs: data_types · logger · rdt_protocol · StateData · network_driver · rdt_bridge · robot_model
```

Rules:
- Feature modules **never** depend on the backend or on each other; they talk only through MainWindow.
- The **only** justified abstraction is the backend contract (two impls: RobotService + Fake).
- Biggest integration win: **remove the `RobotService::instance()` singleton** (used today by
  PanelLeft/PanelTop, calibration/hal/settings overlays, MainWindow) and centralize wiring in MainWindow.
- Anti-over-engineering: NOT a lib per folder — merge thin panels (status bar → shell; diagnostics/hal/
  settings → one `overlays`).

---

## 6. Roadmap (remaining work, in order)

**Phase A — remaining panel modules (independent, not integrated):**
1. ~~`status_bar`~~ — **DONE** (module #3, § 4.3; the 2 RobotService refs replaced by
   `onConfigReceived` + `setAppVersion`).
2. **`overlays`** — settings + diagnostics **DONE** (module #4, § 4.4) — **Phase A modules are
   COMPLETE** (viewport3d deferred).
   `haloverlay` became the separate `hal_control` module (#5, § 4.5) by boss decision 2026-07-03 —
   the "merge thin ones" rationale applies to thin overlays, not to the heaviest hardware panel.
   (`calibrationoverlay` CANCELLED — calibration is performed in `hal_control`, see § 2 rule 7.)
3. **`viewport3d`** (from `PanelView3D`, ~1108 lines) — **DEFERRED by the boss (2026-07-02)**, revisit
   after the other modules. Already zero RobotService coupling. Heavier: depends on `Qt6::Quick3D` +
   `robot_model`; the Fake must feed robot status + a trajectory. Owns a module `Viewport3DPanel`.

**Integration foundation — DONE as module #6 `app_shell` (§ 4.6):** BackendClient contract +
ShellWindow composition root + FakeBackend; the assembled HMI runs offline. **`RobotServiceAdapter`
and the `HexaStudioNG` executable are DONE too (v1.1):** the replacement app builds against the
real backend stack and smoke-passes. **End-to-end VALIDATED against a live local HexaCore (2026-07-03):** `HexaStudioNG --probe`
connected to the real RDT server, full resync, SYSTEM READY with the controller's real tool/base
config and the jog SIM-only gate engaged — exit 0. Remaining for the switch-over: validation on the
MKS hardware rig, the viewport3d module, and the boss release decision to retire the shipping
HexaStudio (plus killing the singleton, which needs shipping-source edits).

**Phase B — shared foundation: DONE (2026-07-04, HexaStudioNG v0.2.7):**
4. `hexa_ui_kit` (styles + cyberkeyboard) and `hexa_contracts` (BackendTypes/ProgramData) are real
   linked libs; every module includes `HexaTheme.h`/`HexaWidgets.h`/`BackendTypes.h` flat and the
   module CMake files no longer add `../src` (exception: targets that still compile the legacy
   `ProgramDelegate`/`ProgramModel`, annotated in place).

**Phase C — integration (explicitly approved; touches the shipping app):**
5. Replace `PanelLeft`→`ProgramEditorPanel`, `PanelRight`→`JogPanel`, etc. in `MainWindow`; wire module
   signals/slots to `RobotService`; **kill the singleton**; delete the old panels; bump
   `kHexaStudioVersion`; update `ARCHITECTURE.md` and the upper-level requirement files.

**Cross-module features (separate, boss-scoped, need RDT + HexaMotion together):** program control flow
(IF/GOTO/LABEL/WAIT DI/SET DO) + integer registers; these stay gated in the editor until the wire +
controller land. See `program_editor/program_editor_REQUIREMENTS.md` § Cross-module interactions.

---

## 7. Build & run

Toolchain (this machine): CMake `C:\Qt\Tools\CMake_64\bin`, MinGW `C:\Qt\Tools\mingw1310_64\bin`, Qt
`C:\Qt\6.11.1\mingw_64`. Configured build dir: `Enterprise-RDT-dev/build` (Release).

```bash
export PATH="/c/Qt/Tools/CMake_64/bin:/c/Qt/Tools/mingw1310_64/bin:$PATH"
cmake -S Enterprise-RDT-dev -B Enterprise-RDT-dev/build          # only after adding/removing a target
cmake --build Enterprise-RDT-dev/build --target program_editor_panel_bench -j 4
cmake --build Enterprise-RDT-dev/build --target jog_control_bench -j 4
cmake --build Enterprise-RDT-dev/build --target status_bar_bench -j 4
cmake --build Enterprise-RDT-dev/build --target overlays_bench -j 4
cmake --build Enterprise-RDT-dev/build --target hal_control_bench -j 4
cmake --build Enterprise-RDT-dev/build --target app_shell_bench -j 4
cmake --build Enterprise-RDT-dev/build --target HexaStudioNG -j 4    # the REAL-backend replacement app
```

Run (needs Qt runtime + plugins on PATH; the `.bat` files set this up):
```
src/HexaStudio/program_editor/run_panel_bench.bat        # or  <bench>.exe --selftest
src/HexaStudio/jog_control/run_jog_bench.bat
src/HexaStudio/status_bar/run_status_bench.bat
src/HexaStudio/overlays/run_overlays_bench.bat
src/HexaStudio/hal_control/run_hal_bench.bat
src/HexaStudio/app_shell/run_shell_bench.bat      # the ASSEMBLED new-product HMI, offline
```
Headless smoke: `QT_QPA_PLATFORM=offscreen <bench>.exe --selftest` → exit 0.
Tests (program_editor): run `program_builder_tests` / `program_editor_model_tests` /
`program_storage_tests` / `fake_controller_tests` with the Qt bin on PATH.

---

## 8. Panel ↔ backend contracts

The seam each module must preserve (so it drops into MainWindow wiring). Signatures taken from the
shipping panels.

**program_editor (`ProgramEditorPanel`)**
- Intent (signals): `playRequested(program)`, `resumeRequested()`, `pauseRequested()`, `stopRequested()`,
  `programChanged(program)`, `remoteListRequested()`, `remoteLoadRequested(name)`,
  `remoteSaveRequested(name, program)`, `remoteDeleteRequested(name)`, view toggles.
- Feedback (slots): `updateState(HmiProgramStatus, HmiMotionStatus)`, `loadProgram(program)`,
  `setProgram(program)`, `setRemoteFileList(files)`.

**jog_control (`JogPanel`)**
- Intent (signals): `jogRequested(axis, increment)`, `jogEnableRequested(enabled)`,
  `coordSystemChanged(mode)`, `goHomeRequested()`, `stepChanged(val)`,
  `jogContextChanged(tool, base)`, `monitorContextChanged(tool, base)`.
- Feedback (slots): `updateState(HmiMotionStatus)`, `onConfigReceived(HmiSystemConfig)`.
  (`setCalibrationMode` removed with the cancelled calibration overlay — fine steps live on the
  HAL panel.)

**status_bar (`StatusBarPanel`)**
- Intent (signals): `modeChanged(isRealRobot)`, `eStopRequested()`, `settingsRequested()`,
  `speedChanged(percent)`, `diagnosticsRequested()`.
- Feedback: `updateState(HmiTopStatus, isMoving)`, `onConfigReceived(HmiSystemConfig)` (new vs
  PanelTop — replaces the About-box singleton reach), `setAppVersion(version)` (host-owned).

**overlays (`SettingsPanel`)**
- Intent (signals): `closeRequested()`, `applyRequested(HmiSystemConfig)`, `halOverlayRequested()`.
  (`calibrationOverlayRequested` dropped — calibration overlay cancelled.)

**overlays (`DiagnosticsPanel`)**
- Intent (signals): `clearErrorRequested()`, `eStopRequested()` (toggle semantics),
  `closeRequested()`.
- Feedback (slot): `updateStatus(HmiRobotStatus)` (pushed; the shipping overlay was fed the same
  way — contract unchanged).
- Feedback (slot): `setConfig(HmiSystemConfig)` (pushed by the host — replaces the shipping
  overlay's showEvent singleton pull).

**hal_control (`HalPanel`)**
- Intent (signals): `closeRequested()`, `applyHalRequested(HmiHalConfig)`,
  `tcpHalSimulatorLaunchRequested()`, `tcpHalSimulatorStopRequested()`,
  `halConnectRequested(host, port)`, `halDisconnectRequested()`, `halJogRequested(axis, stepDeg)`,
  `homingRequested(axis | -1)`, `setZeroRequested(axis)`, `setZeroAllRequested()`,
  `clearErrorsRequested()`, `jogArmRequested(armed)`, `eStopRequested()`.
- Feedback (slots): `setHalConfigCurrent(HmiHalConfig)`, `setTcpSimulatorRunning(bool)`,
  `onRobotStateChanged(HmiRobotStatus)` (all pushed — the shipping overlay connected to the
  singleton and pulled config in showEvent).

DTOs live in `hexa_contracts/BackendTypes.h` (module-owned; the `src/backend` copy remains only
for the shipping legacy app until `src/` is archived).

---

## 9. Open items / handoff notes

- **Commit state (branch `fix/mks-audit-batch`):** commits `325b1696` (program_editor +
  jog_control) and `2faabf58` (overlays, hal_control, status_bar) carry the EARLIER module states.
  Still uncommitted: the entire `app_shell/` module (incl. HexaStudioNG), `overlays/DiagnosticsLog.*`
  + the diagnostics/settings iterations, the jog_control design iterations (v1.5→v1.9), the
  status_bar layout fixes, and the additive root-CMake line for app_shell. Commit per module (only
  files under each `<module>/` + the root-CMake line); do NOT mix with the unrelated MKS-audit
  changes present in the same working tree. Full list: `HANDOVER.md` §3.
- **Version / ARCHITECTURE not bumped** — deferred to integration (they live in the shipping app).
- The upper-level `requirements/…/HexaStudio/ProgramBuilder.md` still lists the removed `rename()`/
  `hasCopy()` in its *proposed* interface sketch — reconcile at integration (out of module scope now).
- **Reach-through closed (v0.2.7)**: modules link `hexa_ui_kit` + `hexa_contracts`. Remaining tail:
  `app_shell`/`program_editor` targets still compile the legacy `ProgramDelegate`/`ProgramModel`
  (they resolve `styles/HexaTheme.h` + `ProgramData.h` via an annotated `../src` include) — port or
  replace them to finish the archive of `src/`.
- `jog_control` has **no gtest yet** (jog logic is in the widget). Consider a small `FakeJogController`
  test, mirroring `program_editor`.
- Memory/context for continuity is in the assistant's project notes (modularization recipe + status).
```
