// --- START OF FILE: HexaStudio/hal_control/bench/hal_control_bench_main.cpp ---
/**
 * @file hal_control_bench_main.cpp
 * @brief Standalone bench hosting the module-owned HalPanel + FakeHalController, NO RobotService,
 *        NO network, NO HexaStudio.
 *
 * Wires the HAL panel exactly the way MainWindow does, but against the offline FakeHalController.
 * The host window paints the application background and loads the application fonts. Direct HAL
 * access matters for MKS bring-up: no need to navigate through the settings overlay.
 *
 * Modes: no args = interactive; --selftest = headless smoke of the jog gates through the REAL
 * widgets (blocked while disarmed, reaches the seam after arming, blocked on a faulted axis) plus
 * the connect round-trip; --screenshot <file.png> = renders the connected panel (axis badge
 * variety) and exits.
 */
#include <QApplication>
#include <QFontDatabase>
#include <QObject>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "HalPanel.h"
#include "FakeHalController.h"
#include "BackendTypes.h"
#include "HexaTheme.h"

using hexa::FakeHalController;
using hexa::HalPanel;

namespace {

constexpr int kBenchWidthPx = 1320;
constexpr int kBenchHeightPx = 800;

// Wire the panel <-> controller the same shape MainWindow uses with RobotService. EVERY panel
// signal lands in a consumer: an intent without a consumer is a dead control on the bench.
void wire(HalPanel& panel, FakeHalController& fc) {
    // feedback
    QObject::connect(&fc, &FakeHalController::halConfigChanged,
                     &panel, &HalPanel::setHalConfigCurrent);
    QObject::connect(&fc, &FakeHalController::robotStateChanged,
                     &panel, &HalPanel::onRobotStateChanged);

    // intents
    QObject::connect(&panel, &HalPanel::applyHalRequested,
                     &fc, &FakeHalController::applyHalConfig);
    QObject::connect(&panel, &HalPanel::halConnectRequested, &fc, &FakeHalController::connectHal);
    QObject::connect(&panel, &HalPanel::halDisconnectRequested,
                     &fc, &FakeHalController::disconnectHal);
    QObject::connect(&panel, &HalPanel::halJogRequested, &fc, &FakeHalController::halJog);
    QObject::connect(&panel, &HalPanel::homingRequested, &fc, &FakeHalController::startHoming);
    QObject::connect(&panel, &HalPanel::setZeroRequested, &fc, &FakeHalController::setZeroAxis);
    QObject::connect(&panel, &HalPanel::setZeroAllRequested, &fc, &FakeHalController::setZeroAll);
    QObject::connect(&panel, &HalPanel::clearErrorsRequested, &fc, &FakeHalController::clearErrors);
    QObject::connect(&panel, &HalPanel::jogArmRequested, &fc, &FakeHalController::setJogArmed);
    QObject::connect(&panel, &HalPanel::eStopRequested, &fc, &FakeHalController::requestEStop);
    // Shell-owned intents (overlay navigation, TCP-sim process): acknowledged on the console; the
    // sim-running state round-trips so the START/STOP buttons flip like in the app.
    QObject::connect(&panel, &HalPanel::closeRequested, &fc, []() {
        fprintf(stdout, "hal_control_bench: closeRequested\n");
    });
    QObject::connect(&panel, &HalPanel::tcpHalSimulatorLaunchRequested, &panel, [&panel]() {
        fprintf(stdout, "hal_control_bench: tcpHalSimulatorLaunchRequested\n");
        panel.setTcpSimulatorRunning(true);
    });
    QObject::connect(&panel, &HalPanel::tcpHalSimulatorStopRequested, &panel, [&panel]() {
        fprintf(stdout, "hal_control_bench: tcpHalSimulatorStopRequested\n");
        panel.setTcpSimulatorRunning(false);
    });

    // Initial sync: the host pushes everything (the shipping overlay pulled the singleton).
    panel.setHalConfigCurrent(fc.halConfig());
    panel.onRobotStateChanged(fc.robotStatus());
    panel.setTcpSimulatorRunning(false);
}

void loadAppFonts() {
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/Michroma-Regular.ttf")) == -1) {
        fprintf(stderr, "hal_control_bench: failed to load Michroma font\n");
    }
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/IBMPlexMono-Regular.ttf")) == -1) {
        fprintf(stderr, "hal_control_bench: failed to load IBM Plex Mono font\n");
    }
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    loadAppFonts();

    FakeHalController fc;

    QWidget host;
    host.setAttribute(Qt::WA_StyledBackground, true);
    host.setStyleSheet(QStringLiteral("background-color: %1;").arg(Hexa::Colors::Background));
    QVBoxLayout* hostLayout = new QVBoxLayout(&host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    HalPanel* panel = new HalPanel(&host);
    hostLayout->addWidget(panel);
    wire(*panel, fc);

    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest"))) {
        // Jog gates through the REAL widgets. A1 is Enabled in the demo config; jog must be
        // BLOCKED while disarmed (the button is disabled, click() is a no-op) and reach the seam
        // after arming; a faulted axis stays blocked even while armed.
        QPushButton* jogPlusA1 = panel->findChild<QPushButton*>(QStringLiteral("halJogPlus_0"));
        QPushButton* jogArm = panel->findChild<QPushButton*>(QStringLiteral("btnHalJogArm"));
        QPushButton* jogPlusA3 = panel->findChild<QPushButton*>(QStringLiteral("halJogPlus_2"));
        if (jogPlusA1 == nullptr || jogArm == nullptr || jogPlusA3 == nullptr) {
            fprintf(stderr, "hal_control_bench --selftest FAILED: jog widgets not found\n");
            return 1;
        }
        jogPlusA1->click();
        if (fc.halJogCount() != 0) {
            fprintf(stderr, "hal_control_bench --selftest FAILED: jog reached the seam while "
                            "DISARMED\n");
            return 1;
        }
        jogArm->click();
        jogPlusA1->click();
        if (fc.halJogCount() != 1 || fc.lastHalJogAxis() != 0 || fc.lastHalJogStep() != 1.0) {
            fprintf(stderr, "hal_control_bench --selftest FAILED: armed jog did not reach the seam "
                            "(count %d, axis %d, step %f)\n",
                    fc.halJogCount(), fc.lastHalJogAxis(), fc.lastHalJogStep());
            return 1;
        }
        jogPlusA3->click();
        if (fc.halJogCount() != 1) {
            fprintf(stderr, "hal_control_bench --selftest FAILED: jog on a FAULTED axis reached "
                            "the seam\n");
            return 1;
        }
        // Connect round-trip through the REAL button.
        QPushButton* connectBtn = panel->findChild<QPushButton*>(QStringLiteral("btnHalConnect"));
        if (connectBtn == nullptr) {
            fprintf(stderr, "hal_control_bench --selftest FAILED: connect button not found\n");
            return 1;
        }
        connectBtn->click();
        if (!fc.halConfig().transportConnected) {
            fprintf(stderr, "hal_control_bench --selftest FAILED: connect intent did not reach "
                            "the controller seam\n");
            return 1;
        }
        fprintf(stdout, "hal_control_bench --selftest exit code: 0\n");
        return 0;
    }

    const QStringList args = QCoreApplication::arguments();
    const int shotIdx = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIdx >= 0) {
        if (shotIdx + 1 >= args.size()) {
            fprintf(stderr, "hal_control_bench: --screenshot requires an output file path\n");
            return 1;
        }
        // Staged review state: connected, axis badge variety from the demo config.
        fc.connectHal(QStringLiteral("127.0.0.1"), 30110);
        host.resize(kBenchWidthPx, kBenchHeightPx);
        host.show();
        QApplication::processEvents();
        if (!host.grab().save(args.at(shotIdx + 1))) {
            fprintf(stderr, "hal_control_bench: failed to save screenshot to %s\n",
                    qPrintable(args.at(shotIdx + 1)));
            return 1;
        }
        fprintf(stdout, "hal_control_bench: screenshot saved to %s\n",
                qPrintable(args.at(shotIdx + 1)));
        return 0;
    }

    host.setWindowTitle(QStringLiteral("HAL Control Bench (standalone, FakeHalController)"));
    host.resize(kBenchWidthPx, kBenchHeightPx);
    host.show();
    return app.exec();
}
// --- END OF FILE: HexaStudio/hal_control/bench/hal_control_bench_main.cpp ---
