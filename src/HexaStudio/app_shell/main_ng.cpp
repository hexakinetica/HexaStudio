// --- START OF FILE: HexaStudio/app_shell/main_ng.cpp ---
/**
 * @file main_ng.cpp
 * @brief HexaStudioNG - the new-product application entry point: ShellWindow (all feature modules)
 *        wired to the REAL backend: hexa::HexaBackend, the module-owned BackendClient.
 *
 * This executable is the replacement for the shipping HexaStudio. Composition-root duties (and
 * nothing else) live here:
 *   - meta types, theme fonts, shared logger, version banner;
 *   - constructing the module-owned backend (hexa::HexaBackend) and connecting to the controller
 *     from the workstation-local settings;
 *   - owning the TCP HAL simulator QProcess (host-process concern outside the backend contract).
 *
 * The viewport3d module is integrated: HexaStudioNG links Qt Quick/Quick3D for the centre-column 3D
 * scene (robot model + planner trajectory preview, fed from the controller config over RDT).
 */
#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QProcess>
#include <QSettings>
#include <QTimer>

#include <iostream>
#include <memory>

#include "ShellWindow.h"
#include "HexaBackend.h"
#include "HalPanel.h"

#include "Logger.h"

namespace {

// New-product application version, printed at startup (project version policy).
// 0.4.3: top-bar centre cluster - SPEED reads as a selector, SETTINGS/STATS/SPEED aligned width.
// 0.5.0: viewport3d integrated - 3D robot + trajectory preview in the centre column (backend
//        trajectoryReceived, config-driven model frame, view-preset toolbar, overlay obscuring).
// 0.5.1: FakeBackend honesty boundary - bench stubs removed (wiring only); OFFLINE BENCH title.
// 0.5.2: program pipeline audit - arm-on-load, program identity in the editor title, mapper gtests.
// 0.5.3: storage architecture (B1/B2) - controller storage is the MASTER; versioned file envelope.
// 0.5.4: program files - envelope is the ONLY accepted format (no legacy fallback).
// 0.5.5: config hardening (TF1-TF3) - fail-closed config load, formatVersion, active-frame persist.
// 0.6.0: legacy shipping app ARCHIVED (repo legacy/HexaStudio): ProgramDelegate/ProgramModel owned by
//        program_editor (theme via hexa_ui_kit, ProgramData via hexa_contracts), zero src/
//        reach-through, HexaStudio.exe target removed - HexaStudioNG is THE product.
// 0.6.1: CIRC authoring un-gated - TEACH VIA two-touch flow, via validation blocks RUN
//        (docs/REQ_motion_circ.md batch 1c).
// 0.6.2: SPLINE authoring un-gated - taught like LIN, consecutive points = one smooth block; block
//        validation (docs/REQ_motion_spline.md batch 2c).
// 0.6.3: SET DO un-gated (sequencer P3) - authored port/state ride the wire io_port/io_state; the
//        controller actuates through the HAL (docs/REQ_program_sequencer.md).
// 0.6.4: viewport - executed trajectory portions fade to a ghosted trace during a program run
//        (fade-executed feature).
// 0.6.5: sequencer P4 authoring - SET/INC/DEC VAR + register-compare IF (counter loops) + BREAK
//        un-gated; CMD gating machinery removed (every command wired); RDT protocol v2.
// 0.6.6: sequencer P5 display - execution annotation strip under the program list (live registers +
//        last IF outcome), kept after the run as a what-happened record.
// 0.6.7: viewport/UI pass - premium trajectory palette (gold path, steel approach+departure hidden
//        by SHOW APPROACH, platinum waypoints), SHOW GHOST toggle + fainter ghost, view toolbar
//        below the viewport, full-screen button names its next action. (URDF flange +Z flip tried
//        and REVERTED: breaks IK on previously taught Cartesian points - needs a coordinated
//        migration, see ARCHITECTURE.md.)
// 0.6.8: viewport preview polish - brighter gold/steel palette, draw-in animation on a new
//        trajectory, soft wipe-to-trace on executed portions; motion-types demo regenerated bigger
//        (6 waypoints/pass, 300x210 mm, 20 mm between passes).
// 0.6.9: signature cool preview palette (product-teal path, platinum waypoints, graphite
//        transfers, glass executed trace 0.12); motion demo layers spread to 30 mm.
// 0.6.10: investor-look batch (boss approved 1/2/3/5/6) - neon-glow programmed path, execution
//         comet on the erase front, idle showroom turntable, stage rings on the floor, PRESENTATION
//         mode (scene + status bar only). Baseline primitives/materials only - weak-GPU safe.
// 0.6.11: review fixes - SHOW APPROACH toggles instantly (draw-in sweep reserved for NEW
//         trajectories); CAMERA AUTO-ORBIT badge while the turntable runs.
// 0.6.12: auto-orbit badge restyled (white, rounded rectangle); flange triad rotated to the ISO
//         tool-approach convention (DISPLAY-ONLY, documented discrepancy until the URDF migration);
//         demo v7 - 50 mm layers, TRAVERSE + LAYER DOWN, no return-to-home.
// 0.6.13: auto-orbit badge enlarged; SHOW TCP FRAME toggle; demo v8 serpentine (pure-Z LAYER DOWN,
//         opposite direction per pass); ONE glass density 0.15 for ghost + executed trace.
// 0.6.14: program list identity swap - motions violet / commands cyan; comment steps gain an EDIT
//         page (ProgramBuilder::setCommentText, undoable).
// 0.6.15: CyberKeyboard reworked (cursor-aware editing, window-centred, draggable, carbon theme);
//         EDIT PROPERTIES pages top-aligned.
// 0.6.16: motion smoothness - rendered joints eased toward the latest status packet (display-only).
// 0.6.17: linear packet interpolation; auto-orbit motion gate; RESUME keeps the executed trace;
//         PAUSED status word.
// 0.6.18: pause fixes - erase self-re-attaches anywhere on the path after a reset (was freezing
//         after pause), comet stays on the execution point through a pause; interpolation ignores
//         duplicate 50 Hz poll packets (lurch fix); TEMP 5s cadence diagnostic for the
//         progressive-slowdown investigation.
// 0.6.19: guide P1 (guided demo skeleton) - GUIDE sidebar tab + scenario cards with PLAY/STEP
//         pacing, compiled-in TOUR scenario, explicit target registry, pulsing highlight +
//         virtual clicks through the production path, floating callout with NEXT/EXIT; guide
//         module bench with --selftest.
// 0.6.20: guide callout readability pass (boss review) - presentation-distance type (title 17px,
//         body 14px, bright step counter), scenario progress bar on the callout and mirrored on
//         the GuidePanel.
// 0.6.21: guide bench --screenshot render mode (design-review frames); fix: the panel status line
//         reports the ACTIVE run's mode from the runner, not the chip selection.
// 0.6.23: guide visual pass 2 (boss review) - VIOLET guide accent everywhere (pulse, counter,
//         progress fills, NEXT, selected card/chip), stronger target pulse, type up again,
//         cards size from content - no more clipped descriptions.
// 0.6.24: pendant-local program store REMOVED (boss directive 2026-07-06) - controller storage is
//         the ONLY program library; WORKSTATION file tab, ProgramStorage and ProgramCommand JSON
//         serialization deleted; SAVE confirmation (programSaved) clears the UNSAVED marker only on
//         controller acknowledgement.
// 0.6.25: Settings gains a REALTIME HAL BACKEND dropdown (Simulation / UDP / MKS-TCP), default
//         Simulation, in the Network editor. Rides the existing persisted-config wire path
//         (HmiSystemConfig.realtimeBackend -> RobotConfigData.realtimeBackend); applied by the
//         controller on its next start. Same shared module serves the future HexaStudioDesktop
//         edition (docs/REQ_hexastudio_desktop.md).
// 0.6.26: guide visual pass 3 (boss review) - highlight = bold STATIC 5px violet frame (blinking
//         rejected as eye-searing; animation machinery removed); card/callout clipping fixed with
//         REAL QFonts at fixed text width; TOUR opens the UI tab BEFORE narrating the scene layers.
// 0.6.27: HAL backend selector MOVED into the HAL RUNTIME overlay (boss directive 2026-07-06),
//         left column above the transport card it gates: SIM (default) hides the MKS CONNECTION
//         card and works immediately (motors enabled, jog rides the production planner path);
//         MKS shows the existing menu; UDP shows a deferred-configuration note. Sim-honest
//         indicators: "HAL: Simulation" badge, axis enable state from motorEnabled, no
//         REAL-hardware warning. Selection still rides the persisted config (echo-synced).
// 0.6.28: guide content + targeting overhaul (boss review pass 4) - TOUR rebuilt around the real
//         teach-and-run workflow (jog the robot, teach a point, load a program, run it); view-
//         preset spam dropped to a single ISO framing at the end. Highlight is now a mouse-
//         transparent overlay FRAME the shell/bench draws over the target - it no longer touches
//         the target stylesheet, so it survives self-restyling controls (the jog ENABLE button
//         wiped the old stylesheet border ~20x/s). Real accessors added for jog ENABLE/key/HOME,
//         RUN, + TEACH, TEACH/FILE nav and the controller LOAD so highlights land on the exact
//         control, never a whole card.
// 0.6.29: operator-experience fixes (boss review 2026-07-07):
//         - showroom turntable NEVER starts while the operator works anywhere in the HMI - an
//           application-wide input filter (click/wheel/key/touch) re-arms the idle timer, not just
//           camera input inside the scene;
//         - program identity moved to its OWN second line under the PROGRAM title, elided to the
//           column width (Ignored size policy): a long file name can no longer widen the left
//           column - the column width belongs to the operator (splitter only);
//         - controller file list refreshes AUTOMATICALLY (on entering the FILE page and after every
//           save/delete via the backend response); the manual REFRESH button is removed.
// 0.6.30: guide TRY mode (P2, pulled forward) is now the DEFAULT hands-on mode - on a ClickTarget
//         step the runner waits for the USER to press the real control (connected to its clicked()),
//         escalates the step's hint on idle, and the callout offers SHOW ME to press it for them;
//         chips reordered TRY/STEP/PLAY (TRY checked). Highlight moved to a mouse-transparent overlay
//         frame earlier, so the awaited control is pressable underneath. Motion scenario (LIVE
//         TEACH & RUN) is next.
// 0.6.31: viewport honours the BASE link's authored <visual><origin> (offset+rotation), exactly as
//         it always did for the axis links. Latent defect exposed by the re-anchored
//         HexaArmMedium URDF (base frame moved to the mounting face): the offset was dropped and
//         the base mesh rendered detached ~1.4 m from the arm.
// 0.6.32: viewport visual retune (boss review) - neutral studio lighting: warm key #fff4e6 and rim
//         #ffe9cf read as a yellow cast on the white/aluminum HexaArmMedium shells; key is now
//         neutral white, rim cool-neutral. Pairs with robot package 1.0.3 (GLB PBR retune:
//         aluminum body, black joint covers - data-only, proven by diagnostic renders).
// 0.6.33: HAL panel MC position source unified (REQ_sim_backend_virtual_mc): SIM view shows
//         realHardwareJoints (the ghost source; plannedJoints is the honest COMMAND since
//         Convention B - the panel was showing the command, not the MC), REAL view the active
//         feedback; the sim backend's virtual MC publishes through the same path.
const QLatin1String kHexaStudioNgVersion("0.6.33");  // HAL panel: MC position source unified (virtual MC parity)

constexpr int kTcpSimStartTimeoutMs = 3000;
const QLatin1String kTcpSimExecutable("HexaHAL_Client.exe");

void loadAppFonts() {
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/Michroma-Regular.ttf")) == -1) {
        std::cerr << "HexaStudioNG: failed to load Michroma font" << std::endl;
    }
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/IBMPlexMono-Regular.ttf")) == -1) {
        std::cerr << "HexaStudioNG: failed to load IBM Plex Mono font" << std::endl;
    }
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    qRegisterMetaType<HmiRobotStatus>("HmiRobotStatus");
    qRegisterMetaType<HmiSystemConfig>("HmiSystemConfig");
    qRegisterMetaType<QVector<double>>("QVector<double>");
    loadAppFonts();

    auto consoleSink = std::make_shared<RDT::ConsoleSink>();
    RDT::Logger::Init({consoleSink}, RDT::LogLevel::Debug);
    std::cout << "HexaStudioNG version " << kHexaStudioNgVersion.data() << " starting" << std::endl;
    RDT::Logger::Info("HexaStudioNG", "--- Application Starting --- Version: {}",
                      kHexaStudioNgVersion.data());

    // The module-owned backend (hexa_backend): implements the BackendClient contract directly - no
    // singleton, no adapter. Everything downstream sees only the BackendClient contract.
    hexa::HexaBackend backend;

    const QString appVersion(kHexaStudioNgVersion);
    hexa::ShellWindow shell(appVersion);
    shell.connectBackend(backend);

    // --- TCP HAL simulator process (host-process concern, outside the backend contract) ---
    QProcess tcpSimProcess;
    tcpSimProcess.setProcessChannelMode(QProcess::SeparateChannels);
    QObject::connect(&tcpSimProcess, &QProcess::stateChanged, &backend,
                     [&backend, &tcpSimProcess](QProcess::ProcessState) {
        backend.notifyTcpSimulatorState(tcpSimProcess.state() != QProcess::NotRunning);
    });
    QObject::connect(&shell.halPanel(), &hexa::HalPanel::tcpHalSimulatorLaunchRequested,
                     &tcpSimProcess, [&tcpSimProcess, &backend]() {
        if (tcpSimProcess.state() != QProcess::NotRunning) {
            return;   // already running; the button state follows the process state signal
        }
        const QString executable = QDir(QCoreApplication::applicationDirPath())
                                       .absoluteFilePath(kTcpSimExecutable);
        tcpSimProcess.setProgram(executable);
        tcpSimProcess.setArguments({QStringLiteral("--transport"), QStringLiteral("tcp"),
                                    QStringLiteral("--ip"), QStringLiteral("127.0.0.1"),
                                    QStringLiteral("--port"), QStringLiteral("30110")});
        tcpSimProcess.setWorkingDirectory(QCoreApplication::applicationDirPath());
        tcpSimProcess.start();
        if (!tcpSimProcess.waitForStarted(kTcpSimStartTimeoutMs)) {
            RDT::Logger::Error("HexaStudioNG",
                               "Failed to launch the TCP HAL simulator ({})",
                               executable.toStdString());
            backend.notifyTcpSimulatorState(false);
        }
    });
    QObject::connect(&shell.halPanel(), &hexa::HalPanel::tcpHalSimulatorStopRequested,
                     &tcpSimProcess, [&tcpSimProcess]() {
        if (tcpSimProcess.state() != QProcess::NotRunning) {
            tcpSimProcess.kill();
        }
    });

    // Publish whatever the service already caches, then connect to the controller using the
    // workstation-local endpoint (same startup flow as the shipping MainWindow).
    backend.publishInitialState();
    QSettings settings(QStringLiteral("HexaStudio"), QStringLiteral("HexaStudio"));
    QString ip = settings.value(QStringLiteral("network/controllerIp"),
                                backend.getConfig().network.controllerIp).toString();
    if (ip.isEmpty()) ip = QStringLiteral("127.0.0.1");
    const int port = settings.value(QStringLiteral("network/controllerPort"),
                                    backend.getConfig().network.controllerPort).toInt();

    // --selftest: construct-and-wire smoke of the REAL-backend assembly (no network wait, no UI
    // loop). Proves the adapter, the shell and the whole panel set link and initialise together.
    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest"))) {
        std::cout << "HexaStudioNG --selftest exit code: 0" << std::endl;
        return 0;
    }

    backend.connectToController(ip, port);

    // --probe <seconds> [--screenshot <file.png>]: end-to-end connection validation against a
    // REAL controller (e.g. a local HexaCore). Runs the full application for the given time, then
    // reports the bridge state and exits 0 (connected) / 1 (not connected). CI-friendly evidence
    // that the whole chain UI -> adapter -> RobotService -> RDT -> controller is alive.
    const QStringList args = QCoreApplication::arguments();
    const int probeIdx = args.indexOf(QStringLiteral("--probe"));
    if (probeIdx >= 0) {
        if (probeIdx + 1 >= args.size()) {
            std::cerr << "HexaStudioNG: --probe requires a duration in seconds" << std::endl;
            return 1;
        }
        const int probeSeconds = args.at(probeIdx + 1).toInt();
        shell.setWindowTitle(
            QStringLiteral("HexaStudio NG %1 - PROBE").arg(kHexaStudioNgVersion));
        shell.show();
        QTimer::singleShot(probeSeconds * 1000, &app, &QCoreApplication::quit);
        app.exec();

        const bool connected = backend.getStatus().top.isConnected;
        const int shotIdx = args.indexOf(QStringLiteral("--screenshot"));
        if (shotIdx >= 0 && shotIdx + 1 < args.size()) {
            if (shell.grab().save(args.at(shotIdx + 1))) {
                std::cout << "HexaStudioNG: probe screenshot saved to "
                          << args.at(shotIdx + 1).toStdString() << std::endl;
            }
        }
        std::cout << "HexaStudioNG --probe result: "
                  << (connected ? "CONNECTED" : "NOT CONNECTED")
                  << " (" << ip.toStdString() << ":" << port << ")" << std::endl;
        if (tcpSimProcess.state() != QProcess::NotRunning) {
            tcpSimProcess.kill();
            tcpSimProcess.waitForFinished(kTcpSimStartTimeoutMs);
        }
        return connected ? 0 : 1;
    }

    shell.setWindowTitle(QStringLiteral("HexaStudio NG %1").arg(kHexaStudioNgVersion));
    shell.showFullScreen();   // full-screen by default (boss request); F11 toggles to a window
    const int exitCode = app.exec();

    if (tcpSimProcess.state() != QProcess::NotRunning) {
        tcpSimProcess.kill();
        tcpSimProcess.waitForFinished(kTcpSimStartTimeoutMs);
    }
    return exitCode;
}
// --- END OF FILE: HexaStudio/app_shell/main_ng.cpp ---
