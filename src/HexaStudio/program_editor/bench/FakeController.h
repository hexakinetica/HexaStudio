// --- START OF FILE: HexaStudio/program_editor/bench/FakeController.h ---
/**
 * @file FakeController.h
 * @brief Offline emulation of HexaMotion's observable responses, for the standalone bench and tests.
 *
 * FakeController plays the controller's role at the RobotService boundary WITHOUT any network/RDT: it
 * receives a program (upload/start), "executes" it on a tick clock advancing the running line, and
 * reports running/paused state, the execution line, a remote file list and external loads — exactly
 * the feedback PanelLeft displays. It is Qt Core only (QObject + QTimer), so it is deterministically
 * unit-testable by calling tick() directly (no real-time waits).
 *
 * Per the authoring/execution ADR, control flow executes HERE (the controller side), and the pendant
 * only displays the reported line. The current execution model covers the step kinds the builder can
 * author today (motion / WAIT / comment) as a linear run; flat control-flow/IO/register execution is
 * added here when those steps are introduced.
 */
#ifndef HEXA_FAKE_CONTROLLER_H
#define HEXA_FAKE_CONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QStringList>

#include "ProgramData.h"   // ProgramCommand

namespace hexa {

class FakeController : public QObject {
    Q_OBJECT
public:
    explicit FakeController(QObject* parent = nullptr);

    // Tick clock: how many program steps are processed per second of wall time, and how much faster
    // than real time WAIT durations elapse on the bench.
    static constexpr int kTicksPerSecond = 10;
    static constexpr double kTimeScale = 8.0;

    bool isRunning() const { return m_exec == Exec::Running || m_exec == Exec::WaitingTime; }
    bool isPaused()  const { return m_exec == Exec::Paused; }
    int  currentLine() const { return m_line; }
    const QVector<ProgramCommand>& uploadedProgram() const { return m_program; }

    void setRemoteFiles(const QStringList& files) { m_remoteFiles = files; }

    /// @brief Start/stop the internal wall-clock timer (the bench uses it; tests drive tick() instead).
    void startClock();
    void stopClock();

public slots:
    // Surface mirrored from RobotService (subset needed by the editor); no network underneath.
    void uploadProgram(const QVector<ProgramCommand>& program);
    void startProgram(const QVector<ProgramCommand>& program); // upload + run from step 0
    void resumeProgram();
    void pauseProgram();
    void stopProgram();
    void requestRemoteFileList();
    void loadRemoteProgramFile(const QString& filename);

    /// @brief Advance execution by one step (called by the internal timer or directly by tests).
    void tick();

signals:
    void executionLineChanged(int row);   // -1 when idle/stopped
    void runningChanged(bool running);
    void pausedChanged(bool paused);
    void remoteFileListReceived(const QStringList& files);
    void programLoaded(const QVector<ProgramCommand>& program);
    void message(const QString& text);

private:
    enum class Exec { Idle, Running, Paused, WaitingTime };

    void setExec(Exec exec);
    void finish();
    static bool isMotion(const QString& code);

    QVector<ProgramCommand> m_program;
    QStringList m_remoteFiles;
    QTimer m_timer;
    Exec m_exec = Exec::Idle;
    int m_index = 0;        // next step to execute
    int m_line = -1;        // currently highlighted execution line
    int m_waitTicks = 0;    // remaining ticks for a WAIT step
};

} // namespace hexa

#endif // HEXA_FAKE_CONTROLLER_H
// --- END OF FILE: HexaStudio/program_editor/bench/FakeController.h ---
