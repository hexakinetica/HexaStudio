// --- START OF FILE: HexaStudio/program_editor/ProgramBuilder.cpp ---
#include "ProgramBuilder.h"

#include <QUuid>
#include <QtGlobal>
#include <array>
#include <cmath>

namespace hexa {

namespace {
// Zones offered by the editor; must match PanelLeft's Zone selector and RobotService mapping.
constexpr std::array<const char*, 7> kKnownZones = {
    "FINE", "APPROX 1mm", "APPROX 5mm", "APPROX 10mm", "APPROX 25mm", "APPROX 50mm", "APPROX 100mm"};

// Default corner zone stamped at teach time depending on the BLEND toggle.
constexpr const char* kBlendOnZone  = "APPROX 50mm";
constexpr const char* kBlendOffZone = "FINE";

// A DI port is valid when it is within the controller-enforced 1-based range.
bool portInRange(int port) {
    return port >= ProgramBuilder::kMinDigitalInputPort && port <= ProgramBuilder::kMaxDigitalInputPort;
}

// Human-readable mirror of a DI condition, stored under kCondition for display on the EDIT pages.
QString conditionLabel(int port, bool triggerState) {
    return QStringLiteral("DI[%1]=%2").arg(port).arg(triggerState ? QStringLiteral("HIGH")
                                                                   : QStringLiteral("LOW"));
}

// Human-readable mirror of a SET DO output assignment, stored under kCondition so the shared IO
// edit page renders it on the same button as a WAIT DI condition.
QString outputLabel(int port, bool state) {
    return QStringLiteral("DO[%1]=%2").arg(port).arg(state ? QStringLiteral("HIGH")
                                                           : QStringLiteral("LOW"));
}

// P4 registers: index/operator validation and the human-readable register-condition mirror
// ("R[3]>0"), kept in sync with the hexa_backend mapper's regConditionLabel.
bool registerInRange(int reg) {
    return reg >= ProgramBuilder::kMinRegisterIndex && reg <= ProgramBuilder::kMaxRegisterIndex;
}

bool compareOpValid(const QString& op) {
    return op == QLatin1String("EQ") || op == QLatin1String("NE") ||
           op == QLatin1String("GT") || op == QLatin1String("LT");
}

QString compareOpSymbol(const QString& op) {
    if (op == QLatin1String("NE")) return QStringLiteral("!=");
    if (op == QLatin1String("GT")) return QStringLiteral(">");
    if (op == QLatin1String("LT")) return QStringLiteral("<");
    return QStringLiteral("=");
}

QString registerConditionLabel(int reg, const QString& op, int operand) {
    return QStringLiteral("R[%1]%2%3").arg(reg).arg(compareOpSymbol(op)).arg(operand);
}
} // namespace

QString toString(ProgramError error) {
    switch (error) {
        case ProgramError::IndexOutOfRange:            return QStringLiteral("step index out of range");
        case ProgramError::EmptyProgram:               return QStringLiteral("program is empty");
        case ProgramError::MotionPointMissingPose:     return QStringLiteral("motion point has no taught pose (no joints and no TCP pose)");
        case ProgramError::SpeedOutOfRange:            return QStringLiteral("speed out of range 1..100 %");
        case ProgramError::ZoneInvalid:                return QStringLiteral("unknown corner zone label");
        case ProgramError::AxisOutOfRange:             return QStringLiteral("pose component index out of range 0..5");
        case ProgramError::PoseNotEditableForJointMove:return QStringLiteral("TCP pose is editable only for LIN (cartesian-dominant) moves");
        case ProgramError::NotAMotionStep:             return QStringLiteral("operation requires a motion step");
        case ProgramError::NotAWaitStep:               return QStringLiteral("operation requires a WAIT step");
        case ProgramError::NotALabelStep:              return QStringLiteral("operation requires a GOTO/LABEL step");
        case ProgramError::NotAConditionStep:          return QStringLiteral("operation requires a WAIT DI/IF step");
        case ProgramError::NotACommentStep:            return QStringLiteral("operation requires a comment step");
        case ProgramError::NegativeTime:               return QStringLiteral("wait time must be >= 0");
        case ProgramError::NegativeLabelId:            return QStringLiteral("label id must be >= 0");
        case ProgramError::DuplicateLabelId:           return QStringLiteral("two LABEL steps share the same id");
        case ProgramError::UnresolvedJumpTarget:       return QStringLiteral("GOTO/IF targets a label id that no LABEL defines");
        case ProgramError::IoPortOutOfRange:           return QStringLiteral("DI port out of range 1..32");
        case ProgramError::NothingToCopy:              return QStringLiteral("copy buffer is empty");
        case ProgramError::UndoStackEmpty:             return QStringLiteral("nothing to undo");
        case ProgramError::RedoStackEmpty:             return QStringLiteral("nothing to redo");
        case ProgramError::RegisterIndexOutOfRange:    return QStringLiteral("register index out of range R[0..15]");
        case ProgramError::InvalidCompareOp:           return QStringLiteral("compare operator must be EQ, NE, GT or LT");
        case ProgramError::NotACircStep:               return QStringLiteral("TEACH VIA requires a CIRC motion step");
        case ProgramError::ViaPointMissing:            return QStringLiteral("CIRC step has no taught via point (use TEACH VIA)");
        case ProgramError::ViaPointCoincidesWithTarget:return QStringLiteral("CIRC via point coincides with the target (no unique arc)");
        case ProgramError::SplinePointsCoincide:       return QStringLiteral("consecutive SPLINE points coincide (no valid curve)");
        case ProgramError::SplineBlockSpeedMismatch:   return QStringLiteral("SPLINE block points have differing speeds (block runs at the first point's speed)");
        case ProgramError::SplineZoneIgnored:          return QStringLiteral("zone is ignored on SPLINE points (the curve is already smooth)");
    }
    return QStringLiteral("unknown error");
}

ProgramBuilder::ProgramBuilder(QObject* parent) : QObject(parent) {
    // Register the QVariant payload types the program steps carry, mirroring PanelLeft/main so the
    // builder works identically inside the app, the standalone bench and the unit tests.
    qRegisterMetaType<QVector<double>>("QVector<double>");
    qRegisterMetaType<QVector<ProgramCommand>>("QVector<ProgramCommand>");
}

bool ProgramBuilder::isMotionCode(const QString& code) {
    return code == "PTP" || code == "LIN" || code == "CIRC" || code == "SPLINE"
        || code == "MoveJ" || code == "MoveL" || code == "MoveC" || code == "MoveS";
}

bool ProgramBuilder::isCartesianDominant(const QString& code) {
    // LIN, CIRC and SPLINE are executed by the controller from cartesian_target (IK per point), so
    // their TCP pose is the authoritative edit surface; PTP executes joint_target.
    return code == "LIN" || code == "MoveL" || code == "CIRC" || code == "MoveC"
        || code == "SPLINE" || code == "MoveS";
}

bool ProgramBuilder::isCircCode(const QString& code) {
    return code == "CIRC" || code == "MoveC";
}

bool ProgramBuilder::isSplineCode(const QString& code) {
    return code == "SPLINE" || code == "MoveS";
}

bool ProgramBuilder::isKnownZone(const QString& zone) {
    for (const char* z : kKnownZones) {
        if (zone == QLatin1String(z)) {
            return true;
        }
    }
    return false;
}

bool ProgramBuilder::isLabelCode(const QString& code) {
    return code == QLatin1String("GOTO") || code == QLatin1String("LABEL");
}

bool ProgramBuilder::isLogicBlockCode(const QString& code) {
    return code == QLatin1String("IF") || code == QLatin1String("ELSE")
        || code == QLatin1String("END_IF") || code == QLatin1String("WAIT");
}

const ProgramCommand& ProgramBuilder::at(int index) const {
    static const ProgramCommand kEmpty;
    if (!indexValid(index)) {
        // Out-of-range access is a caller bug; return a safe empty command but make it diagnosable
        // rather than silently masking the bad index.
        qWarning("ProgramBuilder::at: index %d out of range [0, %lld); returning empty command.",
                 index, static_cast<long long>(m_program.size()));
        return kEmpty;
    }
    return m_program.at(index);
}

int ProgramBuilder::resolveInsertIndex(int afterIndex) const {
    if (afterIndex < 0) {
        return m_program.size();
    }
    return qBound(0, afterIndex + 1, m_program.size());
}

void ProgramBuilder::pushUndoSnapshot() {
    m_undo.append(m_program);
    if (m_undo.size() > kMaxUndoDepth) {
        m_undo.removeFirst();
    }
    m_redo.clear();
}

void ProgramBuilder::setModified(bool modified) {
    if (m_modified == modified) {
        return;
    }
    m_modified = modified;
    emit modifiedChanged(m_modified);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

RDT::Result<int, ProgramError> ProgramBuilder::addMotionPoint(MotionKind kind,
                                                              const QVector<double>& joints,
                                                              const QVector<double>& tcpPose,
                                                              int toolId, int baseId, bool blend,
                                                              int afterIndex) {
    // A motion point with neither joints nor a TCP pose has nothing to execute; reject it at teach time
    // instead of storing an unusable step that validate() would later flag.
    if (joints.isEmpty() && tcpPose.isEmpty()) {
        return ProgramError::MotionPointMissingPose;
    }
    // CIRC arcs and SPLINE curves are planned from the Cartesian target, so joints alone are not
    // enough for them.
    if ((kind == MotionKind::CIRC || kind == MotionKind::SPLINE) && tcpPose.isEmpty()) {
        return ProgramError::MotionPointMissingPose;
    }
    QString code;
    switch (kind) {
        case MotionKind::PTP:    code = QStringLiteral("PTP");    break;
        case MotionKind::LIN:    code = QStringLiteral("LIN");    break;
        case MotionKind::CIRC:   code = QStringLiteral("CIRC");   break;
        case MotionKind::SPLINE: code = QStringLiteral("SPLINE"); break;
    }
    const QString name = QStringLiteral("Point %1").arg(m_program.size() + 1);

    QVariantMap params;
    params[kSpeed]  = kMaxSpeedPercent;
    params[kZone]   = blend ? QLatin1String(kBlendOnZone) : QLatin1String(kBlendOffZone);
    params[kJoints] = QVariant::fromValue(joints);
    params[kTcp]    = QVariant::fromValue(tcpPose);
    params[kToolId] = toolId;
    params[kBaseId] = baseId;

    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::Motion, code, name, params));
    setModified(true);
    emit stepInserted(index);
    return index;
}

