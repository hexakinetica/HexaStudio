// --- START OF FILE: HexaStudio/program_editor/ProgramBuilder.h ---
/**
 * @file ProgramBuilder.h
 * @brief Single authority for authoring/editing an HMI robot program (HexaStudio side).
 *
 * ProgramBuilder owns the one canonical copy of the program (a QVector<ProgramCommand>) and exposes
 * explicit, validated operations to construct and edit it. The UI reads for display through a model
 * adapter and writes ONLY through this class; nobody else mutates the program. The class is
 * widget-free (Qt Core only) so it is unit-testable without a GUI, and it is wire-agnostic: the
 * mapping to the RDT protocol stays in RobotService.
 *
 * Design decisions (see requirements ProgramBuilder.md):
 *  - Control flow is a FLAT model (labels + jumps); no structured-block compiler here.
 *  - Operations that can fail return RDT::Result<T, ProgramError> with diagnostics; no exceptions,
 *    no std::optional/std::variant in the public surface.
 *  - Undo/redo is a bounded snapshot stack (no command-object hierarchy).
 *  - The execution pointer (active running line) is NOT owned here: it is transient controller
 *    feedback handled by the view. ProgramBuilder owns content only.
 */
#ifndef HEXA_PROGRAM_BUILDER_H
#define HEXA_PROGRAM_BUILDER_H

#include <QObject>
#include <QVector>
#include <QString>

#include "ProgramData.h"   // ProgramCommand, CommandType (panels/left, added to include path)
#include "result.h"        // RDT::Result (shared/data_types)

namespace hexa {

/// @brief Motion types the editor can author. Only implemented kinds are exposed.
/// CIRC is a circular arc through an auxiliary (via) point taught separately with teachVia()
/// (docs/REQ_motion_circ.md). SPLINE points are taught like LIN; a contiguous run of SPLINE steps
/// executes as ONE smooth curve through every point (docs/REQ_motion_spline.md).
enum class MotionKind { PTP, LIN, CIRC, SPLINE };

/// @brief Origin of a full-program reset, so listeners can react differently.
/// ExternalLoad = content came from the controller/initial cache (must NOT be echoed back as an
/// upload); LocalEdit = an operator-driven reset (undo/redo/clear/local file load) that MUST be
/// pushed to the controller. This removes the previous ambiguity where one signal covered both.
enum class ProgramResetReason { ExternalLoad, LocalEdit };

/// @brief Typed authoring errors returned by ProgramBuilder operations.
enum class ProgramError {
    IndexOutOfRange,
    EmptyProgram,
    MotionPointMissingPose,
    SpeedOutOfRange,
    ZoneInvalid,
    AxisOutOfRange,
    PoseNotEditableForJointMove,
    NotAMotionStep,
    NotAWaitStep,
    NotALabelStep,
    NotAConditionStep,
    NotACommentStep,
    NegativeTime,
    NegativeLabelId,
    DuplicateLabelId,
    UnresolvedJumpTarget,
    IoPortOutOfRange,
    NothingToCopy,
    UndoStackEmpty,
    RedoStackEmpty,
    RegisterIndexOutOfRange,      ///< Register index outside R[0..kMaxRegisterIndex] (P4).
    InvalidCompareOp,             ///< Compare-operator token is not one of EQ/NE/GT/LT (P4).
    NotACircStep,                 ///< teachVia() called on a step that is not a CIRC motion point.
    ViaPointMissing,              ///< CIRC step has no taught via point (blocks RUN).
    ViaPointCoincidesWithTarget,  ///< CIRC via position equals the target position (no unique arc).
    SplinePointsCoincide,         ///< Consecutive SPLINE points coincide — no valid curve (blocks RUN).
    SplineBlockSpeedMismatch,     ///< SPLINE block points carry differing speeds (warning: the block
                                  ///< runs at the FIRST point's speed, REQ-SPL-05).
    SplineZoneIgnored             ///< SPLINE point carries a non-FINE zone (warning: blending is
                                  ///< ignored on splines, the curve is already smooth, REQ-SPL-10).
};

/// @brief Human-readable diagnostic for a ProgramError (what/why), used in messages and tests.
QString toString(ProgramError error);

/// @brief Severity of a validation finding. Error blocks RUN; Warning is informational only.
enum class IssueSeverity { Warning, Error };

/// @brief One validation finding, tied to a concrete step (stepIndex == -1 means program-level).
struct ProgramIssue {
    int stepIndex = -1;
    IssueSeverity severity = IssueSeverity::Error;
    ProgramError code = ProgramError::EmptyProgram;
    QString message;
};

/**
 * @class ProgramBuilder
 * @brief Owns and edits the program; the single authoring authority on the HMI side.
 */
class ProgramBuilder : public QObject {
    Q_OBJECT
public:
    explicit ProgramBuilder(QObject* parent = nullptr);

