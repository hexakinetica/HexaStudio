// --- START OF FILE: HexaStudio/program_editor/test/ProgramBuilderTests.cpp ---
/**
 * @file ProgramBuilderTests.cpp
 * @brief Unit tests for ProgramBuilder — pure authoring logic, no widgets, no RobotService.
 */
#include <gtest/gtest.h>

#include <QVector>
#include "ProgramBuilder.h"

using hexa::ProgramBuilder;
using hexa::ProgramError;
using hexa::MotionKind;
using hexa::IssueSeverity;

namespace {

QVector<double> sixZeros() { return QVector<double>(6, 0.0); }

QVector<double> pose(double a, double b, double c, double d, double e, double f) {
    return QVector<double>{a, b, c, d, e, f};
}

} // namespace

// --- Construction -----------------------------------------------------------

TEST(ProgramBuilder, AddMotionPointAppendsWithDefaults) {
    ProgramBuilder b;
    auto r = b.addMotionPoint(MotionKind::PTP, sixZeros(), sixZeros(), 0, 0, /*blend=*/false);
    ASSERT_TRUE(r.isSuccess());
    EXPECT_EQ(r.value(), 0);
    ASSERT_EQ(b.stepCount(), 1);
    EXPECT_EQ(b.at(0).code, QStringLiteral("PTP"));
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kSpeed).toInt(), 100);
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kZone).toString(), QStringLiteral("FINE"));
}

TEST(ProgramBuilder, BlendStampsApproxZone) {
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::LIN, sixZeros(), sixZeros(), 0, 0, /*blend=*/true);
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kZone).toString(), QStringLiteral("APPROX 50mm"));
}

// --- CIRC authoring (docs/REQ_motion_circ.md, batch 1c) ----------------------

TEST(ProgramBuilder, CircTeachViaStoresPoseAndMakesRunnable) {
    ProgramBuilder b;
    auto add = b.addMotionPoint(MotionKind::CIRC, sixZeros(), pose(600, 100, 400, 0, 90, 0), 0, 0, false);
    ASSERT_TRUE(add.isSuccess());
    EXPECT_EQ(b.at(0).code, QStringLiteral("CIRC"));

    // Without a taught via the program must NOT be runnable (fail-closed in the editor).
    EXPECT_FALSE(b.isRunnable());

    auto via = b.teachVia(0, pose(550, 50, 420, 0, 0, 0));
    ASSERT_TRUE(via.isSuccess());
    const QVector<double> stored = b.at(0).params.value(ProgramBuilder::kTcpVia).value<QVector<double>>();
    ASSERT_EQ(stored.size(), 6);
    EXPECT_DOUBLE_EQ(stored[0], 550.0);
    EXPECT_TRUE(b.isRunnable());
}

TEST(ProgramBuilder, CircViaCoincidingWithTargetBlocksRun) {
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::CIRC, sixZeros(), pose(600, 100, 400, 0, 0, 0), 0, 0, false);
    // Same position as the target (orientation differs — irrelevant, position defines the arc).
    ASSERT_TRUE(b.teachVia(0, pose(600, 100, 400, 45, 0, 0)).isSuccess());
    EXPECT_FALSE(b.isRunnable());
    bool found = false;
    for (const auto& issue : b.validate()) {
        if (issue.code == ProgramError::ViaPointCoincidesWithTarget) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ProgramBuilder, CircRequiresCartesianTargetAtTeachTime) {
    ProgramBuilder b;
    // Joints alone are not enough for CIRC: the arc is planned from the Cartesian target.
    auto r = b.addMotionPoint(MotionKind::CIRC, sixZeros(), {}, 0, 0, false);
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error(), ProgramError::MotionPointMissingPose);
    EXPECT_EQ(b.stepCount(), 0);
}