RDT::Result<int, ProgramError> ProgramBuilder::addWait(double seconds, int afterIndex) {
    if (seconds < 0.0) {
        return ProgramError::NegativeTime;
    }
    QVariantMap params;
    params[kTime] = seconds;
    params["Subtype"] = QStringLiteral("WAIT");

    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::Logic, QStringLiteral("WAIT"),
                                           QStringLiteral("WAIT"), params));
    setModified(true);
    emit stepInserted(index);
    return index;
}

int ProgramBuilder::addComment(const QString& text, int afterIndex) {
    const QString name = text.isEmpty() ? QStringLiteral("COMMENT") : text;
    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::Comment, QStringLiteral("#"), name));
    setModified(true);
    emit stepInserted(index);
    return index;
}

RDT::Result<int, ProgramError> ProgramBuilder::addLabel(int labelId, int afterIndex) {
    if (labelId < 0) {
        return ProgramError::NegativeLabelId;
    }
    QVariantMap params;
    params[kLabelId] = labelId;
    const QString name = QStringLiteral("LABEL %1").arg(labelId);

    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::Logic, QStringLiteral("LABEL"), name, params));
    setModified(true);
    emit stepInserted(index);
    return index;
}

RDT::Result<int, ProgramError> ProgramBuilder::addGoto(int labelId, int afterIndex) {
    if (labelId < 0) {
        return ProgramError::NegativeLabelId;
    }
    // A GOTO to a not-yet-defined label is allowed at insert time (the operator may add the LABEL
    // afterwards); the dangling target is reported by validate() and blocks RUN until resolved.
    QVariantMap params;
    params[kLabelId] = labelId;
    const QString name = QStringLiteral("GOTO %1").arg(labelId);

    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::Logic, QStringLiteral("GOTO"), name, params));
    setModified(true);
    emit stepInserted(index);
    return index;
}

