// --- START OF FILE: HexaStudio/app_shell/bench/app_shell_bench_main.cpp ---
/**
 * @file app_shell_bench_main.cpp
 * @brief The assembled new-product HMI running offline: ShellWindow (composition root) + all five
 *        feature modules against the FakeBackend implementation of the BackendClient contract.
 *        NO RobotService, NO network, NO shipping HexaStudio.
 *
 * Modes: no args = the full interactive application; --selftest = headless integration smoke that
 * drives REAL widgets across module boundaries (speed combo -> backend, jog arm + jog key ->
 * joints, settings overlay open/apply/close, E-Stop toggle + diagnostics history, HAL overlay
 * navigation + armed HAL jog) and exits 0/1; --screenshot <file.png> = renders the assembled
 * window plus "<file>_settings.png" and "<file>_hal.png" overlay frames and exits.
 */
#include <QApplication>
#include <QComboBox>
#include <QFontDatabase>
#include <QLineEdit>
#include <QMouseEvent>
#include <QObject>
#include <QPixmap>
#include <QPushButton>

#include "ShellWindow.h"
#include "FakeBackend.h"
#include "StatusBarPanel.h"
#include "JogPanel.h"
#include "SettingsPanel.h"
#include "DiagnosticsPanel.h"
#include "HalPanel.h"

using hexa::FakeBackend;
using hexa::ShellWindow;

namespace {

// Version of the assembled new-product shell; printed at startup (project version policy).
const QLatin1String kAppShellVersion("0.1.0");

void loadAppFonts() {
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/Michroma-Regular.ttf")) == -1) {
        fprintf(stderr, "app_shell_bench: failed to load Michroma font\n");
    }
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/IBMPlexMono-Regular.ttf")) == -1) {
        fprintf(stderr, "app_shell_bench: failed to load IBM Plex Mono font\n");
    }
}

QString withSuffix(const QString& path, const QString& suffix) {
    QString result = path;
    const int dotIdx = result.lastIndexOf(QLatin1Char('.'));
    if (dotIdx >= 0) {
        result.insert(dotIdx, suffix);
    } else {
        result.append(suffix + QStringLiteral(".png"));
    }
    return result;
}

bool saveGrab(QWidget& widget, const QString& path) {
    QApplication::processEvents();
    if (!widget.grab().save(path)) {
        fprintf(stderr, "app_shell_bench: failed to save screenshot to %s\n", qPrintable(path));
        return false;
    }
    fprintf(stdout, "app_shell_bench: screenshot saved to %s\n", qPrintable(path));
    return true;
}