TEST(ProgramBuilder, TeachViaRejectsNonCircSteps) {
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::LIN, sixZeros(), sixZeros(), 0, 0, false);
    auto r = b.teachVia(0, pose(1, 2, 3, 0, 0, 0));
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error(), ProgramError::NotACircStep);

    b.addWait(1.0);
    auto r2 = b.teachVia(1, pose(1, 2, 3, 0, 0, 0));
    ASSERT_TRUE(r2.isError());
    EXPECT_EQ(r2.error(), ProgramError::NotACircStep);
}

TEST(ProgramBuilder, CircTcpPoseIsEditable) {
    // CIRC is cartesian-dominant (controller plans from cartesian_target), so the per-component
    // TCP editor must accept it like LIN.
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::CIRC, sixZeros(), pose(600, 100, 400, 0, 0, 0), 0, 0, false);
    EXPECT_TRUE(b.setTcpComponent(0, 0, 601.5).isSuccess());
    const QVector<double> p = b.at(0).params.value(ProgramBuilder::kTcp).value<QVector<double>>();
    EXPECT_DOUBLE_EQ(p[0], 601.5);
}

// --- SPLINE authoring (docs/REQ_motion_spline.md, batch 2c) ------------------

TEST(ProgramBuilder, SplinePointsTaughtLikeLinAndRunnable) {
    ProgramBuilder b;
    ASSERT_TRUE(b.addMotionPoint(MotionKind::SPLINE, sixZeros(), pose(100, 0, 300, 0, 0, 0), 0, 0, false).isSuccess());
    ASSERT_TRUE(b.addMotionPoint(MotionKind::SPLINE, sixZeros(), pose(200, 50, 300, 0, 0, 0), 0, 0, false).isSuccess());
    EXPECT_EQ(b.at(0).code, QStringLiteral("SPLINE"));
    EXPECT_TRUE(b.isRunnable());
    // SPLINE is cartesian-dominant: the TCP pose is editable like LIN.
    EXPECT_TRUE(b.setTcpComponent(1, 0, 210.0).isSuccess());
}

TEST(ProgramBuilder, SplineRequiresCartesianTargetAtTeachTime) {
    ProgramBuilder b;
    auto r = b.addMotionPoint(MotionKind::SPLINE, sixZeros(), {}, 0, 0, false);
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error(), ProgramError::MotionPointMissingPose);
}

TEST(ProgramBuilder, SplineCoincidentConsecutivePointsBlockRun) {
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::SPLINE, sixZeros(), pose(100, 0, 300, 0, 0, 0), 0, 0, false);
    b.addMotionPoint(MotionKind::SPLINE, sixZeros(), pose(100, 0, 300, 0, 0, 45), 0, 0, false);
    EXPECT_FALSE(b.isRunnable());
    bool found = false;
    for (const auto& issue : b.validate()) {
        if (issue.code == ProgramError::SplinePointsCoincide) {
            found = true;
            EXPECT_EQ(issue.severity, IssueSeverity::Error);
        }
    }
    EXPECT_TRUE(found);
}

TEST(ProgramBuilder, SplineSpeedMismatchAndZoneAreWarningsOnly) {
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::SPLINE, sixZeros(), pose(100, 0, 300, 0, 0, 0), 0, 0, false);
    b.addMotionPoint(MotionKind::SPLINE, sixZeros(), pose(200, 50, 300, 0, 0, 0), 0, 0, /*blend=*/true);
    ASSERT_TRUE(b.setSpeed(1, 40).isSuccess()); // differs from the block's first point (100%)

    bool speed_warning = false;
    bool zone_warning = false;
    for (const auto& issue : b.validate()) {
        if (issue.code == ProgramError::SplineBlockSpeedMismatch) {
            speed_warning = true;
            EXPECT_EQ(issue.severity, IssueSeverity::Warning);
        }
        if (issue.code == ProgramError::SplineZoneIgnored) {
            zone_warning = true;
            EXPECT_EQ(issue.severity, IssueSeverity::Warning);
        }
    }
    EXPECT_TRUE(speed_warning);
    EXPECT_TRUE(zone_warning);
    // Warnings must not block RUN.
    EXPECT_TRUE(b.isRunnable());
}

