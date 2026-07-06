// --- START OF FILE: HexaStudio/program_editor/test/PipelineE2ETests.cpp ---
/**
 * @file PipelineE2ETests.cpp
 * @brief Big pipeline integration test: studio authoring -> studio/wire mapping -> controller
 *        execution, exercised in one process without a network or hardware.
 *
 * Chain under test:
 *   hexa::ProgramBuilder          (studio: author the flat program)
 *     -> hexa::program_mapper::toRdt  (studio <-> wire contract, the HexaBackend mapper)
 *       -> RDT::ProgramSequencer      (controller: load + execute loops / branches / DI waits)
 *
 * This closes the loop no single unit test covers: it proves a GOTO loop, an IF-on-DI branch and a
 * blocking WAIT DI authored on the pendant map to the wire and actually execute on the controller
 * sequencer. The sequencer's own execution semantics are covered by its module tests; here we verify
 * the end-to-end contract holds across the module boundary.
 */
#include <gtest/gtest.h>

#include <QVector>

#include "ProgramBuilder.h"     // hexa::ProgramBuilder
#include "ProgramMapper.h"      // hexa::program_mapper::toRdt / toHmi
#include "ProgramSequencer.h"   // RDT::ProgramSequencer
#include "RdtProtocol.h"        // RDT::NetProtocol types
#include "DataTypes.h"          // RDT::AxisId

using hexa::MotionKind;
using hexa::ProgramBuilder;
namespace pm = hexa::program_mapper;
using RDT::ProgramSequencer;
using RDT::StepActionKind;
using RDT::WorldSample;
namespace Net = RDT::NetProtocol;

namespace {

// A joint vector whose first axis carries a distinguishing value (the rest zero).
QVector<double> jointsA1(double firstAxisDeg) {
    return QVector<double>{firstAxisDeg, 0.0, 0.0, 0.0, 0.0, 0.0};
}

// The first-axis target of the first motion step in a PlanMotionChain action.
double firstChainAxis1(const RDT::StepAction& action) {
    return action.motion_chain.front().joint_target[static_cast<RDT::AxisId>(0)].position.value();
}

} // namespace

// --- Studio <-> wire contract: every enriched field survives the mapping ---------------------------

TEST(PipelineE2E, MapperCarriesEveryControlFlowField) {
    ProgramBuilder b;
    b.addLabel(5);                                                    // 0
    b.addMotionPoint(MotionKind::PTP, jointsA1(10.0), {}, 2, 3, false); // 1 (tool 2 / base 3)
    b.addWaitDI(4, /*triggerState=*/true);                           // 2
    b.addConditionalJump(2, /*triggerState=*/false, /*target=*/5);   // 3
    b.addGoto(5);                                                    // 4

    const Net::ProgramDataStruct rdt = pm::toRdt(b.program());
    ASSERT_EQ(rdt.steps.size(), 5u);

    EXPECT_EQ(rdt.steps[0].type, Net::StepType::Label);
    EXPECT_EQ(rdt.steps[0].id, 5u);                                   // LABEL id -> step id

    EXPECT_EQ(rdt.steps[1].type, Net::StepType::MoveJ);
    EXPECT_EQ(rdt.steps[1].tool_id, 2u);                             // execution context preserved
    EXPECT_EQ(rdt.steps[1].base_id, 3u);

    EXPECT_EQ(rdt.steps[2].type, Net::StepType::WaitDI);
    EXPECT_EQ(rdt.steps[2].condition.io_port, 4);
    EXPECT_TRUE(rdt.steps[2].condition.trigger_on_state);

    EXPECT_EQ(rdt.steps[3].type, Net::StepType::ConditionalJump);
    EXPECT_EQ(rdt.steps[3].condition.io_port, 2);
    EXPECT_FALSE(rdt.steps[3].condition.trigger_on_state);
    EXPECT_EQ(rdt.steps[3].jump_target_id, 5u);                       // IF jump target

    EXPECT_EQ(rdt.steps[4].type, Net::StepType::JumpToLabel);
    EXPECT_EQ(rdt.steps[4].jump_target_id, 5u);                       // GOTO jump target

    // The inverse mapping restores the authoring params for the editor.
    const QVector<ProgramCommand> hmi = pm::toHmi(rdt);
    ASSERT_EQ(hmi.size(), 5);
    EXPECT_EQ(hmi[1].params.value(ProgramBuilder::kToolId).toInt(), 2);
    EXPECT_EQ(hmi[1].params.value(ProgramBuilder::kBaseId).toInt(), 3);
    EXPECT_EQ(hmi[2].params.value(ProgramBuilder::kPort).toInt(), 4);
    EXPECT_TRUE(hmi[2].params.value(ProgramBuilder::kState).toBool());
    EXPECT_EQ(hmi[3].params.value(ProgramBuilder::kLabelId).toInt(), 5);
}

