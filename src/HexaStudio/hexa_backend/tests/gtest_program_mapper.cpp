// --- START OF FILE: HexaStudio/hexa_backend/tests/gtest_program_mapper.cpp ---
// Round-trip tests over the program pipeline mappers (audit item A2). They pin the exact defect class
// the shipping RobotService had: fields silently dropped on the wire (GOTO/IF/WaitDI linkage, Tool/
// Base context, blending zone). Every supported step type goes HMI -> RDT -> HMI and must come back
// semantically identical; plus the local JSON envelope round-trip of ProgramCommand.
#include <gtest/gtest.h>

#include "ProgramMapper.h"
#include "ProgramData.h"

// ProgramCommand / CommandType live at global scope (hexa_contracts/ProgramData.h).
namespace mapper = hexa::program_mapper;
namespace Net = RDT::NetProtocol;

namespace {

QVector<ProgramCommand> roundTrip(const QVector<ProgramCommand>& program) {
    return mapper::toHmi(mapper::toRdt(program));
}

QVector<double> jointsOf(const ProgramCommand& cmd) {
    return cmd.params.value(QStringLiteral("Joints")).value<QVector<double>>();
}

} // namespace

TEST(ProgramMapperTest, MotionPtpRoundTrip) {
    ProgramCommand ptp(CommandType::Motion, QStringLiteral("PTP"), QStringLiteral("Pick"));
    ptp.params[QStringLiteral("Speed")] = 50;
    ptp.params[QStringLiteral("Zone")] = QStringLiteral("APPROX 10mm");
    ptp.params[QStringLiteral("Joints")] =
        QVariant::fromValue(QVector<double>{10.0, -20.0, 30.0, 0.0, 15.0, -5.0});
    ptp.params[QStringLiteral("ToolId")] = 1;
    ptp.params[QStringLiteral("BaseId")] = 2;

    const auto back = roundTrip({ptp});
    ASSERT_EQ(back.size(), 1);
    EXPECT_EQ(back[0].type, CommandType::Motion);
    EXPECT_EQ(back[0].code, QStringLiteral("PTP"));
    EXPECT_EQ(back[0].name, QStringLiteral("Pick"));
    EXPECT_EQ(back[0].params.value(QStringLiteral("Speed")).toInt(), 50);
    EXPECT_EQ(back[0].params.value(QStringLiteral("Zone")).toString(), QStringLiteral("APPROX 10mm"));
    EXPECT_EQ(back[0].params.value(QStringLiteral("ToolId")).toInt(), 1);
    EXPECT_EQ(back[0].params.value(QStringLiteral("BaseId")).toInt(), 2);
    const QVector<double> joints = jointsOf(back[0]);
    ASSERT_EQ(joints.size(), 6);
    EXPECT_DOUBLE_EQ(joints[0], 10.0);
    EXPECT_DOUBLE_EQ(joints[1], -20.0);
    EXPECT_DOUBLE_EQ(joints[5], -5.0);
}

TEST(ProgramMapperTest, MotionLinCartesianRoundTrip) {
    ProgramCommand lin(CommandType::Motion, QStringLiteral("LIN"), QStringLiteral("Slide"));
    lin.params[QStringLiteral("Speed")] = 25;
    lin.params[QStringLiteral("Zone")] = QStringLiteral("FINE");
    lin.params[QStringLiteral("TcpPose")] =
        QVariant::fromValue(QVector<double>{500.0, 0.0, 400.0, 0.0, 90.0, 0.0});

    const auto back = roundTrip({lin});
    ASSERT_EQ(back.size(), 1);
    EXPECT_EQ(back[0].code, QStringLiteral("LIN"));
    EXPECT_EQ(back[0].params.value(QStringLiteral("Zone")).toString(), QStringLiteral("FINE"));
    const QVector<double> tcp =
        back[0].params.value(QStringLiteral("TcpPose")).value<QVector<double>>();
    ASSERT_EQ(tcp.size(), 6);
    EXPECT_DOUBLE_EQ(tcp[0], 500.0);
    EXPECT_DOUBLE_EQ(tcp[2], 400.0);
    EXPECT_DOUBLE_EQ(tcp[4], 90.0);
}