TEST(ProgramBuilder, SplineBlocksSeparatedByOtherStepsValidateIndependently) {
    // A LIN between two SPLINE points breaks the block: the coincidence rule must NOT fire across
    // the break (each block validates independently, mirroring execution grouping).
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::SPLINE, sixZeros(), pose(100, 0, 300, 0, 0, 0), 0, 0, false);
    b.addMotionPoint(MotionKind::LIN, sixZeros(), pose(150, 0, 300, 0, 0, 0), 0, 0, false);
    b.addMotionPoint(MotionKind::SPLINE, sixZeros(), pose(100, 0, 300, 0, 0, 0), 0, 0, false);
    for (const auto& issue : b.validate()) {
        EXPECT_NE(issue.code, ProgramError::SplinePointsCoincide);
    }
    EXPECT_TRUE(b.isRunnable());
}

TEST(ProgramBuilder, AddWaitRejectsNegative) {
    ProgramBuilder b;
    auto r = b.addWait(-1.0);
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error(), ProgramError::NegativeTime);
    EXPECT_EQ(b.stepCount(), 0); // rejected: nothing inserted
}

TEST(ProgramBuilder, InsertAfterIndexPlacesNext) {
    ProgramBuilder b;
    b.addComment("a");                        // index 0
    b.addComment("b");                        // index 1
    const int mid = b.addComment("mid", 0);   // insert after index 0 (addComment cannot fail)
    EXPECT_EQ(mid, 1);
    EXPECT_EQ(b.at(1).name, QStringLiteral("mid"));
    EXPECT_EQ(b.at(2).name, QStringLiteral("b"));
}

// --- Editing ----------------------------------------------------------------

TEST(ProgramBuilder, SetSpeedEnforcesRange) {
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::PTP, sixZeros(), sixZeros(), 0, 0, false);
    EXPECT_TRUE(b.setSpeed(0, 250).isError());
    EXPECT_TRUE(b.setSpeed(0, 0).isError());
    auto ok = b.setSpeed(0, 50);
    EXPECT_TRUE(ok.isSuccess());
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kSpeed).toInt(), 50);
}

TEST(ProgramBuilder, SetSpeedRejectsNonMotion) {
    ProgramBuilder b;
    b.addWait(1.0);
    auto r = b.setSpeed(0, 50);
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error(), ProgramError::NotAMotionStep);
}

TEST(ProgramBuilder, TcpEditableOnlyForLin) {
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::PTP, sixZeros(), sixZeros(), 0, 0, false);
    auto ptp = b.setTcpComponent(0, 0, 123.0);
    ASSERT_TRUE(ptp.isError());
    EXPECT_EQ(ptp.error(), ProgramError::PoseNotEditableForJointMove);

    b.addMotionPoint(MotionKind::LIN, sixZeros(), pose(1, 2, 3, 4, 5, 6), 0, 0, false); // index 1
    auto lin = b.setTcpComponent(1, 0, 123.0);
    ASSERT_TRUE(lin.isSuccess());
    EXPECT_DOUBLE_EQ(b.at(1).params.value(ProgramBuilder::kTcp).value<QVector<double>>()[0], 123.0);
}

TEST(ProgramBuilder, TcpAxisRangeChecked) {
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::LIN, sixZeros(), sixZeros(), 0, 0, false);
    EXPECT_EQ(b.setTcpComponent(0, 6, 1.0).error(), ProgramError::AxisOutOfRange);
}

// --- Structural -------------------------------------------------------------

TEST(ProgramBuilder, MoveReorders) {
    ProgramBuilder b;
    b.addComment("0");
    b.addComment("1");
    b.addComment("2");
    ASSERT_TRUE(b.move(0, 2).isSuccess());
    EXPECT_EQ(b.at(0).name, QStringLiteral("1"));
    EXPECT_EQ(b.at(1).name, QStringLiteral("2"));
    EXPECT_EQ(b.at(2).name, QStringLiteral("0"));
}

