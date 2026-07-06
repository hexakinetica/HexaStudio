# viewport3d — module requirements (v1.2)

Standalone, independently-runnable 3D viewport module for the new (NG) HexaStudio product. It is a
**module-owned re-implementation** of the shipping `PanelView3D` (which is frozen and archived to
legacy). Built and reviewed in isolation against an offline `FakeViewportController`; integration into
`ShellWindow`/`HexaStudioNG` is a later step.

Namespace/locations follow the module pattern (see `jog_control`, `status_bar`): folder
`src/HexaStudio/viewport3d/`, own `CMakeLists.txt` bench target, `bench/FakeViewportController.*`,
`bench/viewport3d_bench_main.cpp` (`--selftest` exit 0, `--screenshot <png>`), `run_viewport3d_bench.bat`,
additive `add_subdirectory` in the root studio block.

## 1. Panel contract (drop-in for the MainWindow/ShellWindow wiring)

`ViewportPanel` keeps the exact signal/slot contract of the shipping `PanelView3D` so it drops into the
same wiring with no host changes:

- Slots (feedback in): `updateState(HmiMotionStatus)`, `updateTrajectoryPath(HmiTrajectoryData)`,
  `setGhostVisible(bool)`, `setTrajectoryVisible(bool)`, `setApproachVisible(bool)`,
  `setTcpFrameVisible(bool)` (flange dot + triad, UI: SHOW TCP FRAME, default on),
  `setViewTop/Front/Iso/Side/FitToScreen()`.
- Config: `bool setRobotModelConfig(urdfPath, x,y,z, rx,ry,rz)` — same signature; plus
  `setModelFrameCorrection(rx,ry,rz)` for a wrong-URDF-frame trim (REQ 5.2).
- No RobotService / network coupling (the shipping panel already had none). Robot geometry comes from
  the shared `robot_model` foundation lib (`KinematicModel`), which stays a reach-through link.
- **Hosting:** the QtQuick3D scene is hosted via `QQuickView` + `QWidget::createWindowContainer`
  (native window), NOT `QQuickWidget` — the QQuickWidget texture-compositing flush
  (`QPlatformBackingStore::rhiFlush` → `updateDynamicBuffer`) crashes on the target AMD driver. Frame
  capture uses `QQuickWindow::grabWindow()` via `grabViewport()`.

## 2. Correctness carried over from the audit (must be fixed IN this module)

### 2.1 Terminal flange frame (the boss-flagged loader bug)
`KinematicModel::getVisualChain()` returns the whole `base_link -> tip_link` path, **including a
terminal `fixed` flange joint** past the 6 actuated axes. The shipping loader broke at
`kRobotAxisCount` and discarded it, so the rendered robot ended one flange-length short of the KDL FK
tip (which bakes the flange into its tip segment). This module's loader **composes the fixed frame(s)
past the 6 axes into one flange transform** (in the Joint_6 frame) and renders the optional flange
mesh on a fixed node hung off J6. **No-op for a flange-less URDF** (the current production
`HexaArm_Mini_Nema_Assem`, 6 `continuous` joints) — identical to today.

### 2.2 De-duplication (the "copy-paste / smells" the boss flagged)
- One mesh-attach helper for base link / axis link / flange (shipping had three near-identical copies).
- One geometry class for the trajectory polyline and the waypoint points (shipping had
  `TrajectoryGeometry` and `WaypointGeometry` differing only by primitive type).
- One tip->base chain-walk (do not re-import the shipping regex/duplication).
- Rename the misleading `m_trajCylinders` (they are spheres).

