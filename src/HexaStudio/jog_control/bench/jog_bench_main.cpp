// --- START OF FILE: HexaStudio/jog_control/bench/jog_bench_main.cpp ---
/**
 * @file jog_bench_main.cpp
 * @brief Standalone bench hosting the module-owned JogPanel + FakeJogController, NO RobotService,
 *        NO network, NO HexaStudio.
 *
 * Wires the panel exactly the way MainWindow does, but against the offline FakeJogController instead of
 * RobotService. The bench host window paints the application background colour and loads the
 * application fonts, so the (translucent-by-design) panel is reviewed exactly as it will look in the
 * app. Run with no args for the interactive jog panel; --selftest constructs and wires everything
 * headless, exercises a jog intent, and exits 0 (smoke check); --screenshot <file.png> renders the
 * armed panel (A1 staged at its limit) plus a second "<file>_collapsed.png" frame with the monitor
 * collapsed, so the design AND the no-reflow-on-collapse requirement are reviewable without a manual
 * run.
 */
#include <QApplication>
#include <QComboBox>
#include <QEventLoop>
#include <QFontDatabase>
#include <QObject>
#include <QPixmap>
#include <QTimer>
#include <QVBoxLayout>

#include "JogPanel.h"
#include "FramePickerDialog.h"
#include "FakeJogController.h"
#include "BackendTypes.h"
#include "HexaTheme.h"

using hexa::FakeJogController;
using hexa::JogPanel;

