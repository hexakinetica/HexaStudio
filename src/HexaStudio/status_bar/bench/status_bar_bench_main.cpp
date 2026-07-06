// --- START OF FILE: HexaStudio/status_bar/bench/status_bar_bench_main.cpp ---
/**
 * @file status_bar_bench_main.cpp
 * @brief Standalone bench hosting the module-owned StatusBarPanel + FakeTopController, NO
 *        RobotService, NO network, NO HexaStudio.
 *
 * Wires the panel exactly the way MainWindow does, but against the offline FakeTopController. The
 * host window paints the application background and loads the application fonts, so the
 * (translucent-by-design) panel is reviewed exactly as it will look in the app.
 *
 * Modes: no args = interactive; --selftest = headless smoke that drives the REAL speed combo and the
 * E-Stop feedback path and exits 0/1; --screenshot <file.png> = renders five frames (controls view,
 * "<file>_stats.png" system-stats view, "<file>_estop.png" E-Stop active, "<file>_confirm.png"
 * safety dialog, "<file>_narrow.png" minimum-width bar) and exits.
 */
#include <QApplication>
#include <QComboBox>
#include <QFontDatabase>
#include <QObject>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "StatusBarPanel.h"
#include "ConfirmDialog.h"
#include "FakeTopController.h"
#include "BackendTypes.h"
#include "HexaTheme.h"

using hexa::FakeTopController;
using hexa::StatusBarPanel;

namespace {

// The top bar spans the full app width; MainWindow is typically ~1280 wide.
constexpr int kBenchWidthPx = 1100;
constexpr int kBenchHeightPx = 90;
// Narrow render for the no-crowding check: the bar must lay out cleanly near its minimum width.
constexpr int kBenchNarrowWidthPx = 1000;

// Wire the panel <-> controller the same shape MainWindow uses with RobotService. EVERY panel signal
// lands in a consumer: an intent without a consumer is a dead control on the bench.
void wire(StatusBarPanel& panel, FakeTopController& fc) {
    // controller feedback -> panel (display only)
    QObject::connect(&fc, &FakeTopController::configReceived,
                     &panel, &StatusBarPanel::onConfigReceived);
    // The bench has no program, so isProgramPaused stays false (PMF connects ignore default
    // arguments, hence the explicit lambda).
    QObject::connect(&fc, &FakeTopController::statusChanged, &panel,
                     [&panel](const HmiTopStatus& status, bool isMoving) {
                         panel.updateState(status, isMoving, false);
                     });

    // panel intent -> controller
    QObject::connect(&panel, &StatusBarPanel::modeChanged, &fc, &FakeTopController::setMode);
    QObject::connect(&panel, &StatusBarPanel::speedChanged, &fc, &FakeTopController::setSpeedOverride);
    // Mirrors the MainWindow lambda: E-Stop toggles - active resets, inactive activates.
    QObject::connect(&panel, &StatusBarPanel::eStopRequested, &fc, [&fc]() {
        fc.setEStop(!fc.status().isEStop);
    });
    // Settings / diagnostics overlays are shell-owned (MainWindow); the bench acknowledges the
    // intents visibly on the console.
    QObject::connect(&panel, &StatusBarPanel::settingsRequested, &fc, []() {
        fprintf(stdout, "status_bar_bench: settingsRequested\n");
    });
    QObject::connect(&panel, &StatusBarPanel::diagnosticsRequested, &fc, []() {
        fprintf(stdout, "status_bar_bench: diagnosticsRequested\n");
    });

    // Initial sync: config first (About-box IP), then status.
    panel.setAppVersion(QStringLiteral("bench"));
    panel.onConfigReceived(fc.demoConfig());
    panel.updateState(fc.status(), fc.isMoving());
}

void loadAppFonts() {
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/Michroma-Regular.ttf")) == -1) {
        fprintf(stderr, "status_bar_bench: failed to load Michroma font\n");
    }
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/IBMPlexMono-Regular.ttf")) == -1) {
        fprintf(stderr, "status_bar_bench: failed to load IBM Plex Mono font\n");
    }
}