// --- Loop: LABEL + MoveJ + GOTO executes and re-enters the motion each iteration -------------------

TEST(PipelineE2E, GotoLabelLoopExecutes) {
    ProgramBuilder b;
    b.addLabel(1);                                                    // 0
    b.addMotionPoint(MotionKind::PTP, jointsA1(10.0), {}, 0, 0, false); // 1
    b.addGoto(1);                                                    // 2

    ProgramSequencer seq;
    ASSERT_TRUE(seq.load(pm::toRdt(b.program())).isSuccess());

    const WorldSample w{};
    // Consume LABEL 1, reach the MoveJ -> PlanMotionChain.
    const auto a1 = seq.advance(w);
    ASSERT_TRUE(a1.isSuccess());
    EXPECT_EQ(a1.value().kind, StepActionKind::PlanMotionChain);
    ASSERT_EQ(a1.value().motion_chain.size(), 1u);

    // Motion done -> next advance consumes GOTO 1, jumps back to LABEL 1, reaches the MoveJ again.
    seq.onActionCompleted();
    const auto a2 = seq.advance(w);
    ASSERT_TRUE(a2.isSuccess());
    EXPECT_EQ(a2.value().kind, StepActionKind::PlanMotionChain);   // looped, not Finished
}

// --- Branch: IF-on-DI takes the LABEL path when the input matches, falls through otherwise ---------

TEST(PipelineE2E, IfBranchesOnDigitalInput) {
    // 0: IF DI[1]=HIGH GOTO 9 | 1: MoveJ A(10) | 2: LABEL 9 | 3: MoveJ B(20)
    ProgramBuilder b;
    b.addConditionalJump(1, /*triggerState=*/true, /*target=*/9);
    b.addMotionPoint(MotionKind::PTP, jointsA1(10.0), {}, 0, 0, false);
    b.addLabel(9);
    b.addMotionPoint(MotionKind::PTP, jointsA1(20.0), {}, 0, 0, false);

    const Net::ProgramDataStruct rdt = pm::toRdt(b.program());

    // Run a fresh sequencer with a given DI bitmask and return the first planned axis-1 target.
    auto firstMotionAxis1 = [&rdt](std::uint32_t digital_inputs) -> double {
        ProgramSequencer seq;
        EXPECT_TRUE(seq.load(rdt).isSuccess());
        const auto a = seq.advance(WorldSample{digital_inputs});
        EXPECT_TRUE(a.isSuccess());
        EXPECT_EQ(a.value().kind, StepActionKind::PlanMotionChain);
        return firstChainAxis1(a.value());
    };

    // DI1 LOW -> condition (HIGH) not met -> fall through to MoveJ A (10).
    EXPECT_DOUBLE_EQ(firstMotionAxis1(0u), 10.0);
    // DI1 HIGH (bit 0 set) -> condition met -> jump to LABEL 9 -> MoveJ B (20).
    EXPECT_DOUBLE_EQ(firstMotionAxis1(0b1u), 20.0);
}

// --- Blocking wait: WAIT DI holds until the input matches, then self-completes ---------------------

TEST(PipelineE2E, WaitDiBlocksThenSelfCompletes) {
    // 0: WAIT DI[2]=HIGH | 1: MoveJ
    ProgramBuilder b;
    b.addWaitDI(2, /*triggerState=*/true);
    b.addMotionPoint(MotionKind::PTP, jointsA1(5.0), {}, 0, 0, false);

    ProgramSequencer seq;
    ASSERT_TRUE(seq.load(pm::toRdt(b.program())).isSuccess());

    // DI2 low -> block on the input, reporting the condition.
    const auto a1 = seq.advance(WorldSample{0u});
    ASSERT_TRUE(a1.isSuccess());
    EXPECT_EQ(a1.value().kind, StepActionKind::WaitForInput);
    EXPECT_EQ(a1.value().wait_condition.io_port, 2);

    // Still low -> still waiting (the sequencer owns and re-polls the condition).
    const auto a2 = seq.advance(WorldSample{0u});
    ASSERT_TRUE(a2.isSuccess());
    EXPECT_EQ(a2.value().kind, StepActionKind::WaitForInput);

    // DI2 high (bit 1 set) -> condition met -> the WaitDI self-completes and the MoveJ is planned.
    const auto a3 = seq.advance(WorldSample{0b10u});
    ASSERT_TRUE(a3.isSuccess());
    EXPECT_EQ(a3.value().kind, StepActionKind::PlanMotionChain);
}