RDT::Result<int, ProgramError> ProgramBuilder::addWaitDI(int port, bool triggerState, int afterIndex) {
    if (!portInRange(port)) {
        return ProgramError::IoPortOutOfRange;
    }
    QVariantMap params;
    params[kPort] = port;
    params[kState] = triggerState;
    params[kCondition] = conditionLabel(port, triggerState);   // display mirror
    const QString name = QStringLiteral("WAIT %1").arg(conditionLabel(port, triggerState));

    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::IO, QStringLiteral("WAIT DI"), name, params));
    setModified(true);
    emit stepInserted(index);
    return index;
}

RDT::Result<int, ProgramError> ProgramBuilder::addSetDo(int port, bool state, int afterIndex) {
    if (!portInRange(port)) {
        return ProgramError::IoPortOutOfRange;
    }
    QVariantMap params;
    params[kPort] = port;
    params[kState] = state;
    params[kCondition] = outputLabel(port, state);   // display mirror
    const QString name = QStringLiteral("SET %1").arg(outputLabel(port, state));

    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::IO, QStringLiteral("SET DO"), name, params));
    setModified(true);
    emit stepInserted(index);
    return index;
}

RDT::Result<int, ProgramError> ProgramBuilder::addSetVar(int reg, int value, int afterIndex) {
    if (!registerInRange(reg)) {
        return ProgramError::RegisterIndexOutOfRange;
    }
    QVariantMap params;
    params[kReg] = reg;
    params[kValue] = value;
    const QString name = QStringLiteral("SET R[%1]=%2").arg(reg).arg(value);

    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::Logic, QStringLiteral("SET VAR"), name, params));
    setModified(true);
    emit stepInserted(index);
    return index;
}

RDT::Result<int, ProgramError> ProgramBuilder::addIncVar(int reg, int afterIndex) {
    if (!registerInRange(reg)) {
        return ProgramError::RegisterIndexOutOfRange;
    }
    QVariantMap params;
    params[kReg] = reg;
    const QString name = QStringLiteral("INC R[%1]").arg(reg);

    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::Logic, QStringLiteral("INC VAR"), name, params));
    setModified(true);
    emit stepInserted(index);
    return index;
}

