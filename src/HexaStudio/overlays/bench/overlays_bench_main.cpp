// --- START OF FILE: HexaStudio/overlays/bench/overlays_bench_main.cpp ---
/**
 * @file overlays_bench_main.cpp
 * @brief Standalone bench hosting the module-owned SettingsPanel + FakeOverlaysController, NO
 *        RobotService, NO network, NO HexaStudio.
 *
 * Wires the overlays exactly the way MainWindow does, but against the offline
 * FakeOverlaysController. The host window paints the application background and loads the
 * application fonts. (The HAL runtime panel is the separate hal_control module with its own
 * bench.)
 *
 * Modes: no args = interactive settings overlay; --diagnostics = interactive diagnostics panel with
 * a deterministic fault scenario cycling (HAL warning -> safety fault -> recovery) so the event log
 * fills live; --selftest = headless smoke (settings apply round-trip + diagnostics event log,
 * badges, reset gating - exit 0/1); --screenshot <file.png> = renders the NETWORK page,
 * "<file>_limits.png", "<file>_tools.png" (GRIPPER slot) and "<file>_diag.png" (diagnostics with a
 * staged fault history) and exits.
 */
#include <QApplication>
#include <QComboBox>
#include <QFontDatabase>
#include <QLineEdit>
#include <QListWidget>
#include <QObject>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "SettingsPanel.h"
#include "DiagnosticsPanel.h"
#include "FakeOverlaysController.h"
#include "BackendTypes.h"
#include "HexaTheme.h"

using hexa::DiagnosticsPanel;
using hexa::FakeOverlaysController;
using hexa::SettingsPanel;