// Simulate an operator click on a plain (non-button) widget, e.g. the status label that opens
// the diagnostics card.
void clickWidget(QWidget* widget) {
    const QPointF centre(widget->width() / 2.0, widget->height() / 2.0);
    QMouseEvent press(QEvent::MouseButtonPress, centre, widget->mapToGlobal(centre.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &press);
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    fprintf(stdout, "app_shell version %s starting\n", kAppShellVersion.data());
    loadAppFonts();

    FakeBackend backend;
    const QString appVersion(kAppShellVersion);
    ShellWindow shell(appVersion);
    shell.connectBackend(backend);

    // Host-process concerns outside the BackendClient contract, consumed by the bench:
    QObject::connect(&shell.halPanel(), &hexa::HalPanel::tcpHalSimulatorLaunchRequested,
                     &backend, [&backend]() { backend.setTcpSimulatorRunning(true); });
    QObject::connect(&shell.halPanel(), &hexa::HalPanel::tcpHalSimulatorStopRequested,
                     &backend, [&backend]() { backend.setTcpSimulatorRunning(false); });
    QObject::connect(&backend, &hexa::BackendClient::messageOccurred, &backend,
                     [](const QString& text, bool isError) {
        fprintf(stdout, "app_shell_bench: [%s] %s\n", isError ? "ERROR" : "info",
                qPrintable(text));
    });

    backend.publishAll();

    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest"))) {
        // ---- 1. Speed: status bar combo -> backend override ------------------------------
        QComboBox* speed = shell.statusBar().findChild<QComboBox*>(QStringLiteral("comboSpeed"));
        if (speed == nullptr) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: comboSpeed not found\n");
            return 1;
        }
        speed->setCurrentText(QStringLiteral("25%"));
        if (backend.status().motion.speedOverride != 0.25) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: speed override did not reach the "
                            "backend (%.2f)\n", backend.status().motion.speedOverride);
            return 1;
        }

        // ---- 2. Jog: arm via the REAL JOG button, jog A1 via the REAL key ----------------
        QPushButton* jogArm = shell.jogPanel().findChild<QPushButton*>(QStringLiteral("btnJogArm"));
        QPushButton* jogPlusA1 =
            shell.jogPanel().findChild<QPushButton*>(QStringLiteral("jogPlus_0"));
        if (jogArm == nullptr || jogPlusA1 == nullptr) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: jog widgets not found\n");
            return 1;
        }
        jogPlusA1->click();   // disarmed: the key is disabled, click() is a no-op
        if (backend.status().motion.displayJoints[0] != 0.0) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: jog reached the backend while "
                            "DISARMED\n");
            return 1;
        }
        jogArm->click();
        jogPlusA1->click();
        if (backend.status().motion.displayJoints[0] != 1.0) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: armed jog did not move A1 "
                            "(%.2f)\n", backend.status().motion.displayJoints[0]);
            return 1;
        }

        // ---- 3. Settings: open from the status bar, apply through the editor, close ------
        QPushButton* settingsBtn =
            shell.statusBar().findChild<QPushButton*>(QStringLiteral("btnSettings"));
        if (settingsBtn == nullptr) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: btnSettings not found\n");
            return 1;
        }
        settingsBtn->click();
        // Headless note: the shell window itself is never shown in selftest mode, so absolute
        // isVisible() is always false - visibility is asserted relative to the window.
        if (!shell.settingsPanel().isVisibleTo(&shell)) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: settings overlay did not open\n");
            return 1;
        }
        QLineEdit* ip =
            shell.settingsPanel().findChild<QLineEdit*>(QStringLiteral("editNetworkIp"));
        QPushButton* apply =
            shell.settingsPanel().findChild<QPushButton*>(QStringLiteral("btnApply"));
        QPushButton* close =
            shell.settingsPanel().findChild<QPushButton*>(QStringLiteral("btnClose"));
        if (ip == nullptr || apply == nullptr || close == nullptr) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: settings widgets not found\n");
            return 1;
        }
        ip->setText(QStringLiteral("10.20.30.40"));
        apply->click();
        if (backend.config().network.controllerIp != QStringLiteral("10.20.30.40")) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: applied config did not reach the "
                            "backend\n");
            return 1;
        }
        close->click();
        if (shell.settingsPanel().isVisibleTo(&shell)) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: settings overlay did not close\n");
            return 1;
        }

        // ---- 4. E-Stop toggle + diagnostics history ---------------------------------------
        QPushButton* eStop = shell.statusBar().findChild<QPushButton*>(QStringLiteral("btnEStop"));
        if (eStop == nullptr) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: btnEStop not found\n");
            return 1;
        }
        eStop->click();
        if (!backend.status().top.isEStop) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: E-STOP did not engage\n");
            return 1;
        }
        eStop->click();
        if (backend.status().top.isEStop) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: E-STOP did not release\n");
            return 1;
        }
        bool sawEStopEvent = false;
        const QVector<hexa::DiagEvent>& events = shell.diagnosticsPanel().log().events();
        for (const hexa::DiagEvent& event : events) {
            if (event.source == QLatin1String("E-STOP")) sawEStopEvent = true;
        }
        if (!sawEStopEvent) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: diagnostics history has no "
                            "E-STOP events (%d events)\n", int(events.size()));
            return 1;
        }
        // Diagnostics card opens from the status indicator (a label, clicked like an operator).
        QLabel* statusLabel =
            shell.statusBar().findChild<QLabel*>(QStringLiteral("lblSystemStatus"));
        if (statusLabel == nullptr) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: lblSystemStatus not found\n");
            return 1;
        }
        clickWidget(statusLabel);
        if (!shell.diagnosticsPanel().isVisibleTo(&shell)) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: diagnostics did not open\n");
            return 1;
        }
        clickWidget(statusLabel);   // toggle closed again

        // ---- 5. HAL: navigate from settings, armed HAL jog reaches the same joints -------
        settingsBtn->click();
        QPushButton* halNav =
            shell.settingsPanel().findChild<QPushButton*>(QStringLiteral("btnHalRuntime"));
        if (halNav == nullptr) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: btnHalRuntime not found\n");
            return 1;
        }
        halNav->click();
        if (!shell.halPanel().isVisibleTo(&shell) ||
            shell.settingsPanel().isVisibleTo(&shell)) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: settings -> HAL navigation "
                            "failed\n");
            return 1;
        }
        QPushButton* halArm =
            shell.halPanel().findChild<QPushButton*>(QStringLiteral("btnHalJogArm"));
        QPushButton* halJogPlusA1 =
            shell.halPanel().findChild<QPushButton*>(QStringLiteral("halJogPlus_0"));
        if (halArm == nullptr || halJogPlusA1 == nullptr) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: HAL jog widgets not found\n");
            return 1;
        }
        halArm->click();   // toggles OFF the arm set in step 2? No: HAL owns its own local arm.
        halJogPlusA1->click();
        if (backend.status().motion.displayJoints[0] != 2.0) {
            fprintf(stderr, "app_shell_bench --selftest FAILED: HAL jog did not reach the same "
                            "joint state (A1 = %.2f, expected 2.0)\n",
                    backend.status().motion.displayJoints[0]);
            return 1;
        }

        fprintf(stdout, "app_shell_bench --selftest exit code: 0\n");
        return 0;
    }

    const QStringList args = QCoreApplication::arguments();
    const int shotIdx = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIdx >= 0) {
        if (shotIdx + 1 >= args.size()) {
            fprintf(stderr, "app_shell_bench: --screenshot requires an output file path\n");
            return 1;
        }
        const QString basePath = args.at(shotIdx + 1);
        shell.show();

        // Frame 1: the assembled application.
        if (!saveGrab(shell, basePath)) return 1;

        // Frame 2: settings overlay open.
        QPushButton* settingsBtn =
            shell.statusBar().findChild<QPushButton*>(QStringLiteral("btnSettings"));
        if (settingsBtn == nullptr) {
            fprintf(stderr, "app_shell_bench: btnSettings not found\n");
            return 1;
        }
        settingsBtn->click();
        if (!saveGrab(shell, withSuffix(basePath, QStringLiteral("_settings")))) return 1;

        // Frame 3: HAL overlay (navigated from settings, connected).
        QPushButton* halNav =
            shell.settingsPanel().findChild<QPushButton*>(QStringLiteral("btnHalRuntime"));
        if (halNav == nullptr) {
            fprintf(stderr, "app_shell_bench: btnHalRuntime not found\n");
            return 1;
        }
        halNav->click();
        backend.connectHalEndpoint(QStringLiteral("127.0.0.1"), 30110);
        if (!saveGrab(shell, withSuffix(basePath, QStringLiteral("_hal")))) return 1;
        return 0;
    }

    shell.setWindowTitle(
        QStringLiteral("HexaStudio NG %1 — OFFLINE BENCH (FakeBackend: wiring only, NO motion/trajectory "
                       "simulation — review motion on the live HexaCore stack)").arg(kAppShellVersion));
    shell.show();
    return app.exec();
}
// --- END OF FILE: HexaStudio/app_shell/bench/app_shell_bench_main.cpp ---
