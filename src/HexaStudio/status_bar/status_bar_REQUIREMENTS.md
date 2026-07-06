# status_bar — module requirements

- **Module:** `src/HexaStudio/status_bar`
- **Side:** pendant (HexaStudio HMI), GUI thread. Qt Widgets only; no network, no RDT, no RobotService.
- **Status:** additive standalone module, runnable in isolation; NOT integrated into the HexaStudio app.
- **Policy (per boss):** this module's own requirements live here. Cross-module interactions are listed
  under [§ Cross-module interactions](#cross-module-interactions) and belong to the upper-level files.

Version of this requirements set: **1.5** (v0.4.0 industrial-carbon status block: display words
E-STOP / OFFLINE / MOVING / ERROR / READY, label width from font metrics).

---

## Scope

In scope: a module-owned top status bar (`StatusBarPanel`) run standalone against an offline controller
emulation, driven exactly as `MainWindow` drives it. Out of scope: the settings/diagnostics overlays
(shell-owned), real E-Stop execution, and app integration — all cross-module.

This is a **new-product** module: the shipping `PanelTop` is NOT modified and NOT used. `StatusBarPanel`
is the module's own panel that keeps `PanelTop`'s exact intent/feedback contract (so it drops into the
same MainWindow wiring) but removes the RobotService singleton dependency. The module reuses only the
shared `styles` sources and font resources via reach-through, until they are extracted into
`hexa_ui_kit`.

---

## Requirements

### STB-REQ-0001 — Standalone, controller-free run
The status bar shall build and run in isolation from HexaStudio, the controller and the network, hosted
against `FakeTopController`. A launcher `run_status_bench.bat` shall start it; `--selftest` shall wire
everything headless and exit 0 on success / 1 on failure.
`Verification: Review + Smoke (--selftest)` · `Status: Implemented`

### STB-REQ-0002 — Intent out, status in (no direct backend)
The panel shall emit intent (`modeChanged`, `eStopRequested`, `settingsRequested`, `speedChanged`,
`diagnosticsRequested`) and consume feedback via `updateState(HmiTopStatus, isMoving)` and
`onConfigReceived(HmiSystemConfig)`. It shall not reference `RobotService` or the network. The bench
wiring shall connect EVERY panel signal to a consumer — an intent without a consumer is a dead control
on the bench.
`Verification: Review` · `Status: Implemented`

### STB-REQ-0003 — No singleton: About-box data arrives via the seam
The About box (click on the brand) shall show the controller IP from `onConfigReceived` and the
application version from `setAppVersion(QString)` (host-owned). The shipping panel's
`RobotService::instance()` reach and its hardcoded version string are removed. Missing data is shown
explicitly ("Not configured" / "---"), never fabricated.
`Verification: Review` · `Status: Implemented`

### STB-REQ-0004 — Safety behaviour preserved verbatim
The safety-relevant behaviour of the shipping panel is preserved: switching to REAL requires an explicit
confirmation; the SIM/REAL switch is locked while the robot is moving and in E-STOP/OFFLINE states;
a speed override above 50 % requires an explicit confirmation and reverts to the last accepted value on
refusal; the E-STOP button uses danger styling and flips to RESET while E-Stop is active; the system
status is a traffic light with explicit precedence E-STOP > OFFLINE > MOVING > ERROR > READY
(OFFLINE is the v0.4.0 display word for the connection-lost state, formerly "DISCONNECTED"),
driven by the explicit `hasError` flag (not by message text).
`Verification: Review + Smoke (E-Stop feedback asserted)` · `Status: Implemented`

### STB-REQ-0005 — Honest stats as monochrome instrument cells
`HmiTopStatus` carries CPU load, controller temperature and network latency — the STATS view shows
exactly these, each in a framed instrument cell (muted label over a white mono value, same cell
language as the jog panel). MONOCHROME by boss decision (2026-07-02): no per-state colour coding, no
bars — the standard theme colours only. The view is named STATS (button "STATS" / "CLOSE STATS"),
NOT "monitor": the jog panel already owns a position monitor ("MONITOR SYSTEM"). VOLTAGE and CYCLE
TIME are NOT part of the status DTO and are NOT displayed — no dead "---" slots (the shipping panel
hardcodes "24.0V"/"4.0ms"); the cells return once the DTO reports the values (cross-module item).
`Verification: Review + Screenshot` · `Status: Implemented`

### STB-REQ-0006 — FakeTopController mirrors the RobotService seam
`FakeTopController` shall consume the panel's intents (`setMode`, `setSpeedOverride`, `setEStop` via the
bench toggle wiring that mirrors the MainWindow lambda) and report `HmiTopStatus` + `isMoving` and a
demo config (controller IP). Stat jitter shall be deterministic (sine-based, no randomness) so bench
runs and renders are reproducible. Qt Core only.
`Verification: Review + Smoke` · `Status: Implemented`

