// --- START OF FILE: HexaStudio/program_editor/test/FakeControllerTests.cpp ---
/**
 * @file FakeControllerTests.cpp
 * @brief Unit tests for FakeController — offline HexaMotion emulation, driven deterministically.
 */
#include <gtest/gtest.h>

#include <QVector>
#include <QVariantMap>
#include <QStringList>

#include "FakeController.h"
#include "ProgramData.h"

using hexa::FakeController;

namespace {

QVector<ProgramCommand> comments(int n) {
    QVector<ProgramCommand> prog;
    for (int i = 0; i < n; ++i) {
        prog.append(ProgramCommand(CommandType::Comment, QStringLiteral("#"),
                                   QStringLiteral("c%1").arg(i)));
    }
    return prog;
}

ProgramCommand waitStep(double seconds) {
    QVariantMap params;
    params[QStringLiteral("Time")] = seconds;
    return ProgramCommand(CommandType::Logic, QStringLiteral("WAIT"), QStringLiteral("WAIT"), params);
}

} // namespace

TEST(FakeController, LinearRunAdvancesAndFinishes) {
    FakeController fc;
    fc.startProgram(comments(3));
    EXPECT_TRUE(fc.isRunning());
    EXPECT_EQ(fc.currentLine(), -1); // not advanced until first tick

    fc.tick();
    EXPECT_EQ(fc.currentLine(), 0);
    fc.tick();
    EXPECT_EQ(fc.currentLine(), 1);
    fc.tick();
    EXPECT_EQ(fc.currentLine(), 2);
    fc.tick(); // past the end -> finish
    EXPECT_FALSE(fc.isRunning());
    EXPECT_EQ(fc.currentLine(), -1);
}

TEST(FakeController, WaitHoldsLineThenContinues) {
    QVector<ProgramCommand> prog;
    prog.append(waitStep(8.0)); // ~10 ticks at scale 8
    prog.append(ProgramCommand(CommandType::Comment, QStringLiteral("#"), QStringLiteral("done")));

    FakeController fc;
    fc.startProgram(prog);
    fc.tick();
    EXPECT_EQ(fc.currentLine(), 0);   // entered WAIT on the wait step
    EXPECT_TRUE(fc.isRunning());
    fc.tick();
    EXPECT_EQ(fc.currentLine(), 0);   // still waiting: line does not advance
    EXPECT_TRUE(fc.isRunning());

    int guard = 0;
    while (fc.isRunning() && guard++ < 1000) {
        fc.tick();
    }
    EXPECT_FALSE(fc.isRunning());      // wait elapsed, comment ran, program finished
}

TEST(FakeController, PauseFreezesAndResumeContinues) {
    FakeController fc;
    fc.startProgram(comments(3));
    fc.tick();
    EXPECT_EQ(fc.currentLine(), 0);

    fc.pauseProgram();
    EXPECT_TRUE(fc.isPaused());
    EXPECT_FALSE(fc.isRunning());
    fc.tick();
    EXPECT_EQ(fc.currentLine(), 0);   // paused: no progress

    fc.resumeProgram();
    EXPECT_TRUE(fc.isRunning());
    fc.tick();
    EXPECT_EQ(fc.currentLine(), 1);   // continues from where it paused
}

TEST(FakeController, StopResetsExecution) {
    FakeController fc;
    fc.startProgram(comments(3));
    fc.tick();
    EXPECT_EQ(fc.currentLine(), 0);
    fc.stopProgram();
    EXPECT_FALSE(fc.isRunning());
    EXPECT_EQ(fc.currentLine(), -1);
}

TEST(FakeController, RemoteFileListEmitted) {
    FakeController fc;
    fc.setRemoteFiles(QStringList{QStringLiteral("a.json"), QStringLiteral("b.json")});
    QStringList received;
    QObject::connect(&fc, &FakeController::remoteFileListReceived,
                     [&received](const QStringList& files) { received = files; });
    fc.requestRemoteFileList();
    ASSERT_EQ(received.size(), 2);
    EXPECT_EQ(received.at(0), QStringLiteral("a.json"));
}
// --- END OF FILE: HexaStudio/program_editor/test/FakeControllerTests.cpp ---