    // --- Canonical parameter keys (single definition; removes magic strings from the UI) ---
    static constexpr const char* kSpeed   = "Speed";
    static constexpr const char* kZone    = "Zone";
    static constexpr const char* kTcp     = "TcpPose";
    static constexpr const char* kTcpVia  = "TcpViaPose"; // CIRC auxiliary point (lockstep with hexa_backend ProgramMapper)
    static constexpr const char* kJoints  = "Joints";
    static constexpr const char* kToolId  = "ToolId";
    static constexpr const char* kBaseId  = "BaseId";
    static constexpr const char* kTime    = "Time";
    static constexpr const char* kLabelId = "LabelId";
    // IO keys: for WAIT DI / IF they map to ProgramStepStruct.condition {io_port, trigger_on_state};
    // for SET DO they map to ProgramStepStruct.{io_port, io_state} (the output assignment).
    static constexpr const char* kPort  = "Port";    // IO port (1-based)
    static constexpr const char* kState = "State";   // level: true = HIGH, false = LOW
    // Register keys (P4): SET/INC/DEC VAR carry kReg (+ kValue for SET VAR); a register-sourced IF
    // carries kCondSource="REG" + kReg/kCompareOp/kOperand and maps to Condition{source=Register,...}.
    static constexpr const char* kReg        = "Reg";
    static constexpr const char* kValue      = "Value";
    static constexpr const char* kCondSource = "CondSource"; // "DI" (default when absent) | "REG"
    static constexpr const char* kCompareOp  = "Op";         // "EQ" | "NE" | "GT" | "LT"
    static constexpr const char* kOperand    = "Operand";
    // Display-only keys (read on the EDIT pages). kCondition is a human-readable mirror of Port/State;
    // kSubtype tags the Logic block kind. Declared so the UI has no magic strings on the read path.
    static constexpr const char* kSubtype   = "Subtype";
    static constexpr const char* kCondition = "Condition";
    static constexpr const char* kSignal    = "Signal";

    // Speed is a percentage; these bounds match ProgramStepStruct.speed_ratio semantics.
    static constexpr int kMinSpeedPercent = 1;
    static constexpr int kMaxSpeedPercent = 100;
    // IO port bounds mirror the controller ProgramSequencer (1-based, 1..32); an out-of-range port
    // is rejected here and by the sequencer's fail-closed condition check. DO ports share the same
    // range (SimDriver validates DO 1..32; a real backend refuses unsupported writes explicitly).
    static constexpr int kMinDigitalInputPort = 1;
    static constexpr int kMaxDigitalInputPort = 32;
    // Register bounds mirror ProgramSequencer::kRegisterCount = 16 (P4, boss decision): R[0..15],
    // 0-based. An out-of-range index is rejected here and fail-closed at the sequencer's load().
    static constexpr int kMinRegisterIndex = 0;
    static constexpr int kMaxRegisterIndex = 15;
    // Number of Cartesian pose components: X/Y/Z (mm), Rx/Ry/Rz (deg).
    static constexpr int kPoseComponents = 6;
    // Minimum via<->target position distance for a CIRC step [mm]. Mirrors the planner's
    // CircMotionProfile::kMinPointSeparation: closer points cannot define a unique arc, so the
    // editor blocks RUN instead of letting the controller fault at plan time.
    static constexpr double kMinViaTargetSeparationMm = 0.01;
    // Undo history depth: bounded for memory and predictability.
    static constexpr int kMaxUndoDepth = 50;