### STB-REQ-0007 — Bench renders app-faithful, supports design review
The bench host window shall paint the application background colour and load the application fonts.
`--selftest` shall drive the REAL speed combo (25 %, below the confirmation threshold — a blocking
dialog would hang a headless run) and assert the value reaches the controller seam, and shall assert
the E-Stop feedback path flips the button to RESET. `--screenshot <file.png>` shall render five
frames: controls view, `<file>_stats.png` (system-stats view, toggled via the real STATS button),
`<file>_estop.png` (E-Stop active), `<file>_confirm.png` (the REAL-mode safety confirmation dialog)
and `<file>_narrow.png` (the bar at/near its minimum width — the PNG width measures the real floor)
and exit 0.
`Verification: Smoke (--selftest, --screenshot)` · `Status: Implemented`

### STB-REQ-0008 — Pendant-styled safety confirmation (no OS message box)
Both safety confirmations (REAL-mode switch, speed override above the threshold) shall use the
module-owned `ConfirmDialog`: frameless on the application background with a warning-coloured title
and border; CANCEL is the neutral, focused default; the confirm button is danger-styled and carries an
EXPLICIT action label ("SWITCH TO REAL", "APPLY 75%") so the operator reads what is being confirmed.
`QMessageBox` shall not be used for safety confirmations (OS styling breaks the HMI look).
`Verification: Review + Screenshot` · `Status: Implemented`

### STB-REQ-0009 — Control-group gaps survive resize and DPI scaling
The gaps between the control groups (SIM/REAL | SPEED | SETTINGS/STATS) shall be fixed spacer items,
which are part of the layout's MINIMUM size — they can never collapse when the window narrows or the
display scale changes (plain `setSpacing` is sacrificed first by the layout engine and let SPEED crowd
SETTINGS).
`Verification: Review + Screenshot` · `Status: Implemented`

### STB-REQ-0010 — The bar narrows to its honest content minimum
The bar shall be narrowable down to the width its VISIBLE content actually needs; no artificial width
floors. Mechanisms: the left section is sized by content (no fixed reserved width — the shipping panel
reserved 400 px); only the visible page of the centre stack contributes to the minimum size (hidden
pages get the Ignored size policy, otherwise the wider monitor page dictates the minimum even while
hidden); buttons use BOUNDED-SHRINK widths (preferred width while space allows, smooth compression to
a readable minimum under pressure: SETTINGS/MONITOR 84->64, E-STOP 120->104, CLOSE MONITOR 120->100) so
the controls visibly scale as the operator narrows the window. Exception: the editable speed combo
stays fixed at 72 px - sized by its Qt hint it clips the value text (the drop-down arrow and frame eat
the inner width). The remaining floor is honest content: brand, the status label sized from font
metrics over the full status vocabulary (`HexaWidgets::statusLabelMinWidth` - never a hand-tuned
pixel constant, which clipped "SYSTEM READY"), the control cluster with its forced group gaps
(STB-REQ-0009) and the E-STOP minimum. Reducing the floor further requires a content decision
(e.g. icon-only buttons), not a layout trick.
`Verification: Screenshot (the `_narrow` frame width IS the measured floor)` · `Status: Implemented`

---

## Cross-module interactions

Not specified here (they cross the module boundary):

| Interaction | Belongs in (upper-level) |
|---|---|
| `modeChanged` → `RobotService::setMode`; `speedChanged` → `setSpeedOverride`; E-Stop toggle → `setEStop` | HexaStudio app requirements, [[RobotService]] |
| `settingsRequested` / `diagnosticsRequested` → shell overlays (MainWindow) | HexaStudio app requirements |
| `setColumnGuides(left,right)` → pin the left group / E-STOP under the shell's side columns (shell-only; the bench keeps STB-REQ-0010 content sizing) | HexaStudio app / ShellWindow (v0.4.2) |
| VOLTAGE / CYCLE TIME fields in the status DTO | `BackendTypes` / RDT protocol |
| App integration: host `StatusBarPanel` in `MainWindow`, remove `PanelTop` | HexaStudio app requirements |

---

## Verification summary

GUI smoke: `status_bar_bench --selftest` (exit 0; asserts the speed UI path and the E-Stop feedback
path). Design render: `status_bar_bench --screenshot <file.png>` (three frames). Interactive:
`run_status_bench.bat`. Module owns `StatusBarPanel`; reuses only styles / shared font resources via
reach-through. Shipping `PanelTop` untouched.