RDT::Result<int, ProgramError> ProgramBuilder::addDecVar(int reg, int afterIndex) {
    if (!registerInRange(reg)) {
        return ProgramError::RegisterIndexOutOfRange;
    }
    QVariantMap params;
    params[kReg] = reg;
    const QString name = QStringLiteral("DEC R[%1]").arg(reg);

    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::Logic, QStringLiteral("DEC VAR"), name, params));
    setModified(true);
    emit stepInserted(index);
    return index;
}

RDT::Result<int, ProgramError> ProgramBuilder::addConditionalJumpOnRegister(int reg, const QString& op,
                                                                            int operand, int targetLabelId,
                                                                            int afterIndex) {
    if (!registerInRange(reg)) {
        return ProgramError::RegisterIndexOutOfRange;
    }
    if (!compareOpValid(op)) {
        return ProgramError::InvalidCompareOp;
    }
    if (targetLabelId < 0) {
        return ProgramError::NegativeLabelId;
    }
    QVariantMap params;
    params[kSubtype] = QStringLiteral("IF");
    params[kLabelId] = targetLabelId;
    params[kCondSource] = QStringLiteral("REG");
    params[kReg] = reg;
    params[kCompareOp] = op;
    params[kOperand] = operand;
    params[kCondition] = registerConditionLabel(reg, op, operand);   // display mirror
    const QString name = QStringLiteral("IF %1 GOTO %2")
                             .arg(registerConditionLabel(reg, op, operand)).arg(targetLabelId);

    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::Logic, QStringLiteral("IF"), name, params));
    setModified(true);
    emit stepInserted(index);
    return index;
}

RDT::Result<int, ProgramError> ProgramBuilder::addBreak(int afterIndex) {
    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::Logic, QStringLiteral("BREAK"),
                                           QStringLiteral("BREAK"), QVariantMap{}));
    setModified(true);
    emit stepInserted(index);
    return index;
}

RDT::Result<int, ProgramError> ProgramBuilder::addConditionalJump(int port, bool triggerState,
                                                                  int targetLabelId, int afterIndex) {
    if (!portInRange(port)) {
        return ProgramError::IoPortOutOfRange;
    }
    if (targetLabelId < 0) {
        return ProgramError::NegativeLabelId;
    }
    // Like GOTO, an IF to a not-yet-defined label is allowed here and flagged by validate() (RUN gate).
    QVariantMap params;
    params[kPort] = port;
    params[kState] = triggerState;
    params[kLabelId] = targetLabelId;
    params[kSubtype] = QStringLiteral("IF");
    params[kCondition] = conditionLabel(port, triggerState);   // display mirror
    const QString name = QStringLiteral("IF %1 GOTO %2").arg(conditionLabel(port, triggerState)).arg(targetLabelId);

    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, ProgramCommand(CommandType::Logic, QStringLiteral("IF"), name, params));
    setModified(true);
    emit stepInserted(index);
    return index;
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

RDT::Result<void, ProgramError> ProgramBuilder::setSpeed(int index, int percent) {
    if (!indexValid(index)) {
        return ProgramError::IndexOutOfRange;
    }
    if (m_program[index].type != CommandType::Motion) {
        return ProgramError::NotAMotionStep;
    }
    if (percent < kMinSpeedPercent || percent > kMaxSpeedPercent) {
        return ProgramError::SpeedOutOfRange;
    }
    pushUndoSnapshot();
    m_program[index].params[kSpeed] = percent;
    setModified(true);
    emit stepChanged(index);
    return {};
}

RDT::Result<void, ProgramError> ProgramBuilder::setZone(int index, const QString& zone) {
    if (!indexValid(index)) {
        return ProgramError::IndexOutOfRange;
    }
    if (m_program[index].type != CommandType::Motion) {
        return ProgramError::NotAMotionStep;
    }
    if (!isKnownZone(zone)) {
        return ProgramError::ZoneInvalid;
    }
    pushUndoSnapshot();
    m_program[index].params[kZone] = zone;
    setModified(true);
    emit stepChanged(index);
    return {};
}

RDT::Result<void, ProgramError> ProgramBuilder::setTcpComponent(int index, int axis, double value) {
    if (!indexValid(index)) {
        return ProgramError::IndexOutOfRange;
    }
    // TCP-pose editing is authoritative only for cartesian-dominant moves (LIN): the controller plans
    // them from cartesian_target. A PTP/MoveJ point executes joint_target, so editing its pose would
    // not move the robot (silent failure) -> reject.
    if (!isCartesianDominant(m_program[index].code)) {
        return ProgramError::PoseNotEditableForJointMove;
    }
    if (axis < 0 || axis >= kPoseComponents) {
        return ProgramError::AxisOutOfRange;
    }
    QVector<double> pose = m_program[index].params.value(kTcp).value<QVector<double>>();
    if (pose.size() < kPoseComponents) {
        pose.resize(kPoseComponents); // pad missing components with 0 so axis is always valid
    }
    pushUndoSnapshot();
    pose[axis] = value;
    m_program[index].params[kTcp] = QVariant::fromValue(pose);
    setModified(true);
    emit stepChanged(index);
    return {};
}

