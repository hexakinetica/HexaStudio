// --- START OF FILE: HexaStudio/program_editor/bench/FakeController.cpp ---
#include "FakeController.h"

#include <QtGlobal>

namespace hexa {

FakeController::FakeController(QObject* parent) : QObject(parent) {
    m_timer.setInterval(1000 / kTicksPerSecond);
    connect(&m_timer, &QTimer::timeout, this, &FakeController::tick);
}

bool FakeController::isMotion(const QString& code) {
    return code == "PTP" || code == "LIN" || code == "MoveJ" || code == "MoveL";
}

void FakeController::startClock() { m_timer.start(); }
void FakeController::stopClock()  { m_timer.stop(); }

void FakeController::setExec(Exec exec) {
    if (m_exec == exec) {
        return;
    }
    const bool wasRunning = isRunning();
    const bool wasPaused  = isPaused();
    m_exec = exec;
    if (isRunning() != wasRunning) {
        emit runningChanged(isRunning());
    }
    if (isPaused() != wasPaused) {
        emit pausedChanged(isPaused());
    }
}

void FakeController::finish() {
    m_index = 0;
    setExec(Exec::Idle);
    if (m_line != -1) {
        m_line = -1;
        emit executionLineChanged(m_line);
    }
    emit message(QStringLiteral("Program finished."));
}

void FakeController::uploadProgram(const QVector<ProgramCommand>& program) {
    // Self-upload from the editor: store it but do NOT echo programLoaded (mirrors the real
    // self-echo suppression). programLoaded is reserved for genuine external loads.
    m_program = program;
}

void FakeController::startProgram(const QVector<ProgramCommand>& program) {
    uploadProgram(program);
    m_index = 0;
    if (m_line != -1) {
        m_line = -1;
        emit executionLineChanged(m_line);
    }
    setExec(Exec::Running);
}

void FakeController::resumeProgram() {
    if (m_exec == Exec::Paused) {
        setExec(Exec::Running);
    }
}

void FakeController::pauseProgram() {
    if (m_exec == Exec::Running || m_exec == Exec::WaitingTime) {
        setExec(Exec::Paused);
    }
}

void FakeController::stopProgram() {
    m_index = 0;
    setExec(Exec::Idle);
    if (m_line != -1) {
        m_line = -1;
        emit executionLineChanged(m_line);
    }
}

void FakeController::requestRemoteFileList() {
    emit remoteFileListReceived(m_remoteFiles);
}

void FakeController::loadRemoteProgramFile(const QString& filename) {
    // A genuine external load: emit programLoaded so the editor replaces its program (and resets undo).
    emit message(QStringLiteral("Loaded remote file: %1").arg(filename));
    emit programLoaded(m_program);
}

void FakeController::tick() {
    if (m_exec == Exec::WaitingTime) {
        if (--m_waitTicks <= 0) {
            setExec(Exec::Running);
        }
        return; // line stays on the WAIT step while waiting
    }
    if (m_exec != Exec::Running) {
        return;
    }
    if (m_index >= m_program.size()) {
        finish();
        return;
    }

    // Highlight the step about to execute, then advance.
    m_line = m_index;
    emit executionLineChanged(m_line);

    const ProgramCommand& step = m_program.at(m_index);
    if (step.code == QStringLiteral("WAIT")) {
        const double seconds = step.params.value(QStringLiteral("Time"), 0.0).toDouble();
        const int ticks = qRound(seconds * kTicksPerSecond / kTimeScale);
        ++m_index;
        if (ticks > 0) {
            m_waitTicks = ticks;
            setExec(Exec::WaitingTime);
        }
    } else {
        // Motion (dwell of one tick = "reached"), comment, and other linear steps simply advance.
        ++m_index;
    }
}

} // namespace hexa
// --- END OF FILE: HexaStudio/program_editor/bench/FakeController.cpp ---