TEST(ProgramMapperTest, MotionCircRoundTripCarriesViaPoint) {
    // CIRC maps to StepType::MoveC and must carry BOTH the target (TcpPose) and the auxiliary via
    // point (TcpViaPose) through the wire round trip (docs/REQ_motion_circ.md).
    ProgramCommand circ(CommandType::Motion, QStringLiteral("CIRC"), QStringLiteral("Arc"));
    circ.params[QStringLiteral("Speed")] = 40;
    circ.params[QStringLiteral("Zone")] = QStringLiteral("FINE");
    circ.params[QStringLiteral("TcpPose")] =
        QVariant::fromValue(QVector<double>{600.0, 100.0, 400.0, 0.0, 90.0, 0.0});
    circ.params[QStringLiteral("TcpViaPose")] =
        QVariant::fromValue(QVector<double>{550.0, 50.0, 420.0, 0.0, 0.0, 0.0});

    const auto rdt = mapper::toRdt({circ});
    ASSERT_EQ(rdt.steps.size(), 1u);
    EXPECT_EQ(rdt.steps[0].type, Net::StepType::MoveC);
    EXPECT_DOUBLE_EQ(rdt.steps[0].cartesian_via.x.value(), 550.0);
    EXPECT_DOUBLE_EQ(rdt.steps[0].cartesian_via.z.value(), 420.0);

    const auto back = mapper::toHmi(rdt);
    ASSERT_EQ(back.size(), 1);
    EXPECT_EQ(back[0].type, CommandType::Motion);
    EXPECT_EQ(back[0].code, QStringLiteral("CIRC"));
    const QVector<double> via =
        back[0].params.value(QStringLiteral("TcpViaPose")).value<QVector<double>>();
    ASSERT_EQ(via.size(), 6);
    EXPECT_DOUBLE_EQ(via[0], 550.0);
    EXPECT_DOUBLE_EQ(via[1], 50.0);
    EXPECT_DOUBLE_EQ(via[2], 420.0);
    const QVector<double> tcp =
        back[0].params.value(QStringLiteral("TcpPose")).value<QVector<double>>();
    ASSERT_EQ(tcp.size(), 6);
    EXPECT_DOUBLE_EQ(tcp[0], 600.0);
}

TEST(ProgramMapperTest, MotionSplineRoundTrip) {
    // SPLINE maps to StepType::MoveS with the LIN wire shape (target in TcpPose); the code and the
    // pose must survive the round trip (docs/REQ_motion_spline.md, batch 2b).
    ProgramCommand spl(CommandType::Motion, QStringLiteral("SPLINE"), QStringLiteral("Spl 1"));
    spl.params[QStringLiteral("Speed")] = 60;
    spl.params[QStringLiteral("Zone")] = QStringLiteral("FINE");
    spl.params[QStringLiteral("TcpPose")] =
        QVariant::fromValue(QVector<double>{300.0, -50.0, 350.0, 0.0, 90.0, 0.0});

    const auto rdt = mapper::toRdt({spl});
    ASSERT_EQ(rdt.steps.size(), 1u);
    EXPECT_EQ(rdt.steps[0].type, Net::StepType::MoveS);
    EXPECT_DOUBLE_EQ(rdt.steps[0].cartesian_target.x.value(), 300.0);
    EXPECT_DOUBLE_EQ(rdt.steps[0].speed_ratio, 60.0);

    const auto back = mapper::toHmi(rdt);
    ASSERT_EQ(back.size(), 1);
    EXPECT_EQ(back[0].type, CommandType::Motion);
    EXPECT_EQ(back[0].code, QStringLiteral("SPLINE"));
    const QVector<double> tcp =
        back[0].params.value(QStringLiteral("TcpPose")).value<QVector<double>>();
    ASSERT_EQ(tcp.size(), 6);
    EXPECT_DOUBLE_EQ(tcp[0], 300.0);
    EXPECT_DOUBLE_EQ(tcp[1], -50.0);
}