TEST(ProgramBuilder, CopyPasteCreatesDistinctStep) {
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::PTP, sixZeros(), sixZeros(), 0, 0, false);
    ASSERT_TRUE(b.copy(0).isSuccess());
    auto r = b.paste(-1);
    ASSERT_TRUE(r.isSuccess());
    ASSERT_EQ(b.stepCount(), 2);
    EXPECT_NE(b.at(0).uuid, b.at(1).uuid); // pasted step is a new, distinct step
}

TEST(ProgramBuilder, RemoveOutOfRangeReported) {
    ProgramBuilder b;
    auto r = b.remove(5);
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error(), ProgramError::IndexOutOfRange);
}

// --- Validation -------------------------------------------------------------

TEST(ProgramBuilder, EmptyProgramNotRunnable) {
    ProgramBuilder b;
    EXPECT_FALSE(b.isRunnable());
    const auto issues = b.validate();
    ASSERT_EQ(issues.size(), 1);
    EXPECT_EQ(issues[0].code, ProgramError::EmptyProgram);
    EXPECT_EQ(issues[0].severity, IssueSeverity::Error);
}

TEST(ProgramBuilder, AddMotionPointRejectsMissingPose) {
    ProgramBuilder b;
    auto r = b.addMotionPoint(MotionKind::PTP, QVector<double>{}, QVector<double>{}, 0, 0, false);
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error(), ProgramError::MotionPointMissingPose);
    EXPECT_EQ(b.stepCount(), 0); // rejected at teach time: nothing inserted
}

TEST(ProgramBuilder, MotionWithoutPoseFromExternalLoadBlocksRun) {
    // addMotionPoint rejects a pose-less point, but an external load can still deliver one; validate()
    // must catch it so RUN stays blocked.
    QVector<ProgramCommand> prog;
    prog.append(ProgramCommand(CommandType::Motion, QStringLiteral("PTP"), QStringLiteral("P1"), QVariantMap{}));
    ProgramBuilder b;
    b.load(prog);
    EXPECT_FALSE(b.isRunnable());
}

TEST(ProgramBuilder, ValidProgramIsRunnable) {
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::PTP, sixZeros(), sixZeros(), 0, 0, false);
    EXPECT_TRUE(b.isRunnable());
    EXPECT_TRUE(b.validate().isEmpty());
}

// --- Undo / redo / modified -------------------------------------------------

TEST(ProgramBuilder, UndoRedoRestoresContent) {
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::PTP, sixZeros(), sixZeros(), 0, 0, false); // count 1
    b.addWait(1.0);                                                         // count 2
    ASSERT_EQ(b.stepCount(), 2);
    ASSERT_TRUE(b.undo().isSuccess());
    EXPECT_EQ(b.stepCount(), 1);
    ASSERT_TRUE(b.redo().isSuccess());
    EXPECT_EQ(b.stepCount(), 2);
}

TEST(ProgramBuilder, UndoOnEmptyStackReported) {
    ProgramBuilder b;
    auto r = b.undo();
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error(), ProgramError::UndoStackEmpty);
}

TEST(ProgramBuilder, ModifiedFlagLifecycle) {
    ProgramBuilder b;
    EXPECT_FALSE(b.isModified());
    b.addComment("x");
    EXPECT_TRUE(b.isModified());
    b.markSaved();
    EXPECT_FALSE(b.isModified());
    b.load(QVector<ProgramCommand>{});
    EXPECT_FALSE(b.isModified());
}

// --- Reset origin: external load must not upload; local edits/loads must -----