RDT::Result<void, ProgramError> ProgramBuilder::touchUp(int index, const QVector<double>& joints,
                                                                    const QVector<double>& tcpPose) {
    if (!indexValid(index)) {
        return ProgramError::IndexOutOfRange;
    }
    if (m_program[index].type != CommandType::Motion) {
        return ProgramError::NotAMotionStep;
    }
    pushUndoSnapshot();
    m_program[index].params[kJoints] = QVariant::fromValue(joints);
    m_program[index].params[kTcp]    = QVariant::fromValue(tcpPose);
    setModified(true);
    emit stepChanged(index);
    return {};
}

RDT::Result<void, ProgramError> ProgramBuilder::teachVia(int index, const QVector<double>& tcpPose) {
    if (!indexValid(index)) {
        return ProgramError::IndexOutOfRange;
    }
    const ProgramCommand& cmd = m_program[index];
    if (cmd.type != CommandType::Motion || !isCircCode(cmd.code)) {
        return ProgramError::NotACircStep;
    }
    if (tcpPose.isEmpty()) {
        return ProgramError::MotionPointMissingPose;
    }
    pushUndoSnapshot();
    m_program[index].params[kTcpVia] = QVariant::fromValue(tcpPose);
    setModified(true);
    emit stepChanged(index);
    return {};
}

RDT::Result<void, ProgramError> ProgramBuilder::setWaitTime(int index, double seconds) {
    if (!indexValid(index)) {
        return ProgramError::IndexOutOfRange;
    }
    if (m_program[index].code != QStringLiteral("WAIT")) {
        return ProgramError::NotAWaitStep;
    }
    if (seconds < 0.0) {
        return ProgramError::NegativeTime;
    }
    pushUndoSnapshot();
    m_program[index].params[kTime] = seconds;
    setModified(true);
    emit stepChanged(index);
    return {};
}

RDT::Result<void, ProgramError> ProgramBuilder::setCommentText(int index, const QString& text) {
    if (!indexValid(index)) {
        return ProgramError::IndexOutOfRange;
    }
    if (m_program[index].type != CommandType::Comment) {
        return ProgramError::NotACommentStep;
    }
    // Mirror addComment: an empty entry falls back to the placeholder instead of failing - the
    // step stays visibly a comment either way.
    const QString trimmed = text.trimmed();
    pushUndoSnapshot();
    m_program[index].name = trimmed.isEmpty() ? QStringLiteral("COMMENT") : trimmed;
    setModified(true);
    emit stepChanged(index);
    return {};
}

RDT::Result<void, ProgramError> ProgramBuilder::setLabelId(int index, int labelId) {
    if (!indexValid(index)) {
        return ProgramError::IndexOutOfRange;
    }
    const QString& code = m_program[index].code;
    if (!isLabelCode(code)) {
        return ProgramError::NotALabelStep;
    }
    if (labelId < 0) {
        return ProgramError::NegativeLabelId;
    }
    pushUndoSnapshot();
    m_program[index].params[kLabelId] = labelId;
    setModified(true);
    emit stepChanged(index);
    return {};
}

RDT::Result<void, ProgramError> ProgramBuilder::setCondition(int index, int port, bool triggerState) {
    if (!indexValid(index)) {
        return ProgramError::IndexOutOfRange;
    }
    const QString code = m_program[index].code;
    const bool isWaitDi = (code == QLatin1String("WAIT DI"));
    const bool isIf = (code == QLatin1String("IF"));
    const bool isSetDo = (code == QLatin1String("SET DO"));
    if (!isWaitDi && !isIf && !isSetDo) {
        return ProgramError::NotAConditionStep;
    }
    // A register-sourced IF (P4) is edited through setRegisterCondition(); rewriting it here with a
    // DI pair would silently flip the condition source and leave stale register params behind.
    if (isIf && m_program[index].params.value(kCondSource).toString() == QLatin1String("REG")) {
        return ProgramError::NotAConditionStep;
    }
    if (!portInRange(port)) {
        return ProgramError::IoPortOutOfRange;
    }
    pushUndoSnapshot();
    ProgramCommand& cmd = m_program[index];
    cmd.params[kPort] = port;
    cmd.params[kState] = triggerState;
    cmd.params[kCondition] = isSetDo ? outputLabel(port, triggerState)
                                     : conditionLabel(port, triggerState);
    // Keep the row label in sync; the IF form preserves its existing jump target.
    if (isIf) {
        const int target = cmd.params.value(kLabelId, 0).toInt();
        cmd.name = QStringLiteral("IF %1 GOTO %2").arg(conditionLabel(port, triggerState)).arg(target);
    } else if (isSetDo) {
        cmd.name = QStringLiteral("SET %1").arg(outputLabel(port, triggerState));
    } else {
        cmd.name = QStringLiteral("WAIT %1").arg(conditionLabel(port, triggerState));
    }
    setModified(true);
    emit stepChanged(index);
    return {};
}