// Insert a suffix before the file extension ("shot.png" + "_estop" -> "shot_estop.png").
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
        fprintf(stderr, "status_bar_bench: failed to save screenshot to %s\n", qPrintable(path));
        return false;
    }
    fprintf(stdout, "status_bar_bench: screenshot saved to %s\n", qPrintable(path));
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    loadAppFonts();

    FakeTopController fc;

    // Host window paints the application background (the panel is translucent by design).
    QWidget host;
    host.setAttribute(Qt::WA_StyledBackground, true);
    host.setStyleSheet(QStringLiteral("background-color: %1;").arg(Hexa::Colors::Background));
    QVBoxLayout* hostLayout = new QVBoxLayout(&host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    StatusBarPanel* panel = new StatusBarPanel(&host);
    hostLayout->addWidget(panel);
    wire(*panel, fc);

    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest"))) {
        // Speed intent through the REAL combo. 25% stays under the >50% confirmation threshold - a
        // blocking QMessageBox would hang a headless run.
        QComboBox* speed = panel->findChild<QComboBox*>(QStringLiteral("comboSpeed"));
        if (speed == nullptr) {
            fprintf(stderr, "status_bar_bench --selftest FAILED: comboSpeed not found\n");
            return 1;
        }
        speed->setCurrentText(QStringLiteral("25%"));
        if (fc.speedPercent() != 25) {
            fprintf(stderr, "status_bar_bench --selftest FAILED: speed intent did not reach the "
                            "controller seam (expected 25, got %d)\n", fc.speedPercent());
            return 1;
        }

        // E-Stop feedback path: controller-side E-Stop must flip the button to RESET.
        QPushButton* estop = panel->findChild<QPushButton*>(QStringLiteral("btnEStop"));
        if (estop == nullptr) {
            fprintf(stderr, "status_bar_bench --selftest FAILED: btnEStop not found\n");
            return 1;
        }
        fc.setEStop(true);
        if (estop->text() != QStringLiteral("RESET")) {
            fprintf(stderr, "status_bar_bench --selftest FAILED: E-Stop status did not switch the "
                            "button to RESET (text: '%s')\n", qPrintable(estop->text()));
            return 1;
        }
        fc.setEStop(false);
        fprintf(stdout, "status_bar_bench --selftest exit code: 0\n");
        return 0;
    }

    const QStringList args = QCoreApplication::arguments();
    const int shotIdx = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIdx >= 0) {
        if (shotIdx + 1 >= args.size()) {
            fprintf(stderr, "status_bar_bench: --screenshot requires an output file path\n");
            return 1;
        }
        const QString basePath = args.at(shotIdx + 1);
        host.resize(kBenchWidthPx, kBenchHeightPx);
        host.show();

        // Frame 1: default controls view (SYSTEM READY).
        if (!saveGrab(host, basePath)) return 1;

        // Frame 2: system-stats view, toggled via the real STATS button.
        QPushButton* statsButton = panel->findChild<QPushButton*>(QStringLiteral("btnStats"));
        if (statsButton == nullptr) {
            fprintf(stderr, "status_bar_bench: btnStats not found\n");
            return 1;
        }
        statsButton->click();
        if (!saveGrab(host, withSuffix(basePath, QStringLiteral("_stats")))) return 1;

        // Frame 3: back to the controls view, E-Stop active (danger styling + RESET + locked mode
        // switch). click() toggles the stack regardless of the button's current visibility.
        statsButton->click();
        fc.setEStop(true);
        if (!saveGrab(host, withSuffix(basePath, QStringLiteral("_estop")))) return 1;
        fc.setEStop(false);

        // Frame 4: the pendant-styled safety confirmation (REAL-mode content), rendered standalone.
        hexa::ConfirmDialog confirm(&host);
        confirm.setContent(QStringLiteral("SAFETY WARNING"),
                           QStringLiteral("Switching to REAL ROBOT mode.\n"
                                          "Ensure the workspace is clear."),
                           QStringLiteral("SWITCH TO REAL"));
        confirm.show();
        if (!saveGrab(confirm, withSuffix(basePath, QStringLiteral("_confirm")))) return 1;
        confirm.close();

        // Frame 5: narrowed bar (controls view). The PNG width itself is the check: if the layout
        // minimum exceeded the requested width, the window (and the render) would come out wider.
        host.resize(kBenchNarrowWidthPx, kBenchHeightPx);
        if (!saveGrab(host, withSuffix(basePath, QStringLiteral("_narrow")))) return 1;
        return 0;
    }

    host.setWindowTitle(QStringLiteral("Status Bar Bench (standalone, FakeTopController)"));
    host.resize(kBenchWidthPx, kBenchHeightPx);
    host.show();
    return app.exec();
}
// --- END OF FILE: HexaStudio/status_bar/bench/status_bar_bench_main.cpp ---