TEST(ProgramBuilder, ResetReasonDistinguishesExternalLoadFromLocalEdits) {
    using hexa::ProgramResetReason;
    ProgramBuilder b;
    ProgramResetReason lastReason = ProgramResetReason::LocalEdit;
    int resetCount = 0;
    QObject::connect(&b, &ProgramBuilder::programReset, &b,
                     [&](ProgramResetReason reason) { lastReason = reason; ++resetCount; });

    // Default load() is an external/controller load -> must be tagged ExternalLoad (no upload).
    b.load(QVector<ProgramCommand>{});
    EXPECT_EQ(resetCount, 1);
    EXPECT_EQ(lastReason, ProgramResetReason::ExternalLoad);

    // undo/redo are operator-driven local resets -> LocalEdit (panel re-uploads).
    b.addComment("x");
    ASSERT_TRUE(b.undo().isSuccess());
    EXPECT_EQ(lastReason, ProgramResetReason::LocalEdit);
    ASSERT_TRUE(b.redo().isSuccess());
    EXPECT_EQ(lastReason, ProgramResetReason::LocalEdit);

    // A local file load explicitly opts into LocalEdit.
    b.load(QVector<ProgramCommand>{}, ProgramResetReason::LocalEdit);
    EXPECT_EQ(lastReason, ProgramResetReason::LocalEdit);

    // clear() is a local reset too.
    b.clear();
    EXPECT_EQ(lastReason, ProgramResetReason::LocalEdit);
}

TEST(ProgramBuilder, SetZoneRejectsUnknownAndAcceptsKnown) {
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::LIN, sixZeros(), sixZeros(), 0, 0, false);
    auto bad = b.setZone(0, QStringLiteral("APPROX 999mm"));
    ASSERT_TRUE(bad.isError());
    EXPECT_EQ(bad.error(), ProgramError::ZoneInvalid);
    EXPECT_TRUE(b.setZone(0, QStringLiteral("APPROX 10mm")).isSuccess());
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kZone).toString(), QStringLiteral("APPROX 10mm"));
}

TEST(ProgramBuilder, MoveOutOfRangeReported) {
    ProgramBuilder b;
    b.addComment("only");
    auto r = b.move(0, 5);
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error(), ProgramError::IndexOutOfRange);
}

// --- Flow control: LABEL / GOTO (B') ----------------------------------------

namespace {
bool hasIssue(const QVector<hexa::ProgramIssue>& issues, ProgramError code) {
    for (const auto& i : issues) {
        if (i.code == code) return true;
    }
    return false;
}
} // namespace

TEST(ProgramBuilder, AddLabelAndGotoInsertLogicSteps) {
    ProgramBuilder b;
    auto rl = b.addLabel(3);
    ASSERT_TRUE(rl.isSuccess());
    EXPECT_EQ(b.at(rl.value()).type, CommandType::Logic);
    EXPECT_EQ(b.at(rl.value()).code, QStringLiteral("LABEL"));
    EXPECT_EQ(b.at(rl.value()).params.value(ProgramBuilder::kLabelId).toInt(), 3);

    auto rg = b.addGoto(3);
    ASSERT_TRUE(rg.isSuccess());
    EXPECT_EQ(b.at(rg.value()).code, QStringLiteral("GOTO"));
    EXPECT_EQ(b.at(rg.value()).params.value(ProgramBuilder::kLabelId).toInt(), 3);
}

TEST(ProgramBuilder, AddLabelAndGotoRejectNegativeId) {
    ProgramBuilder b;
    EXPECT_EQ(b.addLabel(-1).error(), ProgramError::NegativeLabelId);
    EXPECT_EQ(b.addGoto(-2).error(), ProgramError::NegativeLabelId);
    EXPECT_EQ(b.stepCount(), 0); // both rejected: nothing inserted
}

TEST(ProgramBuilder, DuplicateLabelIdBlocksRun) {
    ProgramBuilder b;
    b.addLabel(5);
    b.addLabel(5); // duplicate id
    EXPECT_TRUE(hasIssue(b.validate(), ProgramError::DuplicateLabelId));
    EXPECT_FALSE(b.isRunnable());
}

