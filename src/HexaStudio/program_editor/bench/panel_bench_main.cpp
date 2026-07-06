// --- START OF FILE: HexaStudio/program_editor/bench/panel_bench_main.cpp ---
/**
 * @file panel_bench_main.cpp
 * @brief Standalone bench hosting the real ProgramEditorPanel + FakeController, NO RobotService.
 *
 * Wires the panel exactly the way MainWindow will at integration, but against the offline
 * FakeController instead of RobotService. Run with no args for the interactive panel; --selftest
 * constructs and wires everything headless and exits 0 (smoke check).
 */
#include <QApplication>
#include <QObject>
#include <QVector>
#include <QStringList>

#include "ProgramEditorPanel.h"
#include "FakeController.h"
#include "ProgramData.h"
#include "BackendTypes.h"

using hexa::ProgramEditorPanel;
using hexa::FakeController;

namespace {

QVector<double> demoTcp() { return QVector<double>{100.0, 200.0, 300.0, 0.0, 0.0, 0.0}; }

// Wire the panel <-> controller the same shape MainWindow uses with RobotService.
void wire(ProgramEditorPanel& panel, FakeController& fc) {
    // panel intent -> controller
    QObject::connect(&panel, &ProgramEditorPanel::playRequested,
                     [&fc](const QVector<ProgramCommand>& prog) { fc.startProgram(prog); fc.startClock(); });
    QObject::connect(&panel, &ProgramEditorPanel::resumeRequested,
                     [&fc]() { fc.resumeProgram(); fc.startClock(); });
    QObject::connect(&panel, &ProgramEditorPanel::pauseRequested, [&fc]() { fc.pauseProgram(); });
    QObject::connect(&panel, &ProgramEditorPanel::stopRequested,
                     [&fc]() { fc.stopProgram(); fc.stopClock(); });
    QObject::connect(&panel, &ProgramEditorPanel::programChanged, &fc, &FakeController::uploadProgram);
    QObject::connect(&panel, &ProgramEditorPanel::remoteListRequested, &fc, &FakeController::requestRemoteFileList);
    QObject::connect(&panel, &ProgramEditorPanel::remoteLoadRequested, &fc, &FakeController::loadRemoteProgramFile);
    // remoteSave/remoteDelete: the bench has no remote persistence; intentionally unhandled.

    // controller feedback -> panel status (the panel only displays it; it never branches on feedback)
    auto pushStatus = [&panel, &fc]() {
        HmiProgramStatus ps{};
        ps.isRunning = fc.isRunning() && !fc.isPaused();
        ps.isPaused = fc.isPaused();
        ps.currentRowIndex = fc.currentLine();
        ps.loadedProgramName = QStringLiteral("bench");
        HmiMotionStatus ms{};
        ms.isSimulated = false;  // REAL so the bench can teach
        ms.isMoving = false;
        ms.actualJoints = QVector<double>(6, 0.0);
        ms.actualTcp = demoTcp();
        ms.activeToolId = 0;
        ms.activeBaseId = 0;
        panel.updateState(ps, ms);
    };
    QObject::connect(&fc, &FakeController::executionLineChanged, [pushStatus](int) { pushStatus(); });
    QObject::connect(&fc, &FakeController::runningChanged, [pushStatus](bool) { pushStatus(); });
    QObject::connect(&fc, &FakeController::pausedChanged, [pushStatus](bool) { pushStatus(); });
    QObject::connect(&fc, &FakeController::remoteFileListReceived, &panel, &ProgramEditorPanel::setRemoteFileList);
    QObject::connect(&fc, &FakeController::programLoaded, &panel, &ProgramEditorPanel::loadProgram);

    pushStatus(); // initial push enables teach
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    ProgramEditorPanel panel;
    FakeController fc;
    fc.setRemoteFiles(QStringList{QStringLiteral("remote_a.json"), QStringLiteral("remote_b.json")});
    wire(panel, fc);

    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest"))) {
        panel.setProgram(QVector<ProgramCommand>{}); // exercise an incoming-load slot headless
        fprintf(stdout, "program_editor_panel_bench --selftest exit code: 0\n");
        return 0;
    }

    panel.setWindowTitle(QStringLiteral("Program Editor Panel Bench (standalone, FakeController)"));
    panel.resize(420, 640);
    panel.show();
    return app.exec();
}
// --- END OF FILE: HexaStudio/program_editor/bench/panel_bench_main.cpp ---