### 2.3 Base link visual origin (defect found 2026-07-07, fixed in HexaStudioNG 0.6.31)
The BASE link's authored `<visual><origin>` (offset + rotation) MUST be honoured exactly like the
axis links'. The panel used to hardcode identity for the base (`attachMeshVisual(..., QVector3D(),
QQuaternion(), ...)`) — latent, because every legacy export authored `0 0 0` there. The re-anchored
`HexaArmMedium_Light` URDF (base frame moved to the mounting face under the J1 axis) authors a
non-zero base visual origin; dropping it rendered the base mesh detached ~1.4 m from the arm.
The loader (`UrdfVisualModel.base.visualOffsetMm/.visualRotation`) already delivered the values —
the panel now stores (`m_baseVisualOffset/-Rotation`) and applies them in `buildRobotChain()`.

## 3. Visual target — RENDER-LEVEL / CINEMATIC (boss requirement)

The boss rejected the current "CAD-looking" STL view. The module targets a studio-render look while
staying an industrial HMI (must stay responsive; effects are justified and bounded, not gratuitous).

### 3.1 Geometry — smooth shading (biggest single win)
The shipping `StlMeshGeometry` emits **flat per-face normals**, which is the main reason the robot
looks faceted/CAD-ish. This module computes **smooth vertex normals**: weld vertices by position, sum
face normals (area/angle-weighted), and split along a **crease angle** (default 45 deg) so genuine
mechanical hard edges stay sharp while curved surfaces shade smoothly. Bounds are recomputed from the
welded vertices.

### 3.2 Environment — driver-constrained baseline (plain `SceneEnvironment`, Qt 6.11)
The target bring-up GPU is an **AMD Radeon 740M**, which crashes / blacks out on QtQuick3D's advanced
GPU features. The environment is therefore a plain, driver-safe `SceneEnvironment`:
- **Filmic tonemapping** (`TonemapModeFilmic`) + **MSAA High** anti-aliasing. Renders cleanly.
- **DISABLED (crash/black on this driver, re-enable on a driver/GPU fix):**
  - Image-Based Lighting via `ProceduralSkyTextureData` `lightProbe` → SIGSEGV in
    `QSSGBufferManager::createEnvironmentMap` (`QRhi` texture upload).
  - `ExtendedSceneEnvironment` post-effects (SSAO / glow / vignette / dithering) → render the frame
    black.
- The scene is carried entirely by the direct light rig (§3.3); no IBL reflections.

### 3.3 Lighting, materials & stage
- **3-point studio rig**: key `DirectionalLight` with soft PCF shadows (this DOES work on the driver),
  a cooler fill (no shadow), a warm rim/back light for edge separation.
- **Materials — MATTE (boss request "убрать отсветы"):** zero metalness + high roughness, so there are
  no specular glints; per-link base colour is the URDF `<material>` colour lifted to a readable value
  (`readableMetalColor`) so the near-black CAD colours are visible. Ghost stays translucent unlit blend
  (Convention B: solid = command, ghost = physical) — deliberately FAINT (desaturated steel
  `#5D8FC4`, opacity 0.15 — ONE GLASS DENSITY shared with the executed trajectory trace, boss
  consistency decision; hue still separates state/blue from plan/teal); the host UI exposes a
  SHOW GHOST toggle onto `setGhostVisible`. Floor is matte (low specular).
- **Stage/camera**: a large matte ground plane that receives the key shadow; the orbit/pan/zoom camera
  and the named view presets are preserved.

## 4. Offline fake + verification
- `FakeViewportController` feeds a robot status (`plannedJoints` + `realHardwareJoints` for the ghost)
  and a `HmiTrajectoryData` (path + waypoints) so the whole panel — chain, ghost, trajectory, TCP
  marker — renders offline with no controller.
- `--selftest`: headless smoke — loads the production URDF, asserts a valid 6-axis chain is built and
  the trajectory/ghost paths are consumed; exit 0.
- `--screenshot <png>`: renders the cinematic scene to a PNG for boss design-review (the established
  screenshot-review workflow), with optional view-preset frames.

## 5. Architecture laid up front (boss directive)

### 5.1 Mesh + appearance pipeline — STL default, auto-upgrade to an advanced model
STL is geometry only (no color / UV / usable normals). The robot's authored colors live in the URDF
`<visual><material>` — which the shipping pipeline **discards** (parser reads only mesh/origin; the panel
hardcodes colors). This module fixes that and supports a graceful upgrade path:

- **Per-link mesh resolution.** The URDF gives a mesh path (STL). The loader looks for a
  higher-fidelity sibling by base name in a preference order `[.glb, .gltf, .obj]` in the same
  directory. If found → use it; otherwise → the URDF's STL. So dropping a `Link_3.glb` next to
  `Link_3.STL` upgrades that link with no code or URDF change.
- **Appearance source by format:**
  - *Advanced mesh* (glTF/GLB/OBJ): carries its own PBR materials + textures + normals → loaded via
    QtQuick3D runtime import (`QQuick3DRuntimeLoader` / balsam-`.mesh`); the asset's materials are kept,
    not overridden.
  - *STL*: geometry only → the loader computes **smooth normals** and assigns a PBR material whose base
    color is the **URDF `<material>` color** for that link (metal metalness/roughness tuned). This is
    the "assemble properly from URDF" fix.
