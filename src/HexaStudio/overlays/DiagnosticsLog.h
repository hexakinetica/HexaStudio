// --- START OF FILE: HexaStudio/overlays/DiagnosticsLog.h ---
/**
 * @file DiagnosticsLog.h
 * @brief Widget-free diagnostics view-model: turns the HmiRobotStatus stream into a bounded,
 *        timestamped event log plus a typed per-subsystem health classification.
 *
 * Why a separate Qt-Core class (same pattern as program_editor's ProgramBuilder):
 *   - the shipping DiagnosticsOverlay showed the INSTANTANEOUS state only - an intermittent fault
 *     (a SyncLost flicker, a transient axis fault) vanished before the operator could open the
 *     panel. Diagnostics must answer "what happened, in what order", which requires edge detection
 *     and history - logic that belongs in a testable, widget-free object, not in paintEvent code;
 *   - severity/health is typed (enums) and classified in ONE function instead of string-matching
 *     scattered through the view. This also fixes a shipping bug: "Not Connected" was painted
 *     GREEN, making a dead link look healthy.
 *
 * Behaviour contract:
 *   - the FIRST status snapshot is adopted silently as the baseline (no event storm on connect),
 *     EXCEPT active problems (engaged E-Stop, latched fault, disconnected bridge) - the operator
 *     opens diagnostics to learn exactly about those;
 *   - subsequent snapshots produce events only on state TRANSITIONS;
 *   - the log is bounded (kMaxEvents, oldest dropped) and never clears itself - clearing is an
 *     explicit view action and never touches controller latches.
 */
#ifndef HEXA_DIAGNOSTICS_LOG_H
#define HEXA_DIAGNOSTICS_LOG_H

#include <QObject>
#include <QString>
#include <QVector>

#include "BackendTypes.h"   // HmiRobotStatus

namespace hexa {

// Event severity for the log (typed - never derived by the view from strings).
enum class DiagSeverity : int {
    Info = 0,
    Warning = 1,
    Error = 2
};

// Display health of one subsystem. Inactive is deliberately distinct from Healthy: a not-connected
// link is not an error, but it must NEVER render as "all good" (shipping bug).
enum class DiagHealth : int {
    Healthy = 0,
    Inactive = 1,
    Warning = 2,
    Error = 3
};

struct DiagEvent {
    qint64 timestampMs = 0;         // epoch milliseconds
    DiagSeverity severity = DiagSeverity::Info;
    QString source;                 // fixed uppercase token: BRIDGE / E-STOP / SYSTEM / PLANNER / ...
    QString message;
};

class DiagnosticsLog : public QObject {
    Q_OBJECT
public:
    explicit DiagnosticsLog(QObject* parent = nullptr);

    // Bounded history: enough to cover a commissioning session, small enough to stay a UI object.
    static constexpr int kMaxEvents = 200;

    // ONE classification of a wire status string ("Ok", "NotConnected", "Warning: SyncLost",
    // "Error(AxisLimit)", ...) into a display health. Unknown text is an Error by policy: an
    // unrecognised controller state must alarm, not blend in.
    static DiagHealth healthForStatus(const QString& statusText);

    // Human-readable fault line: activeErrors (or an explicit placeholder) plus the failing axis.
    static QString faultMessage(const HmiRobotStatus& status);

    const QVector<DiagEvent>& events() const { return m_events; }
    bool hasBaseline() const { return m_hasBaseline; }
    void clear();

    // Telemetry liveness is observed by the view (its stale watchdog), not derivable from status
    // content - these record it in the same log so the operator sees gaps in the history.
    void noteTelemetryStale();
    void noteTelemetryResumed();

public slots:
    void onStatusChanged(const HmiRobotStatus& status);

signals:
    void eventAppended(const DiagEvent& event);
    void cleared();

private:
    void append(DiagSeverity severity, const QString& source, const QString& message);
    void trackSubsystem(const QString& source, QString& previous, const QString& current);

    bool m_hasBaseline = false;

    // Previous snapshot fields for edge detection.
    bool m_prevConnected = false;
    bool m_prevEStop = false;
    bool m_prevHasError = false;
    bool m_prevRunning = false;
    QString m_prevActiveErrors;
    QString m_prevPlanner;
    QString m_prevSafety;
    QString m_prevInterpolator;
    QString m_prevHal;

    QVector<DiagEvent> m_events;
};

} // namespace hexa

#endif // HEXA_DIAGNOSTICS_LOG_H
// --- END OF FILE: HexaStudio/overlays/DiagnosticsLog.h ---