    // --- Construction (afterIndex == -1 appends; otherwise inserts after that row) ---
    RDT::Result<int, ProgramError> addMotionPoint(MotionKind kind,
                                                  const QVector<double>& joints,
                                                  const QVector<double>& tcpPose,
                                                  int toolId, int baseId, bool blend,
                                                  int afterIndex = -1);
    RDT::Result<int, ProgramError> addWait(double seconds, int afterIndex = -1);
    // A comment cannot fail (any text and any afterIndex are valid), so it returns the new index
    // directly rather than a Result that is always success.
    int addComment(const QString& text, int afterIndex = -1);
    // --- Flow control (flat model: LABEL defines a jump target id; GOTO jumps to it) ---
    // A LABEL marks a position the program can jump to; its id must be >= 0. Duplicate ids are a
    // program-level error surfaced by validate() (not rejected here, so the operator can fix either
    // copy). A GOTO transfers control to the LABEL with the same id; an id with no matching LABEL is
    // an unresolved-jump error surfaced by validate(). Both mirror the controller ProgramSequencer's
    // fail-closed load() so the operator sees the problem in the editor, not as a controller fault.
    RDT::Result<int, ProgramError> addLabel(int labelId, int afterIndex = -1);
    RDT::Result<int, ProgramError> addGoto(int labelId, int afterIndex = -1);
    // --- DI conditions (WAIT DI blocks until DI[port]==triggerState; IF jumps to a label when it is) ---
    // port must be in [kMinDigitalInputPort, kMaxDigitalInputPort]; triggerState true=HIGH, false=LOW.
    RDT::Result<int, ProgramError> addWaitDI(int port, bool triggerState, int afterIndex = -1);
    RDT::Result<int, ProgramError> addConditionalJump(int port, bool triggerState,
                                                      int targetLabelId, int afterIndex = -1);
    // SET DO drives digital output DO[port] to state (true=HIGH, false=LOW). Executable since
    // sequencer P3: the controller actuates it through the HAL and faults the program if the active
    // backend refuses the write. DO shares the DI 1-based port range [kMin..kMaxDigitalInputPort].
    RDT::Result<int, ProgramError> addSetDo(int port, bool state, int afterIndex = -1);

    // --- Registers (P4): SET/INC/DEC over R[0..kMaxRegisterIndex]; a register-compare IF; BREAK ---
    // The canonical counter loop is authored flat: SET VAR R0=N; LABEL; ...; DEC VAR R0;
    // IF R0>0 GOTO label (boss decision: no dedicated "run N cycles" RUN flag).
    RDT::Result<int, ProgramError> addSetVar(int reg, int value, int afterIndex = -1);
    RDT::Result<int, ProgramError> addIncVar(int reg, int afterIndex = -1);
    RDT::Result<int, ProgramError> addDecVar(int reg, int afterIndex = -1);
    // op is one of "EQ"/"NE"/"GT"/"LT" (validated; the same token set the mapper speaks).
    RDT::Result<int, ProgramError> addConditionalJumpOnRegister(int reg, const QString& op,
                                                                int operand, int targetLabelId,
                                                                int afterIndex = -1);
    // BREAK stops the program immediately at that line (STOP-from-code; boss decision).
    RDT::Result<int, ProgramError> addBreak(int afterIndex = -1);