TEST(ProgramBuilder, UnresolvedGotoBlocksRun) {
    ProgramBuilder b;
    b.addGoto(7); // no LABEL 7 defined
    EXPECT_TRUE(hasIssue(b.validate(), ProgramError::UnresolvedJumpTarget));
    EXPECT_FALSE(b.isRunnable());
}

TEST(ProgramBuilder, ResolvedLabelGotoLoopIsRunnable) {
    ProgramBuilder b;
    b.addLabel(1);
    b.addMotionPoint(MotionKind::PTP, sixZeros(), sixZeros(), 0, 0, false);
    b.addGoto(1); // resolves to LABEL 1
    EXPECT_FALSE(hasIssue(b.validate(), ProgramError::UnresolvedJumpTarget));
    EXPECT_TRUE(b.isRunnable());
}

// --- DI conditions: WAIT DI / IF (B'-2) -------------------------------------

TEST(ProgramBuilder, AddWaitDiInsertsIoStepWithCondition) {
    ProgramBuilder b;
    auto r = b.addWaitDI(3, /*triggerState=*/true);
    ASSERT_TRUE(r.isSuccess());
    EXPECT_EQ(b.at(0).type, CommandType::IO);
    EXPECT_EQ(b.at(0).code, QStringLiteral("WAIT DI"));
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kPort).toInt(), 3);
    EXPECT_TRUE(b.at(0).params.value(ProgramBuilder::kState).toBool());
}

TEST(ProgramBuilder, AddWaitDiRejectsPortOutOfRange) {
    ProgramBuilder b;
    EXPECT_EQ(b.addWaitDI(0, true).error(), ProgramError::IoPortOutOfRange);
    EXPECT_EQ(b.addWaitDI(33, true).error(), ProgramError::IoPortOutOfRange);
    EXPECT_EQ(b.stepCount(), 0);
}

TEST(ProgramBuilder, AddConditionalJumpInsertsIfStep) {
    ProgramBuilder b;
    auto r = b.addConditionalJump(5, /*triggerState=*/false, /*targetLabelId=*/2);
    ASSERT_TRUE(r.isSuccess());
    EXPECT_EQ(b.at(0).type, CommandType::Logic);
    EXPECT_EQ(b.at(0).code, QStringLiteral("IF"));
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kPort).toInt(), 5);
    EXPECT_FALSE(b.at(0).params.value(ProgramBuilder::kState).toBool());
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kLabelId).toInt(), 2);
}

TEST(ProgramBuilder, AddConditionalJumpRejectsBadArgs) {
    ProgramBuilder b;
    EXPECT_EQ(b.addConditionalJump(99, true, 1).error(), ProgramError::IoPortOutOfRange);
    EXPECT_EQ(b.addConditionalJump(1, true, -1).error(), ProgramError::NegativeLabelId);
    EXPECT_EQ(b.stepCount(), 0);
}

TEST(ProgramBuilder, SetConditionEditsAndTypeChecks) {
    ProgramBuilder b;
    b.addWaitDI(1, true);
    auto ok = b.setCondition(0, 7, /*triggerState=*/false);
    ASSERT_TRUE(ok.isSuccess());
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kPort).toInt(), 7);
    EXPECT_FALSE(b.at(0).params.value(ProgramBuilder::kState).toBool());
    // Out-of-range port is rejected.
    EXPECT_EQ(b.setCondition(0, 40, true).error(), ProgramError::IoPortOutOfRange);
    // A non-condition step cannot take a condition.
    b.addComment("c"); // index 1
    EXPECT_EQ(b.setCondition(1, 2, true).error(), ProgramError::NotAConditionStep);
}

// --- Digital output: SET DO (sequencer P3) -----------------------------------