- **URDF color source of truth.** The authored color is parsed once into the model's visual description
  (`KinematicModel::VisualSegment` gains an appearance color; inline `<color>` + named top-level
  materials). NOTE: this is the one **shared foundation change** (robot_model) — additive, motion
  ignores the new field; flagged for boss awareness.
- **Ghost** stays translucent unlit (Convention B) regardless of the mesh source.

### 5.2 Coordinate system — ONE root transform, identical to the controller's
Two explicit transforms only:

1. **Render up-axis conversion** — fixed, display-only Z-up->Y-up, on the world node (NOT the robot);
   the controller/operator coordinates stay native Z-up.
2. **World->base root transform** (`ViewportModelConfig`, set via `setRobotModelConfig`) — THE single
   transform, mirroring the controller's `RobotModelConfig.root_transform` exactly. It carries both
   the physical mount placement and any URDF frame fix (production already routes the SolidWorks Y-up
   fix through it: `modelRootRxDeg = 90` in `hexacore_config.json`). A separate "URDF frame
   correction" knob existed earlier and was REMOVED: two orientation inputs for one thing were a
   divergence source between the visual robot, the KDL FK and the displayed trajectory. At integration
   the viewport must be fed from the same config as the controller.

### 5.3 Trajectory preview — displayed plan, robot rides it (boss-corrected semantics)
The panel only DISPLAYS a supplied trajectory (production semantics); it never accumulates a trail
behind the tip. `TrajectoryVisual` renders three layers:

- **Waypoint spheres** (`data.waypoints`) — the programmed via-points, platinum (`#F4F7FB`).
- **Thin line** along the **DENSE planner-sampled path** (`data.path`), product-accent teal
  (`#4FE8D2`, the HexaTheme accent family — the whole scene stays in the cool family so warm colours
  keep their warning-only meaning), drawn as bounded, sub-sampled built-in cylinder segments (custom
  `Lines`/`Points` geometry does not render on the target AMD driver). The dense path is the single source of truth for the TCP's real
  course: **straight for LIN, genuinely curved for PTP/JOINT** — the viewport never re-implements
  interpolation semantics (the answer to "how is the preview built per motion mode": the controller's
  own sampled plan is drawn, `generatePreviewPath`-style FK sampling on the HexaMotion side). Chords
  between sparse waypoints are only the fallback when no dense path is supplied.
- **Approach/departure layer** — the run-in from the current pose to the FIRST programmed waypoint
  and the run-out past the LAST one are transfer moves, not the programmed path: they draw in quiet
  graphite (`#64788C`) and `setApproachVisible(false)` (UI: SHOW APPROACH, default on) omits those
  segments entirely. Waypoints are bound to dense-path indices IN PROGRAM ORDER (monotonic cursor +
  capture tolerance), so closed/looping paths classify correctly.

**Preview lifecycle animations** (v0.6.8, boss request): a newly displayed trajectory **draws in**
from its start point (a single shared `QVariantAnimation` sweeps a soft opacity front along the
normalized path positions, ~0.9 s); executed portions **wipe softly** to the ghosted trace (~0.3 s
per-material colour/opacity animation) instead of snapping. Plain opacity/colour animations only —
shader effects stay off-limits on the target AMD driver.

**Investor-look batch** (v0.6.10, boss approved): the PROGRAMMED path carries a **neon-glow halo**
(second, wider translucent cylinder per segment; transfer moves stay quiet; executed portions drop
the halo — the glass trace keeps only the core); an **execution comet** (bright head + halo sphere)
rides the erase front while a program runs; the scene gains an **idle showroom turntable** (slow
orbit after 15 s without camera input, any input stops it) and **stage rings** (three concentric
hairline circles of flat unlit chord boxes around the robot base); the shell adds a
**PRESENTATION mode** (checkable toolbar button: side panels hidden, scene + status bar only).

**Hardware-compatibility rule (mandatory for every visual increment):** preview/scene effects may
use ONLY the baseline proven on the weakest target GPU (Radeon 740M) — built-in primitives,
`PrincipledMaterial` colour/opacity, standard lights and property animations. No shader effects, no
IBL, no post-processing, no textures introduced for effects: an unsupported GPU feature on this
class of hardware renders black or crashes the driver (§3.2), which is unacceptable on an HMI.