// --- Digital output: SET DO reaches the sequencer with its authored port/state (P3) ----------------

TEST(PipelineE2E, SetDoCarriesOutputThroughWireAndExecutes) {
    // 0: SET DO[5]=HIGH | 1: MoveJ. The sequencer only DECIDES the output action (the controller
    // actuates it through the HAL), so the step is instant: the next advance plans the motion.
    ProgramBuilder b;
    b.addSetDo(5, /*state=*/true);
    b.addMotionPoint(MotionKind::PTP, jointsA1(5.0), {}, 0, 0, false);

    ProgramSequencer seq;
    ASSERT_TRUE(seq.load(pm::toRdt(b.program())).isSuccess());

    const auto a1 = seq.advance(WorldSample{0u});
    ASSERT_TRUE(a1.isSuccess());
    EXPECT_EQ(a1.value().kind, StepActionKind::SetOutput);
    EXPECT_EQ(a1.value().io_port, 5);
    EXPECT_TRUE(a1.value().io_state);

    const auto a2 = seq.advance(WorldSample{0u});
    ASSERT_TRUE(a2.isSuccess());
    EXPECT_EQ(a2.value().kind, StepActionKind::PlanMotionChain);
}

// --- Registers (P4): the authored counter loop runs exactly N iterations through the wire --------

TEST(PipelineE2E, RegisterCounterLoopAuthoredOnPendantRunsNIterations) {
    // The canonical "do N cycles" program, authored exactly as an operator would:
    //   0: SET R[0]=2 | 1: LABEL 1 | 2: MoveJ | 3: DEC R[0] | 4: IF R[0]>0 GOTO 1
    ProgramBuilder b;
    b.addSetVar(0, 2);
    b.addLabel(1);
    b.addMotionPoint(MotionKind::PTP, jointsA1(5.0), {}, 0, 0, false);
    b.addDecVar(0);
    b.addConditionalJumpOnRegister(0, QStringLiteral("GT"), 0, /*label*/ 1);
    ASSERT_TRUE(b.isRunnable());

    ProgramSequencer seq;
    ASSERT_TRUE(seq.load(pm::toRdt(b.program())).isSuccess());

    // Two motion iterations, then Finished — the loop exits via the register compare.
    const auto a1 = seq.advance(WorldSample{0u});
    ASSERT_TRUE(a1.isSuccess());
    ASSERT_EQ(a1.value().kind, StepActionKind::PlanMotionChain);
    seq.onActionCompleted();

    const auto a2 = seq.advance(WorldSample{0u});
    ASSERT_TRUE(a2.isSuccess());
    ASSERT_EQ(a2.value().kind, StepActionKind::PlanMotionChain);
    seq.onActionCompleted();

    const auto a3 = seq.advance(WorldSample{0u});
    ASSERT_TRUE(a3.isSuccess());
    EXPECT_EQ(a3.value().kind, StepActionKind::Finished);
}

TEST(PipelineE2E, BreakAuthoredOnPendantStopsExecution) {
    // 0: MoveJ | 1: BREAK | 2: MoveJ — the trailing motion must never be planned.
    ProgramBuilder b;
    b.addMotionPoint(MotionKind::PTP, jointsA1(5.0), {}, 0, 0, false);
    b.addBreak();
    b.addMotionPoint(MotionKind::PTP, jointsA1(10.0), {}, 0, 0, false);

    ProgramSequencer seq;
    ASSERT_TRUE(seq.load(pm::toRdt(b.program())).isSuccess());

    const auto a1 = seq.advance(WorldSample{0u});
    ASSERT_TRUE(a1.isSuccess());
    ASSERT_EQ(a1.value().kind, StepActionKind::PlanMotionChain);
    ASSERT_EQ(a1.value().motion_chain.size(), 1u);
    seq.onActionCompleted();

    const auto a2 = seq.advance(WorldSample{0u});
    ASSERT_TRUE(a2.isSuccess());
    EXPECT_EQ(a2.value().kind, StepActionKind::Break);
    EXPECT_EQ(a2.value().executing_line, 1);
}
// --- END OF FILE: HexaStudio/program_editor/test/PipelineE2ETests.cpp ---