namespace {

// The overlays dim the whole app window; host them at a typical app-window size.
constexpr int kBenchWidthPx = 1280;
constexpr int kBenchHeightPx = 760;

// Wire the overlay <-> controller the same shape MainWindow uses with RobotService. EVERY
// overlay signal lands in a consumer: an intent without a consumer is a dead control on the
// bench.
void wire(SettingsPanel& panel, FakeOverlaysController& fc) {
    QObject::connect(&fc, &FakeOverlaysController::configReceived,
                     &panel, &SettingsPanel::setConfig);
    QObject::connect(&panel, &SettingsPanel::applyRequested,
                     &fc, &FakeOverlaysController::applyConfig);
    // Shell-owned navigation intents: acknowledged visibly on the console (no shell on the
    // bench; the HAL runtime panel is the separate hal_control module).
    QObject::connect(&panel, &SettingsPanel::closeRequested, &fc, []() {
        fprintf(stdout, "overlays_bench: closeRequested\n");
    });
    QObject::connect(&panel, &SettingsPanel::halOverlayRequested, &fc, []() {
        fprintf(stdout, "overlays_bench: halOverlayRequested\n");
    });

    // Initial sync: the host pushes the config (the shipping overlay pulled the singleton).
    panel.setConfig(fc.demoConfig());
}

// Wire the diagnostics panel the same shape MainWindow uses (status feed in; reset/E-Stop out).
void wireDiagnostics(DiagnosticsPanel& panel, FakeOverlaysController& fc) {
    QObject::connect(&fc, &FakeOverlaysController::robotStateChanged,
                     &panel, &DiagnosticsPanel::updateStatus);
    QObject::connect(&panel, &DiagnosticsPanel::clearErrorRequested,
                     &fc, &FakeOverlaysController::clearError);
    // Mirrors the MainWindow semantics: the E-Stop button toggles engage/release.
    QObject::connect(&panel, &DiagnosticsPanel::eStopRequested,
                     &fc, &FakeOverlaysController::toggleEStop);
    QObject::connect(&panel, &DiagnosticsPanel::closeRequested, &fc, []() {
        fprintf(stdout, "overlays_bench: closeRequested (diagnostics)\n");
    });

    // Initial sync: the host pushes the status stream.
    panel.updateStatus(fc.robotStatus());
}

void loadAppFonts() {
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/Michroma-Regular.ttf")) == -1) {
        fprintf(stderr, "overlays_bench: failed to load Michroma font\n");
    }
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/IBMPlexMono-Regular.ttf")) == -1) {
        fprintf(stderr, "overlays_bench: failed to load IBM Plex Mono font\n");
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
        fprintf(stderr, "overlays_bench: failed to save screenshot to %s\n", qPrintable(path));
        return false;
    }
    fprintf(stdout, "overlays_bench: screenshot saved to %s\n", qPrintable(path));
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    loadAppFonts();

    FakeOverlaysController fc;

    QWidget host;
    host.setAttribute(Qt::WA_StyledBackground, true);
    host.setStyleSheet(QStringLiteral("background-color: %1;").arg(Hexa::Colors::Background));
    QVBoxLayout* hostLayout = new QVBoxLayout(&host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    QStackedWidget* stack = new QStackedWidget(&host);
    stack->setAttribute(Qt::WA_TranslucentBackground);
    stack->setStyleSheet("background: transparent;");
    SettingsPanel* settings = new SettingsPanel(stack);
    stack->addWidget(settings);
    // The diagnostics panel is an anchored card in the app (MainWindow positions it under the top
    // status indicator); the bench centres it on a plain page instead.
    QWidget* diagPage = new QWidget(stack);
    diagPage->setAttribute(Qt::WA_TranslucentBackground);
    QVBoxLayout* diagLayout = new QVBoxLayout(diagPage);
    DiagnosticsPanel* diagnostics = new DiagnosticsPanel(diagPage);
    // Per-ITEM alignment: the card keeps its content size and floats centred (layout-level
    // alignment would let the single item fill the page and stretch the message strip).
    diagLayout->addWidget(diagnostics, 0, Qt::AlignCenter);
    stack->addWidget(diagPage);
    hostLayout->addWidget(stack);
    wire(*settings, fc);
    wireDiagnostics(*diagnostics, fc);

    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest"))) {
        // --- Settings: apply round-trip through the REAL widgets ---
        QLineEdit* ip = settings->findChild<QLineEdit*>(QStringLiteral("editNetworkIp"));
        QPushButton* apply = settings->findChild<QPushButton*>(QStringLiteral("btnApply"));
        if (ip == nullptr || apply == nullptr) {
            fprintf(stderr, "overlays_bench --selftest FAILED: network editor widgets not found\n");
            return 1;
        }
        ip->setText(QStringLiteral("10.20.30.40"));
        apply->click();
        if (!fc.hasApplied() ||
            fc.lastApplied().network.controllerIp != QStringLiteral("10.20.30.40")) {
            fprintf(stderr, "overlays_bench --selftest FAILED: applied config did not reach the "
                            "controller seam (IP '%s')\n",
                    qPrintable(fc.lastApplied().network.controllerIp));
            return 1;
        }
        QListWidget* categories = settings->findChild<QListWidget*>(QStringLiteral("listCategories"));
        if (categories == nullptr) {
            fprintf(stderr, "overlays_bench --selftest FAILED: category list not found\n");
            return 1;
        }
        for (int row = 0; row < categories->count(); ++row) categories->setCurrentRow(row);

        // --- Diagnostics: classification, event log, reset gating - through the REAL widgets ---
        // The fixed shipping bug must stay fixed: a dead link must never classify as healthy.
        if (hexa::DiagnosticsLog::healthForStatus(QStringLiteral("NotConnected")) ==
            hexa::DiagHealth::Healthy) {
            fprintf(stderr, "overlays_bench --selftest FAILED: NotConnected classified healthy\n");
            return 1;
        }
        QPushButton* resetError =
            diagnostics->findChild<QPushButton*>(QStringLiteral("btnDiagClearError"));
        QListWidget* eventList =
            diagnostics->findChild<QListWidget*>(QStringLiteral("listDiagEvents"));
        if (resetError == nullptr || eventList == nullptr) {
            fprintf(stderr, "overlays_bench --selftest FAILED: diagnostics widgets not found\n");
            return 1;
        }
        if (resetError->isEnabled()) {
            fprintf(stderr, "overlays_bench --selftest FAILED: RESET ERROR armed with no fault\n");
            return 1;
        }
        const int eventsBefore = diagnostics->log().events().size();
        fc.injectSafetyFault();
        if (diagnostics->log().events().size() <= eventsBefore ||
            eventList->count() != diagnostics->log().events().size()) {
            fprintf(stderr, "overlays_bench --selftest FAILED: safety fault produced no events "
                            "(log %d -> %d, list %d)\n",
                    eventsBefore, int(diagnostics->log().events().size()), eventList->count());
            return 1;
        }
        if (!resetError->isEnabled()) {
            fprintf(stderr, "overlays_bench --selftest FAILED: RESET ERROR not armed on fault\n");
            return 1;
        }
        resetError->click();
        if (fc.robotStatus().top.hasError) {
            fprintf(stderr, "overlays_bench --selftest FAILED: RESET ERROR did not reach the "
                            "controller seam\n");
            return 1;
        }
        // E-Stop toggle path through the REAL button.
        QPushButton* eStop = diagnostics->findChild<QPushButton*>(QStringLiteral("btnDiagEStop"));
        if (eStop == nullptr) {
            fprintf(stderr, "overlays_bench --selftest FAILED: E-STOP button not found\n");
            return 1;
        }
        eStop->click();
        if (!fc.robotStatus().top.isEStop || eStop->text() != QStringLiteral("RESET E-STOP")) {
            fprintf(stderr, "overlays_bench --selftest FAILED: E-STOP toggle did not engage\n");
            return 1;
        }
        eStop->click();   // release again
        // CLEAR LOG is view-local: the list empties, controller latches untouched.
        QPushButton* clearLog =
            diagnostics->findChild<QPushButton*>(QStringLiteral("btnDiagClearLog"));
        if (clearLog == nullptr) {
            fprintf(stderr, "overlays_bench --selftest FAILED: CLEAR LOG button not found\n");
            return 1;
        }
        clearLog->click();
        if (eventList->count() != 0 || !diagnostics->log().events().isEmpty()) {
            fprintf(stderr, "overlays_bench --selftest FAILED: CLEAR LOG did not clear the view "
                            "log\n");
            return 1;
        }
        fprintf(stdout, "overlays_bench --selftest exit code: 0\n");
        return 0;
    }

    const QStringList args = QCoreApplication::arguments();
    const int shotIdx = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIdx >= 0) {
        if (shotIdx + 1 >= args.size()) {
            fprintf(stderr, "overlays_bench: --screenshot requires an output file path\n");
            return 1;
        }
        const QString basePath = args.at(shotIdx + 1);
        host.resize(kBenchWidthPx, kBenchHeightPx);
        host.show();

        QListWidget* categories = settings->findChild<QListWidget*>(QStringLiteral("listCategories"));
        if (categories == nullptr) {
            fprintf(stderr, "overlays_bench: category list not found\n");
            return 1;
        }

        // Frames 1-3: settings pages.
        if (!saveGrab(host, basePath)) return 1;
        categories->setCurrentRow(1);
        if (!saveGrab(host, withSuffix(basePath, QStringLiteral("_limits")))) return 1;
        categories->setCurrentRow(2);
        QApplication::processEvents();
        QComboBox* slot = settings->findChild<QComboBox*>(QStringLiteral("comboFrameSlot"));
        if (slot == nullptr) {
            fprintf(stderr, "overlays_bench: frame slot combo not found\n");
            return 1;
        }
        slot->setCurrentIndex(1);
        if (!saveGrab(host, withSuffix(basePath, QStringLiteral("_tools")))) return 1;

        // Frame 4: diagnostics with a staged fault history (warning -> fault -> recovery), so the
        // annunciator badges and the event log render with real content.
        stack->setCurrentWidget(diagPage);
        fc.injectHalWarning();
        fc.injectSafetyFault();
        fc.recoverAll();
        fc.injectSafetyFault();
        if (!saveGrab(host, withSuffix(basePath, QStringLiteral("_diag")))) return 1;
        return 0;
    }

    // Interactive: --diagnostics shows the diagnostics panel with a deterministic scenario cycling
    // so the event log fills live; default shows the settings overlay.
    QTimer scenarioTimer;
    if (QCoreApplication::arguments().contains(QStringLiteral("--diagnostics"))) {
        stack->setCurrentWidget(diagPage);
        int step = 0;
        scenarioTimer.setInterval(3000);
        QObject::connect(&scenarioTimer, &QTimer::timeout, &fc, [&fc, step]() mutable {
            switch (step % 4) {
            case 0: fc.injectHalWarning(); break;
            case 1: fc.injectSafetyFault(); break;
            case 2: fc.clearError(); break;
            case 3: fc.recoverAll(); break;
            default: break;
            }
            ++step;
        });
        scenarioTimer.start();
    }

    host.setWindowTitle(QStringLiteral("Overlays Bench (standalone, FakeOverlaysController)"));
    host.resize(kBenchWidthPx, kBenchHeightPx);
    host.show();
    return app.exec();
}
// --- END OF FILE: HexaStudio/overlays/bench/overlays_bench_main.cpp ---