TEST(ProgramBuilder, AddSetDoInsertsIoStepWithAssignment) {
    ProgramBuilder b;
    auto r = b.addSetDo(5, /*state=*/true);
    ASSERT_TRUE(r.isSuccess());
    EXPECT_EQ(b.at(0).type, CommandType::IO);
    EXPECT_EQ(b.at(0).code, QStringLiteral("SET DO"));
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kPort).toInt(), 5);
    EXPECT_TRUE(b.at(0).params.value(ProgramBuilder::kState).toBool());
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kCondition).toString(),
              QStringLiteral("DO[5]=HIGH"));
}

TEST(ProgramBuilder, AddSetDoRejectsPortOutOfRange) {
    ProgramBuilder b;
    EXPECT_EQ(b.addSetDo(0, true).error(), ProgramError::IoPortOutOfRange);
    EXPECT_EQ(b.addSetDo(33, true).error(), ProgramError::IoPortOutOfRange);
    EXPECT_EQ(b.stepCount(), 0);
}

// --- Registers + BREAK (sequencer P4) ----------------------------------------

TEST(ProgramBuilder, AddRegisterStepsInsertLogicStepsWithParams) {
    ProgramBuilder b;
    auto r1 = b.addSetVar(0, 5);
    ASSERT_TRUE(r1.isSuccess());
    EXPECT_EQ(b.at(0).code, QStringLiteral("SET VAR"));
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kReg).toInt(), 0);
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kValue).toInt(), 5);
    EXPECT_EQ(b.at(0).name, QStringLiteral("SET R[0]=5"));

    auto r2 = b.addIncVar(1);
    ASSERT_TRUE(r2.isSuccess());
    EXPECT_EQ(b.at(1).code, QStringLiteral("INC VAR"));

    auto r3 = b.addDecVar(0);
    ASSERT_TRUE(r3.isSuccess());
    EXPECT_EQ(b.at(2).code, QStringLiteral("DEC VAR"));

    auto r4 = b.addBreak();
    ASSERT_TRUE(r4.isSuccess());
    EXPECT_EQ(b.at(3).code, QStringLiteral("BREAK"));
}

TEST(ProgramBuilder, AddRegisterStepsRejectIndexOutOfRange) {
    ProgramBuilder b;
    EXPECT_EQ(b.addSetVar(-1, 0).error(), ProgramError::RegisterIndexOutOfRange);
    EXPECT_EQ(b.addSetVar(16, 0).error(), ProgramError::RegisterIndexOutOfRange);
    EXPECT_EQ(b.addIncVar(16).error(), ProgramError::RegisterIndexOutOfRange);
    EXPECT_EQ(b.addDecVar(16).error(), ProgramError::RegisterIndexOutOfRange);
    EXPECT_EQ(b.stepCount(), 0);
}

TEST(ProgramBuilder, AddRegisterIfValidatesAndLabels) {
    ProgramBuilder b;
    b.addLabel(4);
    auto r = b.addConditionalJumpOnRegister(1, QStringLiteral("GT"), 0, 4);
    ASSERT_TRUE(r.isSuccess());
    EXPECT_EQ(b.at(1).code, QStringLiteral("IF"));
    EXPECT_EQ(b.at(1).params.value(ProgramBuilder::kCondSource).toString(), QStringLiteral("REG"));
    EXPECT_EQ(b.at(1).params.value(ProgramBuilder::kCondition).toString(), QStringLiteral("R[1]>0"));
    EXPECT_EQ(b.at(1).name, QStringLiteral("IF R[1]>0 GOTO 4"));
    EXPECT_TRUE(b.isRunnable());

    EXPECT_EQ(b.addConditionalJumpOnRegister(16, QStringLiteral("GT"), 0, 4).error(),
              ProgramError::RegisterIndexOutOfRange);
    EXPECT_EQ(b.addConditionalJumpOnRegister(1, QStringLiteral("XX"), 0, 4).error(),
              ProgramError::InvalidCompareOp);
}