TEST(ProgramMapperTest, FlowControlLinkageSurvives) {
    // The exact fields the OLD (shipping) mapper dropped: label ids, jump targets, DI conditions.
    ProgramCommand label(CommandType::Logic, QStringLiteral("LABEL"), QStringLiteral("loop"));
    label.params[QStringLiteral("LabelId")] = 7;

    ProgramCommand gotoCmd(CommandType::Logic, QStringLiteral("GOTO"), QStringLiteral("again"));
    gotoCmd.params[QStringLiteral("LabelId")] = 7;

    ProgramCommand ifCmd(CommandType::Logic, QStringLiteral("IF"), QStringLiteral("branch"));
    ifCmd.params[QStringLiteral("LabelId")] = 7;
    ifCmd.params[QStringLiteral("Port")] = 3;
    ifCmd.params[QStringLiteral("State")] = false;

    const Net::ProgramDataStruct rdt = mapper::toRdt({label, gotoCmd, ifCmd});
    ASSERT_EQ(rdt.steps.size(), 3u);
    EXPECT_EQ(rdt.steps[0].type, Net::StepType::Label);
    EXPECT_EQ(rdt.steps[0].id, 7u);                       // label id IS the jump-table key
    EXPECT_EQ(rdt.steps[1].type, Net::StepType::JumpToLabel);
    EXPECT_EQ(rdt.steps[1].jump_target_id, 7u);
    EXPECT_EQ(rdt.steps[2].type, Net::StepType::ConditionalJump);
    EXPECT_EQ(rdt.steps[2].jump_target_id, 7u);
    EXPECT_EQ(rdt.steps[2].condition.io_port, 3);
    EXPECT_FALSE(rdt.steps[2].condition.trigger_on_state);

    const auto back = mapper::toHmi(rdt);
    ASSERT_EQ(back.size(), 3);
    EXPECT_EQ(back[0].code, QStringLiteral("LABEL"));
    EXPECT_EQ(back[0].params.value(QStringLiteral("LabelId")).toInt(), 7);
    EXPECT_EQ(back[1].code, QStringLiteral("GOTO"));
    EXPECT_EQ(back[1].params.value(QStringLiteral("LabelId")).toInt(), 7);
    EXPECT_EQ(back[2].code, QStringLiteral("IF"));
    EXPECT_EQ(back[2].params.value(QStringLiteral("LabelId")).toInt(), 7);
    EXPECT_EQ(back[2].params.value(QStringLiteral("Port")).toInt(), 3);
    EXPECT_FALSE(back[2].params.value(QStringLiteral("State")).toBool());
}

TEST(ProgramMapperTest, WaitTimeAndWaitDiRoundTrip) {
    ProgramCommand wait(CommandType::Logic, QStringLiteral("WAIT"), QStringLiteral("settle"));
    wait.params[QStringLiteral("Time")] = 2.5;

    ProgramCommand waitDi(CommandType::IO, QStringLiteral("WAIT DI"), QStringLiteral("sensor"));
    waitDi.params[QStringLiteral("Port")] = 4;
    waitDi.params[QStringLiteral("State")] = true;

    const auto back = roundTrip({wait, waitDi});
    ASSERT_EQ(back.size(), 2);
    EXPECT_EQ(back[0].code, QStringLiteral("WAIT"));
    EXPECT_DOUBLE_EQ(back[0].params.value(QStringLiteral("Time")).toDouble(), 2.5);
    EXPECT_EQ(back[1].code, QStringLiteral("WAIT DI"));
    EXPECT_EQ(back[1].params.value(QStringLiteral("Port")).toInt(), 4);
    EXPECT_TRUE(back[1].params.value(QStringLiteral("State")).toBool());
}

TEST(ProgramMapperTest, SetDoRoundTrip) {
    // SET DO (sequencer P3) carries its output assignment in the direct io_port/io_state wire fields
    // (not in .condition); dropping either would make the controller drive DO[0]=LOW instead of the
    // authored output.
    ProgramCommand setDo(CommandType::IO, QStringLiteral("SET DO"), QStringLiteral("gripper on"));
    setDo.params[QStringLiteral("Port")] = 7;
    setDo.params[QStringLiteral("State")] = true;

    const auto rdt = mapper::toRdt({setDo});
    ASSERT_EQ(rdt.steps.size(), 1u);
    EXPECT_EQ(rdt.steps[0].type, Net::StepType::SetDO);
    EXPECT_EQ(rdt.steps[0].io_port, 7);
    EXPECT_TRUE(rdt.steps[0].io_state);

    const auto back = mapper::toHmi(rdt);
    ASSERT_EQ(back.size(), 1);
    EXPECT_EQ(back[0].code, QStringLiteral("SET DO"));
    EXPECT_EQ(back[0].params.value(QStringLiteral("Port")).toInt(), 7);
    EXPECT_TRUE(back[0].params.value(QStringLiteral("State")).toBool());
    EXPECT_EQ(back[0].params.value(QStringLiteral("Condition")).toString(),
              QStringLiteral("DO[7]=HIGH"));
}