RDT::Result<void, ProgramError> ProgramBuilder::setRegisterCondition(int index, int reg,
                                                                     const QString& op, int operand) {
    if (!indexValid(index)) {
        return ProgramError::IndexOutOfRange;
    }
    // Only a register-sourced IF is editable here (the DI form is edited via setCondition; the
    // source of an existing step never flips silently — delete and re-insert to change it).
    if (m_program[index].code != QLatin1String("IF") ||
        m_program[index].params.value(kCondSource).toString() != QLatin1String("REG")) {
        return ProgramError::NotAConditionStep;
    }
    if (!registerInRange(reg)) {
        return ProgramError::RegisterIndexOutOfRange;
    }
    if (!compareOpValid(op)) {
        return ProgramError::InvalidCompareOp;
    }
    pushUndoSnapshot();
    ProgramCommand& cmd = m_program[index];
    cmd.params[kReg] = reg;
    cmd.params[kCompareOp] = op;
    cmd.params[kOperand] = operand;
    cmd.params[kCondition] = registerConditionLabel(reg, op, operand);
    const int target = cmd.params.value(kLabelId, 0).toInt();
    cmd.name = QStringLiteral("IF %1 GOTO %2").arg(registerConditionLabel(reg, op, operand)).arg(target);
    setModified(true);
    emit stepChanged(index);
    return {};
}

// ---------------------------------------------------------------------------
// Structural editing
// ---------------------------------------------------------------------------

RDT::Result<void, ProgramError> ProgramBuilder::remove(int index) {
    if (!indexValid(index)) {
        return ProgramError::IndexOutOfRange;
    }
    pushUndoSnapshot();
    m_program.removeAt(index);
    setModified(true);
    emit stepRemoved(index);
    return {};
}

RDT::Result<void, ProgramError> ProgramBuilder::move(int from, int to) {
    if (!indexValid(from) || !indexValid(to)) {
        return ProgramError::IndexOutOfRange;
    }
    if (from == to) {
        return {}; // no-op move is success, not an error
    }
    pushUndoSnapshot();
    m_program.move(from, to);
    setModified(true);
    emit stepMoved(from, to);
    return {};
}

RDT::Result<void, ProgramError> ProgramBuilder::copy(int index) {
    if (!indexValid(index)) {
        return ProgramError::IndexOutOfRange;
    }
    // Copy does not mutate the program: no snapshot, no modified flag, no signal.
    m_copyBuffer = m_program.at(index);
    m_hasCopy = true;
    return {};
}

