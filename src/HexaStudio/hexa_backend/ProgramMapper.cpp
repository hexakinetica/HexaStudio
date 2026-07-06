// --- START OF FILE: HexaStudio/hexa_backend/ProgramMapper.cpp ---
#include "ProgramMapper.h"

#include "DataTypes.h"   // RDT::AxisSet, RDT::CartPose, unit literals

#include <QLatin1String>
#include <QRegularExpression>
#include <QString>

#include <algorithm>
#include <cmath>

namespace hexa::program_mapper {

namespace {
// Program authoring parameter keys — the canonical set owned by ProgramBuilder (ProgramBuilder.h).
// Duplicated here as literals (not linked) so the mapper has no dependency on the editor module; they
// must stay in lockstep with ProgramBuilder's k* constants.
const QLatin1String kParamSpeed("Speed");
const QLatin1String kParamZone("Zone");
const QLatin1String kParamJoints("Joints");
const QLatin1String kParamTcp("TcpPose");
const QLatin1String kParamTcpVia("TcpViaPose");
const QLatin1String kParamToolId("ToolId");
const QLatin1String kParamBaseId("BaseId");
const QLatin1String kParamTime("Time");
const QLatin1String kParamLabelId("LabelId");
const QLatin1String kParamPort("Port");
const QLatin1String kParamState("State");
const QLatin1String kParamCondition("Condition");
// P4 register params (lockstep with ProgramBuilder::kReg/kValue/kCondSource/kCompareOp/kOperand).
const QLatin1String kParamReg("Reg");
const QLatin1String kParamValue("Value");
const QLatin1String kParamCondSource("CondSource"); // "DI" (default) | "REG"
const QLatin1String kParamCompareOp("Op");          // "EQ" | "NE" | "GT" | "LT"
const QLatin1String kParamOperand("Operand");

// CartPose <-> 6-vector [x,y,z (mm), rx,ry,rz (deg)] converters (pure).
QVector<double> cartPoseToVector(const RDT::CartPose& pose) {
    QVector<double> result;
    result.reserve(6);
    result.append(pose.x.value());
    result.append(pose.y.value());
    result.append(pose.z.value());
    result.append(pose.rx.value());
    result.append(pose.ry.value());
    result.append(pose.rz.value());
    return result;
}

RDT::CartPose qVectorToCartPose(const QVector<double>& v) {
    RDT::CartPose pose{};
    if (v.size() > 0) pose.x = RDT::Millimeters(v[0]);
    if (v.size() > 1) pose.y = RDT::Millimeters(v[1]);
    if (v.size() > 2) pose.z = RDT::Millimeters(v[2]);
    if (v.size() > 3) pose.rx = RDT::Degrees(v[3]);
    if (v.size() > 4) pose.ry = RDT::Degrees(v[4]);
    if (v.size() > 5) pose.rz = RDT::Degrees(v[5]);
    return pose;
}

// Zone label (FINE / APPROX Nmm) <-> backend blend radius in millimeters. Unknown/empty -> FINE (0mm)
// so motion stays exact and safe by default.
double zoneLabelToRadiusMm(const QString& zone) {
    const QString z = zone.trimmed();
    if (z.isEmpty() || z.compare("FINE", Qt::CaseInsensitive) == 0) {
        return 0.0;
    }
    static const QRegularExpression number_re(QStringLiteral("([0-9]+(?:\\.[0-9]+)?)"));
    const QRegularExpressionMatch m = number_re.match(z);
    if (m.hasMatch()) {
        bool ok = false;
        const double v = m.captured(1).toDouble(&ok);
        if (ok && v > 0.0) {
            return v;
        }
    }
    return 0.0;
}

QString radiusMmToZoneLabel(double radius_mm) {
    if (radius_mm <= 0.0) {
        return QStringLiteral("FINE");
    }
    static const int kZones[] = {1, 5, 10, 25, 50, 100};
    int best = kZones[0];
    double best_err = std::abs(radius_mm - kZones[0]);
    for (int z : kZones) {
        const double err = std::abs(radius_mm - z);
        if (err < best_err) {
            best_err = err;
            best = z;
        }
    }
    return QStringLiteral("APPROX %1mm").arg(best);
}

// Human-readable mirror of a DI condition, kept in sync with ProgramBuilder::conditionLabel so a
// controller-loaded WAIT DI/IF renders identically to a locally authored one.
QString diConditionLabel(uint16_t io_port, bool triggerState) {
    return QStringLiteral("DI[%1]=%2").arg(io_port).arg(triggerState ? QStringLiteral("HIGH")
                                                                      : QStringLiteral("LOW"));
}

// Human-readable mirror of a SET DO output assignment, kept in sync with ProgramBuilder::outputLabel.
QString doOutputLabel(uint16_t io_port, bool state) {
    return QStringLiteral("DO[%1]=%2").arg(io_port).arg(state ? QStringLiteral("HIGH")
                                                              : QStringLiteral("LOW"));
}

// P4: compare-operator token ("EQ"/"NE"/"GT"/"LT") <-> wire enum. The token set is the single
// authoring vocabulary (ProgramBuilder validates it); an unknown token maps to Equal, which the
// builder-side validation makes unreachable in practice.
RDT::NetProtocol::CompareOp compareOpFromLabel(const QString& op) {
    if (op == QLatin1String("NE")) return RDT::NetProtocol::CompareOp::NotEqual;
    if (op == QLatin1String("GT")) return RDT::NetProtocol::CompareOp::GreaterThan;
    if (op == QLatin1String("LT")) return RDT::NetProtocol::CompareOp::LessThan;
    return RDT::NetProtocol::CompareOp::Equal;
}

QString compareOpToLabel(RDT::NetProtocol::CompareOp op) {
    switch (op) {
        case RDT::NetProtocol::CompareOp::NotEqual:    return QStringLiteral("NE");
        case RDT::NetProtocol::CompareOp::GreaterThan: return QStringLiteral("GT");
        case RDT::NetProtocol::CompareOp::LessThan:    return QStringLiteral("LT");
        case RDT::NetProtocol::CompareOp::Equal:       break;
    }
    return QStringLiteral("EQ");
}

// Human-readable mirror of a register condition ("R[3]>0"), kept in sync with
// ProgramBuilder::registerConditionLabel.
QString regConditionLabel(uint16_t reg, RDT::NetProtocol::CompareOp op, int32_t operand) {
    QString symbol;
    switch (op) {
        case RDT::NetProtocol::CompareOp::Equal:       symbol = QStringLiteral("=");  break;
        case RDT::NetProtocol::CompareOp::NotEqual:    symbol = QStringLiteral("!="); break;
        case RDT::NetProtocol::CompareOp::GreaterThan: symbol = QStringLiteral(">");  break;
        case RDT::NetProtocol::CompareOp::LessThan:    symbol = QStringLiteral("<");  break;
    }
    return QStringLiteral("R[%1]%2%3").arg(reg).arg(symbol).arg(operand);
}
} // namespace

RDT::NetProtocol::ProgramDataStruct toRdt(const QVector<ProgramCommand>& prog) {
    RDT::NetProtocol::ProgramDataStruct rdt_prog;
    rdt_prog.name = "SyncedProgram";

    uint32_t id_counter = 0;
    for (const auto& cmd : prog) {
        RDT::NetProtocol::ProgramStepStruct step;
        step.id = id_counter++;

        if (cmd.type == CommandType::Motion) {
            if (cmd.code == "PTP" || cmd.code == "MoveJ") step.type = RDT::NetProtocol::StepType::MoveJ;
            else if (cmd.code == "LIN" || cmd.code == "MoveL") step.type = RDT::NetProtocol::StepType::MoveL;
            else if (cmd.code == "CIRC" || cmd.code == "MoveC") step.type = RDT::NetProtocol::StepType::MoveC;
            else if (cmd.code == "SPLINE" || cmd.code == "MoveS") step.type = RDT::NetProtocol::StepType::MoveS;
            else step.type = RDT::NetProtocol::StepType::MoveJ;
        } else if (cmd.type == CommandType::IO) {
            if (cmd.code.contains("WAIT DI")) step.type = RDT::NetProtocol::StepType::WaitDI;
            else if (cmd.code.contains("SET DO")) step.type = RDT::NetProtocol::StepType::SetDO;
        } else if (cmd.type == CommandType::Logic) {
            if (cmd.code.contains("WAIT")) step.type = RDT::NetProtocol::StepType::WaitTime;
            else if (cmd.code.contains("IF")) step.type = RDT::NetProtocol::StepType::ConditionalJump;
            else if (cmd.code == "GOTO") step.type = RDT::NetProtocol::StepType::JumpToLabel;
            else if (cmd.code == "LABEL") step.type = RDT::NetProtocol::StepType::Label;
            else if (cmd.code == "SET VAR") step.type = RDT::NetProtocol::StepType::SetVar;
            else if (cmd.code == "INC VAR") step.type = RDT::NetProtocol::StepType::IncVar;
            else if (cmd.code == "DEC VAR") step.type = RDT::NetProtocol::StepType::DecVar;
            else if (cmd.code == "BREAK") step.type = RDT::NetProtocol::StepType::Break;
        } else {
            step.type = RDT::NetProtocol::StepType::Comment;
        }

        step.comment = cmd.name.toStdString();
        step.speed_ratio = cmd.params.value(kParamSpeed, 100.0).toDouble();
        step.blending_radius_mm =
            RDT::Millimeters(zoneLabelToRadiusMm(cmd.params.value(kParamZone, "FINE").toString()));

        if (cmd.params.contains(kParamJoints)) {
            const QVector<double> vec = cmd.params[kParamJoints].value<QVector<double>>();
            for (int i = 0; i < qMin(6, static_cast<int>(vec.size())); ++i) {
                step.joint_target[static_cast<RDT::AxisId>(i)].position = RDT::Degrees(vec[i]);
            }
        }
        if (cmd.params.contains(kParamTcp)) {
            step.cartesian_target = qVectorToCartPose(cmd.params[kParamTcp].value<QVector<double>>());
        }
        // MoveC auxiliary point. The mapper is non-validating (same policy as the other motion
        // params): a CIRC without a taught via keeps the zero default, which the sequencer/planner
        // rejects with a typed InvalidPathGeometry fault; the editor gates authoring upstream.
        if (step.type == RDT::NetProtocol::StepType::MoveC && cmd.params.contains(kParamTcpVia)) {
            step.cartesian_via = qVectorToCartPose(cmd.params[kParamTcpVia].value<QVector<double>>());
        }
        if (step.type == RDT::NetProtocol::StepType::WaitTime) {
            step.wait_duration_s = RDT::Seconds(cmd.params.value(kParamTime, 0.0).toDouble());
        }

        // Execution context the shipping stub dropped: the active Tool/Base for a motion step.
        step.tool_id = static_cast<uint32_t>(cmd.params.value(kParamToolId, 0).toInt());
        step.base_id = static_cast<uint32_t>(cmd.params.value(kParamBaseId, 0).toInt());

        // Flow-control linkage: a Label carries the user label id as its step id (the jump table key);
        // a GOTO/IF carries the target label id as jump_target_id. The sequencer resolves jumps against
        // Label step ids and fails closed on an unresolved target.
        if (step.type == RDT::NetProtocol::StepType::Label) {
            if (cmd.params.contains(kParamLabelId)) {
                step.id = static_cast<uint32_t>(cmd.params.value(kParamLabelId, 0).toInt());
            }
        } else if (step.type == RDT::NetProtocol::StepType::JumpToLabel ||
                   step.type == RDT::NetProtocol::StepType::ConditionalJump) {
            step.jump_target_id = static_cast<uint32_t>(cmd.params.value(kParamLabelId, 0).toInt());
        }

        // DI conditions: WAIT DI blocks until DI[port]==state; IF (ConditionalJump) branches on it.
        // SET DO carries its output assignment in the direct io_port/io_state fields (not in
        // .condition) — that is what the sequencer forwards to the controller's HAL write.
        // A register-sourced IF (P4) carries {source=REG, register_index, op, operand} instead of
        // the DI pair; WAIT DI is always DI-sourced (the sequencer refuses a register wait).
        if (step.type == RDT::NetProtocol::StepType::WaitDI ||
            step.type == RDT::NetProtocol::StepType::ConditionalJump) {
            if (step.type == RDT::NetProtocol::StepType::ConditionalJump &&
                cmd.params.value(kParamCondSource).toString() == QLatin1String("REG")) {
                step.condition.source = RDT::NetProtocol::ConditionSource::Register;
                step.condition.register_index =
                    static_cast<uint16_t>(cmd.params.value(kParamReg, 0).toInt());
                step.condition.op = compareOpFromLabel(cmd.params.value(kParamCompareOp).toString());
                step.condition.operand = cmd.params.value(kParamOperand, 0).toInt();
            } else {
                step.condition.io_port = static_cast<uint16_t>(cmd.params.value(kParamPort, 0).toInt());
                step.condition.trigger_on_state = cmd.params.value(kParamState, true).toBool();
            }
        } else if (step.type == RDT::NetProtocol::StepType::SetDO) {
            step.io_port = static_cast<uint16_t>(cmd.params.value(kParamPort, 0).toInt());
            step.io_state = cmd.params.value(kParamState, false).toBool();
        } else if (step.type == RDT::NetProtocol::StepType::SetVar) {
            step.reg_index = static_cast<uint16_t>(cmd.params.value(kParamReg, 0).toInt());
            step.reg_value = cmd.params.value(kParamValue, 0).toInt();
        } else if (step.type == RDT::NetProtocol::StepType::IncVar ||
                   step.type == RDT::NetProtocol::StepType::DecVar) {
            step.reg_index = static_cast<uint16_t>(cmd.params.value(kParamReg, 0).toInt());
        }

        rdt_prog.steps.push_back(step);
    }
    return rdt_prog;
}

QVector<ProgramCommand> toHmi(const RDT::NetProtocol::ProgramDataStruct& rdt_prog) {
    QVector<ProgramCommand> result;
    for (const auto& step : rdt_prog.steps) {
        CommandType type = CommandType::Comment;
        QString code = "#";
        const QString name = QString::fromStdString(step.comment);
        QVariantMap params;

        switch (step.type) {
            case RDT::NetProtocol::StepType::MoveJ: type = CommandType::Motion; code = "PTP"; break;
            case RDT::NetProtocol::StepType::MoveL: type = CommandType::Motion; code = "LIN"; break;
            case RDT::NetProtocol::StepType::MoveC:
                type = CommandType::Motion; code = "CIRC";
                params[kParamTcpVia] = QVariant::fromValue(cartPoseToVector(step.cartesian_via));
                break;
            case RDT::NetProtocol::StepType::MoveS: type = CommandType::Motion; code = "SPLINE"; break;
            case RDT::NetProtocol::StepType::WaitDI:
                type = CommandType::IO; code = "WAIT DI";
                params[kParamPort] = static_cast<int>(step.condition.io_port);
                params[kParamState] = step.condition.trigger_on_state;
                params[kParamCondition] = diConditionLabel(step.condition.io_port, step.condition.trigger_on_state);
                break;
            case RDT::NetProtocol::StepType::SetDO:
                type = CommandType::IO; code = "SET DO";
                params[kParamPort] = static_cast<int>(step.io_port);
                params[kParamState] = step.io_state;
                params[kParamCondition] = doOutputLabel(step.io_port, step.io_state);
                break;
            case RDT::NetProtocol::StepType::WaitTime:
                type = CommandType::Logic; code = "WAIT";
                params[kParamTime] = step.wait_duration_s.value();
                params["Subtype"] = "WAIT";
                break;
            case RDT::NetProtocol::StepType::ConditionalJump:
                type = CommandType::Logic; code = "IF";
                params["Subtype"] = "IF";
                params[kParamLabelId] = static_cast<int>(step.jump_target_id);
                if (step.condition.source == RDT::NetProtocol::ConditionSource::Register) {
                    params[kParamCondSource] = QStringLiteral("REG");
                    params[kParamReg] = static_cast<int>(step.condition.register_index);
                    params[kParamCompareOp] = compareOpToLabel(step.condition.op);
                    params[kParamOperand] = static_cast<int>(step.condition.operand);
                    params[kParamCondition] = regConditionLabel(step.condition.register_index,
                                                                step.condition.op,
                                                                step.condition.operand);
                } else {
                    params[kParamPort] = static_cast<int>(step.condition.io_port);
                    params[kParamState] = step.condition.trigger_on_state;
                    params[kParamCondition] = diConditionLabel(step.condition.io_port,
                                                               step.condition.trigger_on_state);
                }
                break;
            case RDT::NetProtocol::StepType::JumpToLabel:
                type = CommandType::Logic; code = "GOTO";
                params[kParamLabelId] = static_cast<int>(step.jump_target_id);
                break;
            case RDT::NetProtocol::StepType::Label:
                type = CommandType::Logic; code = "LABEL";
                params[kParamLabelId] = static_cast<int>(step.id);
                break;
            case RDT::NetProtocol::StepType::SetVar:
                type = CommandType::Logic; code = "SET VAR";
                params[kParamReg] = static_cast<int>(step.reg_index);
                params[kParamValue] = static_cast<int>(step.reg_value);
                break;
            case RDT::NetProtocol::StepType::IncVar:
                type = CommandType::Logic; code = "INC VAR";
                params[kParamReg] = static_cast<int>(step.reg_index);
                break;
            case RDT::NetProtocol::StepType::DecVar:
                type = CommandType::Logic; code = "DEC VAR";
                params[kParamReg] = static_cast<int>(step.reg_index);
                break;
            case RDT::NetProtocol::StepType::Break:
                type = CommandType::Logic; code = "BREAK";
                break;
            case RDT::NetProtocol::StepType::Comment:
            default: type = CommandType::Comment; code = "#"; break;
        }

        if (type == CommandType::Motion) {
            params[kParamSpeed] = static_cast<int>(step.speed_ratio);
            QVector<double> joints;
            for (int i = 0; i < 6; ++i) {
                joints.append(step.joint_target[static_cast<RDT::AxisId>(i)].position.value());
            }
            params[kParamJoints] = QVariant::fromValue(joints);
            params[kParamTcp] = QVariant::fromValue(cartPoseToVector(step.cartesian_target));
            params[kParamZone] = radiusMmToZoneLabel(step.blending_radius_mm.value());
            params[kParamToolId] = static_cast<int>(step.tool_id);
            params[kParamBaseId] = static_cast<int>(step.base_id);
        }

        result.append(ProgramCommand(type, code, name, params));
    }
    return result;
}

} // namespace hexa::program_mapper
// --- END OF FILE: HexaStudio/hexa_backend/ProgramMapper.cpp ---
