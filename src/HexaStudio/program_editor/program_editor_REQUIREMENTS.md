# program_editor — module requirements

- **Module:** `src/HexaStudio/program_editor`
- **Side:** pendant (HexaStudio HMI), non-RT, GUI thread. Qt Core/Widgets only; no network, no RDT.
- **Status:** additive standalone module, proven on the bench; NOT yet integrated into the HexaStudio app.
- **Policy (per boss):** requirements that concern *this module's own behavior* live here. Requirements
  that concern *interactions with other modules* (HexaStudio app, RobotService/RDT, HexaMotion) live in
  the upper-level requirement files — see [§ Cross-module interactions](#cross-module-interactions). They
  are referenced here, never duplicated.

Version of this requirements set: **1.2** (removes pendant-local program persistence — boss directive
2026-07-06: programs are loaded/saved on the controller only).

---

## Scope

In scope (this module): authoring a program (`QVector<ProgramCommand>`), a Qt list adapter for display,
whole-program validation, undo/redo, and the decoupled editor panel with its tools/layout. Out of
scope: program file persistence (controller-side only, reached over the backend seam — cross-module),
mapping to the wire, executing control flow, controller feedback semantics, and app integration — all
cross-module (see below).

---

## Authoring core — `ProgramBuilder`

### PE-REQ-0001 — Single authoring authority
The program content shall be owned by `ProgramBuilder` alone (`QVector<ProgramCommand>`); every
construction/edit shall go through its validated methods. No other module-internal type mutates the
program.
`Verification: Test (program_builder_tests)` · `Status: Implemented`

### PE-REQ-0002 — Typed errors, no silent failure
Every operation that can fail shall return `RDT::Result<…, ProgramError>` with a diagnostic
(`toString`). `std::optional`/`std::variant`/exceptions shall not appear in the public surface.
`Verification: Review + Test` · `Status: Implemented`

### PE-REQ-0003 — Canonical parameter keys
Parameter keys shall be declared once as `ProgramBuilder::k*` constants and used on both the write and
the read path; no magic parameter strings in the UI.
`Verification: Review` · `Status: Implemented`

### PE-REQ-0004 — TCP edit authority
TCP-pose component editing shall be permitted only for cartesian-dominant moves (LIN); a joint-dominant
point (PTP) shall reject it with `PoseNotEditableForJointMove`.
`Verification: Test` · `Status: Implemented`

---

## Display adapter — `ProgramEditorModel`

### PE-REQ-0010 — Thin adapter, no own content
The model shall hold no program content; it shall read from `ProgramBuilder` and stay in sync via the
builder's granular signals. Roles shall match `panels/left/ProgramModel` for `ProgramDelegate`
compatibility, plus `IssueRole`.
`Verification: Test (program_editor_model_tests)` · `Status: Implemented`

### PE-REQ-0011 — Transient view state kept out of the builder
The execution pointer (active running line) and the per-row validation severity shall be view-only state
in the model, not owned by the builder.
`Verification: Review + Test` · `Status: Implemented`

---

## Validation & RUN gate

### PE-REQ-0020 — Whole-program validation
`validate()` shall return typed `ProgramIssue`s (empty program, motion point without a taught pose, speed
out of range → Error; unknown zone → Warning).
`Verification: Test` · `Status: Implemented`

### PE-REQ-0021 — Author-side RUN gate
The panel shall call `validate()` before requesting RUN and shall block RUN when any Error-severity issue
exists, showing the issues and selecting the first offending step. The controller remains the final
safety arbiter (cross-module).
`Verification: Review` · `Status: Implemented`

### PE-REQ-0022 — Validation severity is visible
The list shall show a per-row validation marker (Error/Warning) via `IssueRole`, rendered without
obscuring the step's type bar or the active-line indicator.
`Verification: Review` · `Status: Implemented (ProgramIssueDelegate)`

---

## Undo/redo & reset origin

### PE-REQ-0030 — Bounded undo/redo
The builder shall provide undo/redo as a bounded snapshot stack (depth `kMaxUndoDepth`), with `canUndo`/
`canRedo`; `load`/`clear` clear both stacks.
`Verification: Test` · `Status: Implemented`

### PE-REQ-0031 — Reset origin distinguishes echo vs upload
A full-program reset shall carry a `ProgramResetReason`: `ExternalLoad` (controller/initial cache — must
NOT echo an upload) vs `LocalEdit` (undo/redo/clear — MUST be pushed out). undo/redo now
re-upload; controller loads do not.
`Verification: Test (ResetReasonDistinguishes…)` · `Status: Implemented`

### PE-REQ-0032 — Dirty state
`isModified()`/`modifiedChanged` shall track unsaved edits; the panel shall show an unsaved marker in the
program title, cleared by `markSaved()`/load.
`Verification: Review` · `Status: Implemented`

---

## Program storage — controller only

### PE-REQ-0040 — No pendant-local program store
The pendant shall NOT persist program files locally. Programs shall be listed/loaded/saved/deleted
exclusively in controller storage through the backend seam (`remoteListRequested` /
`remoteLoadRequested` / `remoteSaveRequested` / `remoteDeleteRequested`). The controller's program
directory is configured on the controller side (`programs_dir` in `hexacore_runtime_config.json`,
overridable by `HEXAMOTION_PROGRAMS_DIR`), never by the pendant.
Rationale: boss directive 2026-07-06 — one program library, one master. The former `ProgramStorage`
(pendant JSON store, WORKSTATION tab) and its requirements PE-REQ-0041…0043 were removed with it;
their integrity guarantees (typed errors, versioned envelope, validated I/O) now apply on the
controller side only.
`Verification: Review (no local file I/O in the module) + Test (fake_controller_tests)` · `Status: Implemented`

---

## Panel behavior & layout — `ProgramEditorPanel`

### PE-REQ-0050 — Read via model, write via builder; no inline construction
The panel shall read step data through the model roles and mutate only through `ProgramBuilder`; it shall
not construct `ProgramCommand` inline nor call model `setData`.
`Verification: Review` · `Status: Implemented`

### PE-REQ-0051 — Command-surface gating
Only commands whose data path is complete shall be enabled; not-yet-wired commands (CIRC/SPLINE, SET DO/
WAIT DI/IF/GOTO/LABEL/BREAK, SET PP) shall be visible-but-disabled with a "planned" tooltip (no silent
mis-execution). Un-gating each is cross-module.
`Verification: Review` · `Status: Implemented`

### PE-REQ-0052 — Tool-area layout has no overlap
Each tab's tool area shall be given the height its rows need (no row overlaps another); the program list
takes the remaining space.
`Verification: Review` · `Status: Implemented`

### PE-REQ-0053 — Program-wide controls and collapse
UNDO/REDO shall be reachable from every tab (program title row). MOVE ▲/▼ (reorder) shall live in TEACH.
A collapse toggle shall hide only the bottom tool area in every program tab, keeping and enlarging the
program list; the collapsed state shall persist across tab switches.
`Verification: Review` · `Status: Implemented`

### PE-REQ-0054 — Empty-state guidance
When the program has no steps, the list shall show a hint directing the operator to the first action.
`Verification: Review` · `Status: Implemented`

---

## Cross-module interactions

Per policy, these are NOT specified here; they belong in the upper-level requirement files because they
cross the module boundary. This module only exposes the seam (signals/slots) and encodes/validates.

| Interaction | Belongs in (upper-level) |
|---|---|
| Panel ↔ RobotService: play/resume/pause/stop, `programChanged`→upload, remote list/load/save/delete, status→`updateState`, external `loadProgram` (self-echo suppression) | `requirements/…/HexaStudio/PanelLeft.md`, [[RobotService]] |
| Program upload/mapping to the wire (`mapHmiProgramToRdt`) | [[RobotService]], `RDT_Protocol` |
| Control flow / IO / registers executed on the controller (IF/GOTO/LABEL/WAIT DI/SET DO, SetVar/Inc/Dec) + watchdog | `requirements/…/HexaStudio/ProgramBuilder.md` REQ-0006/0008, HexaMotion `executeNextStep` |
| App integration: replace `PanelLeft` with `ProgramEditorPanel` in `MainWindow`; version bump; ARCHITECTURE.md | HexaStudio app requirements / `ARCHITECTURE.md` |

---

## Verification summary

Module unit tests (Qt Core, no widgets, no RobotService), all green:
`program_builder_tests` (21) · `program_editor_model_tests` (5) ·
`fake_controller_tests` (5). GUI smoke: `program_editor_panel_bench --selftest` (exit 0).