TEST(ProgramMapperTest, RegisterStepsAndBreakRoundTrip) {
    // P4: SET/INC/DEC VAR carry reg_index (+ reg_value for SET) on the wire; BREAK has no payload.
    ProgramCommand setVar(CommandType::Logic, QStringLiteral("SET VAR"), QStringLiteral("init"));
    setVar.params[QStringLiteral("Reg")] = 2;
    setVar.params[QStringLiteral("Value")] = 5;

    ProgramCommand incVar(CommandType::Logic, QStringLiteral("INC VAR"), QStringLiteral("count up"));
    incVar.params[QStringLiteral("Reg")] = 3;

    ProgramCommand decVar(CommandType::Logic, QStringLiteral("DEC VAR"), QStringLiteral("count down"));
    decVar.params[QStringLiteral("Reg")] = 2;

    ProgramCommand brk(CommandType::Logic, QStringLiteral("BREAK"), QStringLiteral("BREAK"));

    const auto rdt = mapper::toRdt({setVar, incVar, decVar, brk});
    ASSERT_EQ(rdt.steps.size(), 4u);
    EXPECT_EQ(rdt.steps[0].type, Net::StepType::SetVar);
    EXPECT_EQ(rdt.steps[0].reg_index, 2);
    EXPECT_EQ(rdt.steps[0].reg_value, 5);
    EXPECT_EQ(rdt.steps[1].type, Net::StepType::IncVar);
    EXPECT_EQ(rdt.steps[1].reg_index, 3);
    EXPECT_EQ(rdt.steps[2].type, Net::StepType::DecVar);
    EXPECT_EQ(rdt.steps[2].reg_index, 2);
    EXPECT_EQ(rdt.steps[3].type, Net::StepType::Break);

    const auto back = mapper::toHmi(rdt);
    ASSERT_EQ(back.size(), 4);
    EXPECT_EQ(back[0].code, QStringLiteral("SET VAR"));
    EXPECT_EQ(back[0].params.value(QStringLiteral("Reg")).toInt(), 2);
    EXPECT_EQ(back[0].params.value(QStringLiteral("Value")).toInt(), 5);
    EXPECT_EQ(back[1].code, QStringLiteral("INC VAR"));
    EXPECT_EQ(back[1].params.value(QStringLiteral("Reg")).toInt(), 3);
    EXPECT_EQ(back[2].code, QStringLiteral("DEC VAR"));
    EXPECT_EQ(back[2].params.value(QStringLiteral("Reg")).toInt(), 2);
    EXPECT_EQ(back[3].code, QStringLiteral("BREAK"));
}

TEST(ProgramMapperTest, RegisterConditionIfRoundTrip) {
    // P4: a register-sourced IF carries {source=REG, register_index, op, operand} in .condition —
    // distinct from the DI pair. Dropping the source would turn "IF R[1]>0" into a DI-port-0 test.
    ProgramCommand regIf(CommandType::Logic, QStringLiteral("IF"), QStringLiteral("loop check"));
    regIf.params[QStringLiteral("LabelId")] = 4;
    regIf.params[QStringLiteral("CondSource")] = QStringLiteral("REG");
    regIf.params[QStringLiteral("Reg")] = 1;
    regIf.params[QStringLiteral("Op")] = QStringLiteral("GT");
    regIf.params[QStringLiteral("Operand")] = 0;

    ProgramCommand label(CommandType::Logic, QStringLiteral("LABEL"), QStringLiteral("loop"));
    label.params[QStringLiteral("LabelId")] = 4;

    const auto rdt = mapper::toRdt({label, regIf});
    ASSERT_EQ(rdt.steps.size(), 2u);
    EXPECT_EQ(rdt.steps[1].type, Net::StepType::ConditionalJump);
    EXPECT_EQ(rdt.steps[1].condition.source, Net::ConditionSource::Register);
    EXPECT_EQ(rdt.steps[1].condition.register_index, 1);
    EXPECT_EQ(rdt.steps[1].condition.op, Net::CompareOp::GreaterThan);
    EXPECT_EQ(rdt.steps[1].condition.operand, 0);
    EXPECT_EQ(rdt.steps[1].jump_target_id, 4u);

    const auto back = mapper::toHmi(rdt);
    ASSERT_EQ(back.size(), 2);
    EXPECT_EQ(back[1].code, QStringLiteral("IF"));
    EXPECT_EQ(back[1].params.value(QStringLiteral("CondSource")).toString(), QStringLiteral("REG"));
    EXPECT_EQ(back[1].params.value(QStringLiteral("Reg")).toInt(), 1);
    EXPECT_EQ(back[1].params.value(QStringLiteral("Op")).toString(), QStringLiteral("GT"));
    EXPECT_EQ(back[1].params.value(QStringLiteral("Operand")).toInt(), 0);
    EXPECT_EQ(back[1].params.value(QStringLiteral("Condition")).toString(), QStringLiteral("R[1]>0"));
}

// ProgramCommand JSON round-trip tests were removed with ProgramCommand::toJson/fromJson: the
// pendant-local program file store (ProgramStorage, WORKSTATION tab) was removed per boss directive
// 2026-07-06 — programs persist on the controller only, carried over the wire as ProgramDataStruct
// (program_mapper::toRdt/toHmi, covered above). No production path serializes ProgramCommand to JSON.