RDT::Result<int, ProgramError> ProgramBuilder::paste(int afterIndex) {
    if (!m_hasCopy) {
        return ProgramError::NothingToCopy;
    }
    ProgramCommand pasted = m_copyBuffer;
    pasted.uuid = QUuid::createUuid(); // a pasted step is a new, distinct step
    const int index = resolveInsertIndex(afterIndex);
    pushUndoSnapshot();
    m_program.insert(index, pasted);
    setModified(true);
    emit stepInserted(index);
    return index;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

QVector<ProgramIssue> ProgramBuilder::validate() const {
    QVector<ProgramIssue> issues;

    if (m_program.isEmpty()) {
        issues.append({-1, IssueSeverity::Error, ProgramError::EmptyProgram,
                       QStringLiteral("Program is empty: nothing to run.")});
        return issues;
    }

    for (int i = 0; i < m_program.size(); ++i) {
        const ProgramCommand& cmd = m_program.at(i);
        if (cmd.type != CommandType::Motion) {
            continue;
        }

        const QVector<double> joints = cmd.params.value(kJoints).value<QVector<double>>();
        const QVector<double> pose   = cmd.params.value(kTcp).value<QVector<double>>();
        if (joints.isEmpty() && pose.isEmpty()) {
            issues.append({i, IssueSeverity::Error, ProgramError::MotionPointMissingPose,
                           QStringLiteral("Step %1 (%2): motion point has no taught pose.")
                               .arg(i).arg(cmd.code)});
        }

        const int speed = cmd.params.value(kSpeed, kMaxSpeedPercent).toInt();
        if (speed < kMinSpeedPercent || speed > kMaxSpeedPercent) {
            issues.append({i, IssueSeverity::Error, ProgramError::SpeedOutOfRange,
                           QStringLiteral("Step %1 (%2): speed %3%% out of range 1..100.")
                               .arg(i).arg(cmd.code).arg(speed)});
        }

        const QString zone = cmd.params.value(kZone, QStringLiteral("FINE")).toString();
        if (!isKnownZone(zone)) {
            issues.append({i, IssueSeverity::Warning, ProgramError::ZoneInvalid,
                           QStringLiteral("Step %1 (%2): unknown zone '%3' (will default to FINE).")
                               .arg(i).arg(cmd.code, zone)});
        }

        // CIRC arc authoring rules (docs/REQ_motion_circ.md): the target pose must be Cartesian
        // (checked at teach time too) and the auxiliary via point must be taught and positionally
        // distinct from the target — mirroring the planner's degenerate-geometry rejection so the
        // operator fixes the program in the editor, not as a controller fault at RUN.
        if (isCircCode(cmd.code)) {
            if (pose.isEmpty()) {
                issues.append({i, IssueSeverity::Error, ProgramError::MotionPointMissingPose,
                               QStringLiteral("Step %1 (CIRC): no Cartesian target pose taught.").arg(i)});
            }
            const QVector<double> via = cmd.params.value(kTcpVia).value<QVector<double>>();
            if (via.size() < 3) {
                issues.append({i, IssueSeverity::Error, ProgramError::ViaPointMissing,
                               QStringLiteral("Step %1 (CIRC): via point not taught (use TEACH VIA).").arg(i)});
            } else if (pose.size() >= 3) {
                const double dx = via[0] - pose[0];
                const double dy = via[1] - pose[1];
                const double dz = via[2] - pose[2];
                const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dist < kMinViaTargetSeparationMm) {
                    issues.append({i, IssueSeverity::Error, ProgramError::ViaPointCoincidesWithTarget,
                                   QStringLiteral("Step %1 (CIRC): via point coincides with the target "
                                                  "(distance %2 mm); no unique arc.").arg(i).arg(dist)});
                }
            }
        }
    }

    // SPLINE block rules (docs/REQ_motion_spline.md, batch 2c). A block is a maximal contiguous run
    // of SPLINE motion steps — the same grouping the sequencer/planner executes. Mirroring the
    // planner keeps the failure in the editor instead of a controller fault at RUN:
    //  - consecutive points closer than kMinViaTargetSeparationMm cannot define a curve -> Error;
    //  - differing speeds inside one block -> Warning (the block runs at the FIRST point's speed);
    //  - a non-FINE zone on a spline point -> Warning (blending is ignored, the curve is smooth).
    for (int i = 0; i < m_program.size(); ++i) {
        const ProgramCommand& cmd = m_program.at(i);
        if (cmd.type != CommandType::Motion || !isSplineCode(cmd.code)) {
            continue;
        }
        const int block_start = i;
        int block_end = i; // inclusive
        while (block_end + 1 < m_program.size() &&
               m_program.at(block_end + 1).type == CommandType::Motion &&
               isSplineCode(m_program.at(block_end + 1).code)) {
            ++block_end;
        }
        const int first_speed =
            m_program.at(block_start).params.value(kSpeed, kMaxSpeedPercent).toInt();
        for (int s = block_start; s <= block_end; ++s) {
            const ProgramCommand& point = m_program.at(s);
            const QVector<double> pose = point.params.value(kTcp).value<QVector<double>>();
            if (s > block_start) {
                const QVector<double> prev =
                    m_program.at(s - 1).params.value(kTcp).value<QVector<double>>();
                if (pose.size() >= 3 && prev.size() >= 3) {
                    const double dx = pose[0] - prev[0];
                    const double dy = pose[1] - prev[1];
                    const double dz = pose[2] - prev[2];
                    if (std::sqrt(dx * dx + dy * dy + dz * dz) < kMinViaTargetSeparationMm) {
                        issues.append({s, IssueSeverity::Error, ProgramError::SplinePointsCoincide,
                                       QStringLiteral("Step %1 (SPLINE): coincides with the previous "
                                                      "spline point; no valid curve.").arg(s)});
                    }
                }
                if (point.params.value(kSpeed, kMaxSpeedPercent).toInt() != first_speed) {
                    issues.append({s, IssueSeverity::Warning, ProgramError::SplineBlockSpeedMismatch,
                                   QStringLiteral("Step %1 (SPLINE): speed differs from the block's "
                                                  "first point (%2%%); the block runs at %2%%.")
                                       .arg(s).arg(first_speed)});
                }
            }
            const QString zone = point.params.value(kZone, QStringLiteral("FINE")).toString();
            if (zone.compare(QStringLiteral("FINE"), Qt::CaseInsensitive) != 0) {
                issues.append({s, IssueSeverity::Warning, ProgramError::SplineZoneIgnored,
                               QStringLiteral("Step %1 (SPLINE): zone '%2' is ignored on spline "
                                              "points (the curve is already smooth).").arg(s).arg(zone)});
            }
        }
        i = block_end; // continue after the block
    }

    // Flow-control resolution (flat label model): duplicate LABEL ids and dangling GOTO targets are
    // program-level errors that block RUN, mirroring the controller ProgramSequencer's fail-closed
    // load(). Reported here so the operator sees them in the editor, not as a controller fault.
    QVector<int> definedLabelIds;
    for (const ProgramCommand& cmd : m_program) {
        if (cmd.type == CommandType::Logic && cmd.code == QLatin1String("LABEL")) {
            definedLabelIds.append(cmd.params.value(kLabelId, 0).toInt());
        }
    }
    for (int i = 0; i < m_program.size(); ++i) {
        const ProgramCommand& cmd = m_program.at(i);
        const QString& code = cmd.code;
        if (code == QLatin1String("LABEL")) {
            const int labelId = cmd.params.value(kLabelId, 0).toInt();
            if (definedLabelIds.count(labelId) > 1) {
                issues.append({i, IssueSeverity::Error, ProgramError::DuplicateLabelId,
                               QStringLiteral("Step %1 (LABEL): duplicate label id %2.").arg(i).arg(labelId)});
            }
        } else if (code == QLatin1String("GOTO") || code == QLatin1String("IF")) {
            const int labelId = cmd.params.value(kLabelId, 0).toInt();
            if (!definedLabelIds.contains(labelId)) {
                issues.append({i, IssueSeverity::Error, ProgramError::UnresolvedJumpTarget,
                               QStringLiteral("Step %1 (%2): no LABEL defines target id %3.").arg(i).arg(code).arg(labelId)});
            }
        }
        // WAIT DI and a DI-sourced IF carry a DI condition: the port must be in range, else the
        // controller cannot evaluate it (fail-closed at runtime). A register-sourced IF (P4)
        // carries a register condition instead: validate the register index against the sequencer's
        // fixed file. Both mirrors keep the failure in the editor, not as a controller fault.
        const bool isRegisterIf = (code == QLatin1String("IF") &&
                                   cmd.params.value(kCondSource).toString() == QLatin1String("REG"));
        if (isRegisterIf) {
            const int reg = cmd.params.value(kReg, -1).toInt();
            if (!registerInRange(reg)) {
                issues.append({i, IssueSeverity::Error, ProgramError::RegisterIndexOutOfRange,
                               QStringLiteral("Step %1 (IF): register R[%2] out of range R[0..15].").arg(i).arg(reg)});
            }
        } else if (code == QLatin1String("WAIT DI") || code == QLatin1String("IF")) {
            const int port = cmd.params.value(kPort, 0).toInt();
            if (!portInRange(port)) {
                issues.append({i, IssueSeverity::Error, ProgramError::IoPortOutOfRange,
                               QStringLiteral("Step %1 (%2): DI port %3 out of range 1..32.").arg(i).arg(code).arg(port)});
            }
        }
        // SET/INC/DEC VAR address the fixed register file R[0..15] (P4).
        if (code == QLatin1String("SET VAR") || code == QLatin1String("INC VAR") ||
            code == QLatin1String("DEC VAR")) {
            const int reg = cmd.params.value(kReg, -1).toInt();
            if (!registerInRange(reg)) {
                issues.append({i, IssueSeverity::Error, ProgramError::RegisterIndexOutOfRange,
                               QStringLiteral("Step %1 (%2): register R[%3] out of range R[0..15].").arg(i).arg(code).arg(reg)});
            }
        }
    }
    return issues;
}