TEST(ProgramBuilder, RegisterIfEditsOnlyThroughRegisterPath) {
    // The condition source of an existing IF never flips silently: the DI edit path refuses a
    // register IF and vice versa; the register path relabels correctly.
    ProgramBuilder b;
    b.addLabel(4);
    ASSERT_TRUE(b.addConditionalJumpOnRegister(1, QStringLiteral("GT"), 0, 4).isSuccess());
    EXPECT_EQ(b.setCondition(1, 3, true).error(), ProgramError::NotAConditionStep);

    auto ok = b.setRegisterCondition(1, 2, QStringLiteral("EQ"), 7);
    ASSERT_TRUE(ok.isSuccess());
    EXPECT_EQ(b.at(1).params.value(ProgramBuilder::kCondition).toString(), QStringLiteral("R[2]=7"));
    EXPECT_EQ(b.at(1).name, QStringLiteral("IF R[2]=7 GOTO 4"));

    b.addWaitDI(1, true); // index 2: a DI step is refused by the register edit path
    EXPECT_EQ(b.setRegisterCondition(2, 1, QStringLiteral("EQ"), 0).error(),
              ProgramError::NotAConditionStep);
}

TEST(ProgramBuilder, ValidateFlagsRegisterIssues) {
    // validate() mirrors the sequencer's fail-closed load: an out-of-range register blocks RUN.
    // Authoring already rejects bad indexes, so forge one via the external-load path.
    ProgramBuilder b;
    ProgramCommand bad(CommandType::Logic, QStringLiteral("SET VAR"), QStringLiteral("SET R[99]=1"));
    bad.params[ProgramBuilder::kReg] = 99;
    bad.params[ProgramBuilder::kValue] = 1;
    b.load({bad}, hexa::ProgramResetReason::ExternalLoad);
    EXPECT_TRUE(hasIssue(b.validate(), ProgramError::RegisterIndexOutOfRange));
    EXPECT_FALSE(b.isRunnable());
}

TEST(ProgramBuilder, SetConditionRelabelsSetDoAsOutput) {
    // Editing a SET DO through the shared IO edit path must keep the DO[...] label and row name in
    // sync (not relabel it as a DI condition).
    ProgramBuilder b;
    b.addSetDo(5, true);
    auto ok = b.setCondition(0, 9, /*triggerState=*/false);
    ASSERT_TRUE(ok.isSuccess());
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kPort).toInt(), 9);
    EXPECT_FALSE(b.at(0).params.value(ProgramBuilder::kState).toBool());
    EXPECT_EQ(b.at(0).params.value(ProgramBuilder::kCondition).toString(),
              QStringLiteral("DO[9]=LOW"));
    EXPECT_EQ(b.at(0).name, QStringLiteral("SET DO[9]=LOW"));
}

TEST(ProgramBuilder, UnresolvedIfTargetBlocksRun) {
    ProgramBuilder b;
    b.addConditionalJump(1, true, 9); // no LABEL 9 defined
    EXPECT_TRUE(hasIssue(b.validate(), ProgramError::UnresolvedJumpTarget));
    EXPECT_FALSE(b.isRunnable());
}

TEST(ProgramBuilder, LoadedWaitDiWithBadPortBlocksRun) {
    // addWaitDI rejects a bad port, but an external load can still deliver one; validate() must catch
    // it so RUN stays blocked (mirrors the controller's fail-closed condition check).
    QVariantMap params;
    params[ProgramBuilder::kPort] = 99;
    params[ProgramBuilder::kState] = true;
    QVector<ProgramCommand> prog;
    prog.append(ProgramCommand(CommandType::IO, QStringLiteral("WAIT DI"), QStringLiteral("w"), params));
    ProgramBuilder b;
    b.load(prog);
    EXPECT_TRUE(hasIssue(b.validate(), ProgramError::IoPortOutOfRange));
    EXPECT_FALSE(b.isRunnable());
}
// --- END OF FILE: HexaStudio/program_editor/test/ProgramBuilderTests.cpp ---
