// --- START OF FILE: HexaStudio/app_shell/main_desktop.cpp ---
/**
 * @file main_desktop.cpp
 * @brief HexaStudioDesktop - the single-process edition: the HexaMotion controller stack and the
 *        HexaStudio HMI in ONE executable, linked by the in-process loopback transport.
 *
 * Composition-root duties only (mirrors main_ng.cpp; see ARCHITECTURE.md §4,
 * docs/REQ_hexastudio_desktop.md):
 *   - meta types, theme fonts, shared logger, version banner;
 *   - the LoopbackHub + the shared ControllerRuntime (the exact stack HexaCore.exe runs) with an
 *     injected LoopbackNetworkServer - no TCP socket, no firewall interaction;
 *   - hexa::HexaBackend with an injected LoopbackNetworkClient factory - the HMI side of the SAME
 *     RDT protocol, byte-identical to the networked edition;
 *   - the TCP HAL simulator QProcess (host-process concern, used by the MKS backend selection).
 *
 * Destruction order is the wiring order reversed and is load-bearing: the hub is declared FIRST
 * so it outlives every endpoint attached to it; the backend disconnects before the runtime stops.
 */
#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QIcon>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>

#include <filesystem>
#include <iostream>
#include <memory>

#include "ShellWindow.h"
#include "HexaBackend.h"
#include "HalPanel.h"

#include "ControllerRuntime.h"
#include "LoopbackNetwork.h"
#include "Logger.h"