**Executed-portion fade** (v1.0.43): while a program runs, segments/waypoints the TCP has passed dim
to a GLASS TRACE (opacity 0.15 — the same glass density as the ghost — + darkened colour, hue kept)
and stay as a what-was-executed record until the next run or upload. Colour language (v0.6.9): the preview lives in the product's cool
accent family (teal path / platinum waypoints / graphite transfers) so warm colours keep their
warning-only meaning across the HMI.

**TCP highlight (boss request):** a teal dot + RGB XYZ triad (`attachTcpMarker`) parented to the solid
robot's flange node, so the operator sees exactly which point rides the trajectory and how the tool
frame is oriented. The production URDF authors the flange frame with +Z pointing INTO the arm (not
ISO 9787 tool-approach); a source-level flip was tried and reverted because it re-interprets every
stored Cartesian orientation (taught programs fail IK). By boss order (v0.6.12) the MARKER frame
alone is therefore rotated pi about X (`kTriadIsoPreviewRxDeg` in `attachTcpMarker`): the drawn Z
exits the mounting face as the operator expects. DELIBERATE, DOCUMENTED discrepancy until the URDF
migration lands: TOOL-frame jog Z+ still follows the kinematic frame (into the arm). See the URDF
Joint_Flange comment / ARCHITECTURE.md for the migration plan. The bench demo precomputes one closed pose cycle (`buildPreview()`: samples
`flangeWorldPosition()` over 120 phases), displays it once via `updateTrajectoryPath`, and animates the
robot along the same cycle — trajectory shown, robot follows.

Extensible next steps reserved by the design (no code yet): typed per-segment metadata in the protocol
(LIN/PTP/CIRC spans + blend zones) for mode-aware styling, per-waypoint orientation triads, and a
separate ACTUAL-vs-PLANNED path layer.

## 6. Integration plan (next step, boss-reviewed direction)

The module is standalone-approved; integration into the NG product follows this order:

### 6.1 Coordinate-system alignment — THE critical integration requirement
The controller publishes trajectory/TCP data in the WORLD frame (its KDL chain bakes the configured
`root_transform` in at import). The viewport applies its frame correction + mount on the robot's visual
root. These agree **only if both sides consume the same `root_transform` values from the same config**
(`hexacore_config.json` `modelRoot*` fields → `setRobotModelConfig`, the single transform).
At integration this must be verified END-TO-END against a live HexaCore (the `--probe` recipe): the
FK TCP marker must sit on the visual flange, and a controller-sent trajectory must emanate from the
robot. A mismatch here is exactly the "траектория не там" class of defect.

### 6.2 Wiring (mechanical)
- Un-defer `ShellWindow::m_viewportPlaceholder` (ShellWindow.cpp:99-107, 208-222): replace the QLabel
  with `ViewportPanel`; re-route the existing view-toggle intents (`setGhostVisible`,
  `setTrajectoryVisible`, view presets) to the panel slots they were designed for.
- `app_shell/CMakeLists.txt`: add `Qt6::Quick3D` + `Quick3DAssetUtils(Private)` + `QuickWidgets`-free
  Quick deps to `HexaStudioNG` (currently "NO Quick3D") and compile the viewport3d sources (same
  reach-through pattern as the other feature modules).
- Connect the backend's `trajectoryReceived(HmiTrajectoryData)` → `ViewportPanel::updateTrajectoryPath`
  (deliberately left unconnected until now) and the robot status stream → `updateState`.
- Program-authored waypoints (program_editor teach points) arrive as `HmiTrajectoryData.waypoints`;
  the preview (waypoint spheres + thin line) renders them as-is.

### 6.3 Preview layering (after 6.1/6.2 are proven)
Separate the PLANNED program preview from the ACTUAL motion:
- **Planned layer** (static, from the controller/editor): waypoint markers + thin line, plus reserved
  increments — per-waypoint orientation triads, LIN/PTP/CIRC per-segment styling, blend-zone corner
  rounding.
- **Actual layer** (live): the flange TCP trail (this module's current demo behaviour).
Both consume `HmiTrajectoryData`-shaped input; the demo TCP trail moves behind a bench-only switch.

## 7. Out of scope (later)
- Attaching a tool mesh / TCP triad on the flange node (the flange frame is prepared here; the tool
  attach lands with the tool/measurement feature).
- IBL + `ExtendedSceneEnvironment` effects — gated on the AMD driver issue (§3.2).
- glTF asset production for the auto-upgrade path (§5.1); waypoint click-selection/editing coupled to
  the program editor.