namespace {

// Bench window matches the right-panel column MainWindow allocates (see m_mainSplitter->setSizes).
constexpr int kBenchWidthPx = 300;
constexpr int kBenchHeightPx = 720;

// Wire the panel <-> controller the same shape MainWindow uses with RobotService.
void wire(JogPanel& panel, FakeJogController& fc) {
    // controller feedback -> panel (display only; the panel never branches on the controller)
    QObject::connect(&fc, &FakeJogController::configReceived, &panel, &JogPanel::onConfigReceived);
    QObject::connect(&fc, &FakeJogController::statusChanged, &panel, &JogPanel::updateState);

    // panel intent -> controller. EVERY panel signal lands in a controller slot: an intent without a
    // consumer is a dead control on the bench (that is how the monitor tool selector was broken).
    QObject::connect(&panel, &JogPanel::jogRequested, &fc, &FakeJogController::requestJog);
    QObject::connect(&panel, &JogPanel::jogEnableRequested, &fc, &FakeJogController::setJogEnabled);
    QObject::connect(&panel, &JogPanel::coordSystemChanged, &fc, &FakeJogController::setJogMode);
    QObject::connect(&panel, &JogPanel::goHomeRequested, &fc, &FakeJogController::goHome);
    // Context signals carry names; the controller seam takes ids. The mapping below mirrors the
    // MainWindow lambdas (lookup against the controller config; "JOINT" is not a base -> id 0).
    QObject::connect(&panel, &JogPanel::jogContextChanged, &fc,
                     [&fc](const QString& toolName, const QString& baseName) {
                         fc.setJogContext(fc.toolIdByName(toolName), fc.baseIdByName(baseName));
                     });
    QObject::connect(&panel, &JogPanel::monitorContextChanged, &fc,
                     [&fc](const QString& toolName, const QString& baseName) {
                         const int baseId = (baseName == QStringLiteral("JOINT"))
                                                ? 0 : fc.baseIdByName(baseName);
                         fc.setMonitorContext(fc.toolIdByName(toolName), baseId);
                     });

    // Initial sync: config first (populates the tool/base selectors), then status (enables the jog UI).
    panel.onConfigReceived(fc.demoConfig());
    panel.updateState(fc.status());
}

// The application fonts (Michroma / IBM Plex Mono) live in the shared Qt resource; without them the
// theme falls back to system fonts and the panel is reviewed with the wrong typography.
void loadAppFonts() {
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/Michroma-Regular.ttf")) == -1) {
        fprintf(stderr, "jog_control_bench: failed to load Michroma font\n");
    }
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/resources/IBMPlexMono-Regular.ttf")) == -1) {
        fprintf(stderr, "jog_control_bench: failed to load IBM Plex Mono font\n");
    }
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    loadAppFonts();

    FakeJogController fc;

    // Host window: paints the application background (the panel itself is translucent by design and
    // relies on the host colour, exactly like MainWindow). The host owns the panel (Qt parenting).
    QWidget host;
    host.setAttribute(Qt::WA_StyledBackground, true); // plain QWidget ignores stylesheet bg otherwise
    host.setStyleSheet(QStringLiteral("background-color: %1;").arg(Hexa::Colors::Background));
    QVBoxLayout* hostLayout = new QVBoxLayout(&host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    JogPanel* panel = new JogPanel(&host);
    hostLayout->addWidget(panel);
    wire(*panel, fc);

    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest"))) {
        fc.setJogEnabled(true);
        fc.requestJog(0, 1.0); // exercise a jog intent headless

        // Monitor context, FULL UI PATH: drive the real combos the way the operator does; the intent
        // must cross panel -> wiring (name->id) -> controller seam and change the monitor pose.
        // GRIPPER (z+120) on TABLE (x+500) must differ from TOOL0 on TABLE.
        QComboBox* monBase = panel->findChild<QComboBox*>(QStringLiteral("comboMonitorBase"));
        QComboBox* monTool = panel->findChild<QComboBox*>(QStringLiteral("comboMonitorTool"));
        if (monBase == nullptr || monTool == nullptr) {
            fprintf(stderr, "jog_control_bench --selftest FAILED: monitor combos not found\n");
            return 1;
        }
        monBase->setCurrentText(QStringLiteral("TABLE"));
        const QVector<double> poseTool0 = fc.status().monitorPose;
        monTool->setCurrentText(QStringLiteral("GRIPPER"));
        const QVector<double> poseGripper = fc.status().monitorPose;
        if (poseTool0 == poseGripper) {
            fprintf(stderr, "jog_control_bench --selftest FAILED: monitor tool selection does not "
                            "reach the controller seam (pose unchanged)\n");
            return 1;
        }
        fc.setJogContext(1, 1); // exercise the jog-context seam headless
        fprintf(stdout, "jog_control_bench --selftest exit code: 0\n");
        return 0;
    }

    // Design-review mode: render the armed panel once into a PNG and exit (no interaction needed).
    const QStringList args = QCoreApplication::arguments();
    const int shotIdx = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIdx >= 0) {
        if (shotIdx + 1 >= args.size()) {
            fprintf(stderr, "jog_control_bench: --screenshot requires an output file path\n");
            return 1;
        }
        // Staged review state: armed (READY) with A1 driven into its +limit, so one render shows the
        // live desired value, the per-axis stripe, the at-limit highlight and the limit notice.
        fc.setJogEnabled(true);
        fc.requestJog(0, 400.0); // far past +170 -> clamps at the limit, raises the notice
        QEventLoop settle;       // let the fake motion settle so the JOG button renders READY
        QTimer::singleShot(FakeJogController::kMotionSettleMs + 100, &settle, &QEventLoop::quit);
        settle.exec();
        host.resize(kBenchWidthPx, kBenchHeightPx);
        host.show();
        QApplication::processEvents();
        const QPixmap shot = host.grab();
        if (!shot.save(args.at(shotIdx + 1))) {
            fprintf(stderr, "jog_control_bench: failed to save screenshot to %s\n",
                    qPrintable(args.at(shotIdx + 1)));
            return 1;
        }
        fprintf(stdout, "jog_control_bench: screenshot saved to %s\n",
                qPrintable(args.at(shotIdx + 1)));

        // Second frame with the monitor collapsed (via its real title button): the jog pad must not
        // reflow, which is a stated layout requirement - compare the two PNGs to verify.
        QPushButton* monitorTitle = nullptr;
        const QList<QPushButton*> buttons = host.findChildren<QPushButton*>();
        for (QPushButton* button : buttons) {
            if (button->text() == QStringLiteral("MONITOR SYSTEM")) { monitorTitle = button; break; }
        }
        if (monitorTitle == nullptr) {
            fprintf(stderr, "jog_control_bench: MONITOR SYSTEM title button not found\n");
            return 1;
        }
        monitorTitle->click();
        QApplication::processEvents();
        QString collapsedPath = args.at(shotIdx + 1);
        const int dotIdx = collapsedPath.lastIndexOf(QLatin1Char('.'));
        if (dotIdx >= 0) {
            collapsedPath.insert(dotIdx, QStringLiteral("_collapsed"));
        } else {
            collapsedPath.append(QStringLiteral("_collapsed.png"));
        }
        if (!host.grab().save(collapsedPath)) {
            fprintf(stderr, "jog_control_bench: failed to save screenshot to %s\n",
                    qPrintable(collapsedPath));
            return 1;
        }
        fprintf(stdout, "jog_control_bench: screenshot saved to %s\n", qPrintable(collapsedPath));

        // Third frame: the module-owned tool picker with a non-identity frame selected (GRIPPER),
        // so the offset preview grid is exercised in the render.
        hexa::FramePickerDialog picker(&host);
        picker.setToolData(fc.demoConfig().tools, 1);
        picker.show();
        QApplication::processEvents();
        QString pickerPath = args.at(shotIdx + 1);
        const int pickerDotIdx = pickerPath.lastIndexOf(QLatin1Char('.'));
        if (pickerDotIdx >= 0) {
            pickerPath.insert(pickerDotIdx, QStringLiteral("_tool_picker"));
        } else {
            pickerPath.append(QStringLiteral("_tool_picker.png"));
        }
        if (!picker.grab().save(pickerPath)) {
            fprintf(stderr, "jog_control_bench: failed to save screenshot to %s\n",
                    qPrintable(pickerPath));
            return 1;
        }
        fprintf(stdout, "jog_control_bench: screenshot saved to %s\n", qPrintable(pickerPath));
        return 0;
    }

    host.setWindowTitle(QStringLiteral("Jog Control Bench (standalone, FakeJogController)"));
    host.resize(kBenchWidthPx, kBenchHeightPx);
    host.show();
    return app.exec();
}
// --- END OF FILE: HexaStudio/jog_control/bench/jog_bench_main.cpp ---