bool ProgramBuilder::isRunnable() const {
    const QVector<ProgramIssue> issues = validate();
    for (const ProgramIssue& issue : issues) {
        if (issue.severity == IssueSeverity::Error) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------

RDT::Result<void, ProgramError> ProgramBuilder::undo() {
    if (m_undo.isEmpty()) {
        return ProgramError::UndoStackEmpty;
    }
    m_redo.append(m_program);
    m_program = m_undo.takeLast();
    setModified(true);
    emit programReset(ProgramResetReason::LocalEdit);
    return {};
}

RDT::Result<void, ProgramError> ProgramBuilder::redo() {
    if (m_redo.isEmpty()) {
        return ProgramError::RedoStackEmpty;
    }
    m_undo.append(m_program);
    m_program = m_redo.takeLast();
    setModified(true);
    emit programReset(ProgramResetReason::LocalEdit);
    return {};
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ProgramBuilder::load(const QVector<ProgramCommand>& program, ProgramResetReason reason) {
    m_program = program;
    m_undo.clear();
    m_redo.clear();
    emit programReset(reason);
    setModified(false);
}

void ProgramBuilder::clear() {
    m_program.clear();
    m_undo.clear();
    m_redo.clear();
    emit programReset(ProgramResetReason::LocalEdit);
    setModified(false);
}

void ProgramBuilder::markSaved() {
    setModified(false);
}

} // namespace hexa
// --- END OF FILE: HexaStudio/program_editor/ProgramBuilder.cpp ---
