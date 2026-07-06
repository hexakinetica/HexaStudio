// --- START OF FILE: HexaStudio/program_editor/test/ProgramEditorModelTests.cpp ---
/**
 * @file ProgramEditorModelTests.cpp
 * @brief Unit tests for ProgramEditorModel — the Qt list adapter over ProgramBuilder (no widgets).
 */
#include <gtest/gtest.h>

#include <QVector>
#include "ProgramBuilder.h"
#include "ProgramEditorModel.h"
#include "ProgramData.h"   // CommandType

using hexa::ProgramBuilder;
using hexa::ProgramEditorModel;
using hexa::MotionKind;

namespace {
QVector<double> sixZeros() { return QVector<double>(6, 0.0); }
} // namespace

TEST(ProgramEditorModel, RowCountTracksBuilder) {
    ProgramBuilder b;
    ProgramEditorModel m(&b);
    EXPECT_EQ(m.rowCount(), 0);
    b.addMotionPoint(MotionKind::PTP, sixZeros(), sixZeros(), 0, 0, false);
    EXPECT_EQ(m.rowCount(), 1);
    b.addWait(1.0);
    EXPECT_EQ(m.rowCount(), 2);
}

TEST(ProgramEditorModel, ExposesStepDataViaRoles) {
    ProgramBuilder b;
    ProgramEditorModel m(&b);
    b.addMotionPoint(MotionKind::LIN, sixZeros(), sixZeros(), 0, 0, false);
    const QModelIndex idx = m.index(0);
    EXPECT_EQ(m.data(idx, ProgramEditorModel::CodeRole).toString(), QStringLiteral("LIN"));
    EXPECT_EQ(m.data(idx, ProgramEditorModel::TypeRole).value<CommandType>(), CommandType::Motion);
}

TEST(ProgramEditorModel, ActiveRowReflectsExecutionPointer) {
    ProgramBuilder b;
    ProgramEditorModel m(&b);
    b.addComment("a");
    b.addComment("b");
    EXPECT_FALSE(m.data(m.index(1), ProgramEditorModel::IsActiveRole).toBool());
    m.setActiveRow(1);
    EXPECT_TRUE(m.data(m.index(1), ProgramEditorModel::IsActiveRole).toBool());
    EXPECT_FALSE(m.data(m.index(0), ProgramEditorModel::IsActiveRole).toBool());
}

TEST(ProgramEditorModel, IssueRoleReflectsValidation) {
    // A pose-less motion is rejected by addMotionPoint, but an external load can deliver one; the row's
    // IssueRole must then report Error severity.
    QVector<ProgramCommand> prog;
    prog.append(ProgramCommand(CommandType::Motion, QStringLiteral("PTP"), QStringLiteral("P1"), QVariantMap{}));
    ProgramBuilder b;
    ProgramEditorModel m(&b);
    b.load(prog);
    EXPECT_EQ(m.data(m.index(0), ProgramEditorModel::IssueRole).toInt(), ProgramEditorModel::ErrorIssue);

    // A valid motion point reports no issue.
    ProgramBuilder b2;
    ProgramEditorModel m2(&b2);
    b2.addMotionPoint(MotionKind::PTP, sixZeros(), sixZeros(), 0, 0, false);
    EXPECT_EQ(m2.data(m2.index(0), ProgramEditorModel::IssueRole).toInt(), ProgramEditorModel::NoIssue);
}

TEST(ProgramEditorModel, ReactsToRemoveAndReset) {
    ProgramBuilder b;
    ProgramEditorModel m(&b);
    b.addComment("a");
    b.addComment("b");
    ASSERT_EQ(m.rowCount(), 2);
    b.remove(0);
    EXPECT_EQ(m.rowCount(), 1);
    EXPECT_EQ(m.data(m.index(0), ProgramEditorModel::NameRole).toString(), QStringLiteral("b"));
    b.clear();
    EXPECT_EQ(m.rowCount(), 0);
}
// --- END OF FILE: HexaStudio/program_editor/test/ProgramEditorModelTests.cpp ---