namespace {

// HexaStudioDesktop version, printed at startup (project version policy).
// 0.1.0: first integrated build - ControllerRuntime (HexaCore 0.1.20 stack) + the full
//        HexaStudioNG 0.6.29 panel set in one process over the loopback transport; SIM HAL
//        backend out of the box; --selftest smoke flag.
// 0.1.1: active robot = HexaArmMedium_Light (re-anchored URDF, 16 mm flange, GLB visuals);
//        viewport honours the base link's authored visual origin (shared viewport3d fix,
//        HexaStudioNG 0.6.31).
// 0.1.2: viewport visual retune (HexaStudioNG 0.6.32): neutral studio lighting (no yellow cast);
//        robot package 1.0.3 GLB materials - aluminum body, black joint covers.
// 0.1.3: product icon (REQ-INST-16): hexagon-H mark embedded in the exe (app_icon.rc -> Explorer,
//        shortcuts) and set as the application window icon (taskbar / title bar).
// 0.1.4: sim backend = virtual Motor Configurator (shared stack change, HexaCore 0.1.22 +
//        HexaStudioNG 0.6.33): live ghost on the sim backend, unified HAL-panel MC position
//        source. See docs/REQ_sim_backend_virtual_mc.md.
// 0.1.5: official HexaKinetica brand logo (hexagon + robot arm) replaces the interim 0.1.3 mark:
//        exe icon, window/taskbar icon and installer wizard branding are all derived from the
//        checked-in master packaging/assets/HexaKinetica_logo.png.
const QLatin1String kHexaStudioDesktopVersion("0.1.5");

constexpr int kTcpSimStartTimeoutMs = 3000;
const QLatin1String kTcpSimExecutable("HexaHAL_Client.exe");

void loadAppFonts() {
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/Michroma-Regular.ttf")) == -1) {
        std::cerr << "HexaStudioDesktop: failed to load Michroma font" << std::endl;
    }
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/IBMPlexMono-Regular.ttf")) == -1) {
        std::cerr << "HexaStudioDesktop: failed to load IBM Plex Mono font" << std::endl;
    }
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Window/taskbar icon. The same .ico is embedded in the executable by app_icon.rc
    // (Explorer, shortcuts); Qt does not read the exe resource, so it is set explicitly
    // from the bundled Qt resource. A missing resource is a packaging defect, not fatal.
    app.setWindowIcon(QIcon(QStringLiteral(":/resources/HexaStudioDesktop.ico")));

    qRegisterMetaType<HmiRobotStatus>("HmiRobotStatus");
    qRegisterMetaType<HmiSystemConfig>("HmiSystemConfig");
    qRegisterMetaType<QVector<double>>("QVector<double>");
    loadAppFonts();

    // Shared logger: console + file. The file goes to logs/ under the CWD - run_desktop.bat starts
    // the app in the HexaMotion repo root, so controller and HMI diagnostics land side by side.
    {
        std::error_code ec;
        std::filesystem::create_directories("logs", ec);
    }
    auto consoleSink = std::make_shared<RDT::ConsoleSink>();
    auto fileSink = std::make_shared<RDT::FileSink>("logs/hexadesktop_debug.log");
    RDT::Logger::Init({consoleSink, fileSink}, RDT::LogLevel::Debug);
    std::cout << "HexaStudioDesktop version " << kHexaStudioDesktopVersion.data() << " starting" << std::endl;
    RDT::Logger::Info("HexaStudioDesktop", "--- Application Starting --- Version: {}",
                      kHexaStudioDesktopVersion.data());

    // --- The in-process link. Declared FIRST: the hub must outlive every endpoint (runtime's
    // server transport and every client the backend's reconnect loop creates).
    RDT::LoopbackHub hub;

    // --- The controller stack: the EXACT assembly HexaCore.exe runs (ControllerRuntime), with the
    // loopback server injected as its RDT transport. Same configs, same programs library, same
    // fail-closed policies; the SIM HAL backend works out of the box.
    RDT::ControllerRuntimeOptions options;
    options.config_path = RDT::ControllerRuntime::resolveConfigPath(argc, argv);
    options.runtime_config_path = RDT::ControllerRuntime::resolveRuntimeConfigPath(argc, argv);
    options.rdt_transport = std::make_unique<RDT::LoopbackNetworkServer>(hub);

    auto runtime_result = RDT::ControllerRuntime::create(std::move(options));
    if (runtime_result.isError()) {
        const QString reason = QString::fromStdString(RDT::ToString(runtime_result.error()));
        RDT::Logger::Critical("HexaStudioDesktop", "Controller startup refused: {}.",
                              reason.toStdString());
        QMessageBox::critical(nullptr, QStringLiteral("HexaStudioDesktop"),
                              QStringLiteral("Controller startup refused: %1.\n"
                                             "See logs/hexadesktop_debug.log for details.").arg(reason));
        RDT::Logger::Shutdown();
        return 1;
    }
    auto runtime = std::move(runtime_result).value();
    runtime->start();   // NRT loop on its own thread; the RT loop is owned by MotionManager

    // --- The HMI backend: the SAME production HexaBackend, its RdtClient fed by loopback clients.
    hexa::HexaBackend backend(nullptr, [&hub]() -> std::unique_ptr<RDT::INetworkClient> {
        return std::make_unique<RDT::LoopbackNetworkClient>(hub);
    });

    const QString appVersion(kHexaStudioDesktopVersion);
    hexa::ShellWindow shell(appVersion);
    shell.connectBackend(backend);

    // --- TCP HAL simulator process (host-process concern; used with the MKS backend selection) ---
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
            RDT::Logger::Error("HexaStudioDesktop",
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

    // Publish the cached state, then connect over the loopback. No workstation endpoint settings:
    // the desktop edition has exactly one controller and it lives in this process (the "address"
    // is only a port tag the loopback verifies).
    backend.publishInitialState();

    // --selftest: construct-and-wire smoke of the WHOLE single-process assembly (controller stack
    // up, loopback server listening, shell + panels built). No UI loop.
    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest"))) {
        std::cout << "HexaStudioDesktop --selftest exit code: 0" << std::endl;
        backend.disconnectFromController();
        runtime->stop();
        return 0;
    }

    backend.connectToController(QStringLiteral("loopback"), runtime->rdtPort());

    shell.setWindowTitle(QStringLiteral("HexaStudio Desktop %1").arg(kHexaStudioDesktopVersion));
    shell.showFullScreen();   // full-screen by default (product policy); F11 toggles to a window
    const int exitCode = app.exec();

    // Teardown in wiring-reverse order: HMI link first, then the controller stack. The hub (first
    // declared) is destroyed last, after every endpoint is gone.
    if (tcpSimProcess.state() != QProcess::NotRunning) {
        tcpSimProcess.kill();
        tcpSimProcess.waitForFinished(kTcpSimStartTimeoutMs);
    }
    backend.disconnectFromController();
    runtime->stop();
    RDT::Logger::Info("HexaStudioDesktop", "Shutdown complete.");
    return exitCode;
}
// --- END OF FILE: HexaStudio/app_shell/main_desktop.cpp ---