    // --- Editing one existing step (validated) ---
    RDT::Result<void, ProgramError> setSpeed(int index, int percent);
    RDT::Result<void, ProgramError> setZone(int index, const QString& zone);
    RDT::Result<void, ProgramError> setTcpComponent(int index, int axis, double value);
    RDT::Result<void, ProgramError> touchUp(int index, const QVector<double>& joints,
                                                        const QVector<double>& tcpPose);
    // Teach/re-teach the auxiliary (via) point of an existing CIRC step from the current robot TCP
    // pose (KUKA-style two-touch flow: + TEACH records the target, TEACH VIA records the arc's
    // via point). Only valid for CIRC motion steps; the pose must be non-empty.
    RDT::Result<void, ProgramError> teachVia(int index, const QVector<double>& tcpPose);
    RDT::Result<void, ProgramError> setWaitTime(int index, double seconds);
    RDT::Result<void, ProgramError> setLabelId(int index, int labelId);
    /// @brief Replaces a comment step's text (undoable). Empty input falls back to the same
    /// "COMMENT" placeholder addComment uses; fails typed on a non-comment step.
    RDT::Result<void, ProgramError> setCommentText(int index, const QString& text);
    // Edit the IO port + level of an existing WAIT DI / DI-sourced IF step (its DI condition) or
    // SET DO step (its output assignment). A register-sourced IF is refused here (see
    // setRegisterCondition); any other step type is refused with NotAConditionStep.
    RDT::Result<void, ProgramError> setCondition(int index, int port, bool triggerState);
    // Edit the register condition (R[reg] op operand) of an existing register-sourced IF (P4).
    // The condition source of a step never flips silently: a DI IF is refused here.
    RDT::Result<void, ProgramError> setRegisterCondition(int index, int reg, const QString& op,
                                                         int operand);

    // --- Structural editing ---
    RDT::Result<void, ProgramError> remove(int index);
    RDT::Result<void, ProgramError> move(int from, int to);
    RDT::Result<void, ProgramError> copy(int index);
    RDT::Result<int,  ProgramError> paste(int afterIndex);

    // --- Whole-program verification before RUN (empty list => ok) ---
    QVector<ProgramIssue> validate() const;
    /// @brief True if validate() found no Error-severity issue (RUN is allowed).
    bool isRunnable() const;

    // --- Undo / redo (bounded snapshot stack) ---
    RDT::Result<void, ProgramError> undo();
    RDT::Result<void, ProgramError> redo();
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }

    // --- Read access ---
    const QVector<ProgramCommand>& program() const { return m_program; }
    const ProgramCommand& at(int index) const;
    int stepCount() const { return m_program.size(); }

    // --- Lifecycle / persistence integration ---
    // External load (default) clears undo/redo and does not echo an upload; pass LocalEdit for an
    // operator-driven local file load that must be pushed to the controller.
    void load(const QVector<ProgramCommand>& program,
              ProgramResetReason reason = ProgramResetReason::ExternalLoad);
    void clear();
    bool isModified() const { return m_modified; }
    void markSaved();

    // --- Helpers exposed for the model adapter / tests ---
    static bool isMotionCode(const QString& code);
    static bool isCartesianDominant(const QString& code);
    static bool isCircCode(const QString& code);        // CIRC / MoveC
    static bool isSplineCode(const QString& code);      // SPLINE / MoveS
    static bool isKnownZone(const QString& zone);
    static bool isLabelCode(const QString& code);       // GOTO / LABEL
    static bool isLogicBlockCode(const QString& code);  // IF / ELSE / END_IF / WAIT

signals:
    void programReset(ProgramResetReason reason);  // full replacement (load/clear/undo/redo) -> model resets
    void stepInserted(int index);
    void stepRemoved(int index);
    void stepChanged(int index);     // single-step edit -> model dataChanged(index)
    void stepMoved(int from, int to);
    void modifiedChanged(bool modified);

private:
    bool indexValid(int index) const { return index >= 0 && index < m_program.size(); }
    int  resolveInsertIndex(int afterIndex) const; // afterIndex==-1 -> end; else afterIndex+1 (clamped)
    void pushUndoSnapshot();          // snapshot current program before a content mutation
    void setModified(bool modified);

    QVector<ProgramCommand> m_program;                 // single source of truth
    ProgramCommand          m_copyBuffer;
    bool                    m_hasCopy = false;
    QVector<QVector<ProgramCommand>> m_undo;           // snapshots taken before mutations
    QVector<QVector<ProgramCommand>> m_redo;
    bool                    m_modified = false;
};

} // namespace hexa

#endif // HEXA_PROGRAM_BUILDER_H
// --- END OF FILE: HexaStudio/program_editor/ProgramBuilder.h ---
