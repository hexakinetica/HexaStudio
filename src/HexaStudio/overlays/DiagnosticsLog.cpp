// --- START OF FILE: HexaStudio/overlays/DiagnosticsLog.cpp ---
#include "DiagnosticsLog.h"

#include <QDateTime>

namespace hexa {

namespace {
// Fixed source tokens (uppercase, padded by the view): one vocabulary for the whole log.
const QLatin1String kSourceBridge("BRIDGE");
const QLatin1String kSourceEStop("E-STOP");
const QLatin1String kSourceSystem("SYSTEM");
const QLatin1String kSourcePlanner("PLANNER");
const QLatin1String kSourceSafety("SAFETY");
const QLatin1String kSourceInterpolator("INTERPOLATOR");
const QLatin1String kSourceHal("HAL");
const QLatin1String kSourceProgram("PROGRAM");
const QLatin1String kSourceTelemetry("TELEMETRY");
} // namespace

DiagnosticsLog::DiagnosticsLog(QObject* parent) : QObject(parent) {}

DiagHealth DiagnosticsLog::healthForStatus(const QString& statusText) {
    const QString trimmed = statusText.trimmed();
    if (trimmed.isEmpty()) {
        return DiagHealth::Inactive;   // no data yet - not healthy, not an alarm
    }
    if (trimmed.compare(QLatin1String("Ok"), Qt::CaseInsensitive) == 0) {
        return DiagHealth::Healthy;
    }
    // A dead link is NOT healthy (the shipping overlay painted it green) - but it is a legitimate
    // idle state (e.g. HAL before bring-up), so it is Inactive, not Error.
    if (trimmed.compare(QLatin1String("NotConnected"), Qt::CaseInsensitive) == 0 ||
        trimmed.compare(QLatin1String("Not Connected"), Qt::CaseInsensitive) == 0) {
        return DiagHealth::Inactive;
    }
    if (trimmed.contains(QLatin1String("Warning"), Qt::CaseInsensitive)) {
        return DiagHealth::Warning;
    }
    // Unknown controller state must alarm, never blend in.
    return DiagHealth::Error;
}

QString DiagnosticsLog::faultMessage(const HmiRobotStatus& status) {
    QString message = status.top.activeErrors.trimmed();
    if (message.isEmpty()) {
        message = QStringLiteral("Unknown fault (controller reported no message)");
    }
    if (status.motion.failingAxisId >= 0) {
        message += QStringLiteral(" (axis A%1)").arg(status.motion.failingAxisId + 1);
    }
    return message;
}

void DiagnosticsLog::clear() {
    m_events.clear();
    emit cleared();
}

void DiagnosticsLog::noteTelemetryStale() {
    append(DiagSeverity::Warning, kSourceTelemetry,
           QStringLiteral("Status stream stopped - values below may be outdated"));
}

void DiagnosticsLog::noteTelemetryResumed() {
    append(DiagSeverity::Info, kSourceTelemetry, QStringLiteral("Status stream resumed"));
}

void DiagnosticsLog::onStatusChanged(const HmiRobotStatus& status) {
    if (!m_hasBaseline) {
        // Adopt the first snapshot silently (no event storm on connect), but report problems that
        // are ALREADY active - they are what the operator opened the panel for.
        m_prevConnected = status.top.isConnected;
        m_prevEStop = status.top.isEStop;
        m_prevHasError = status.top.hasError;
        m_prevRunning = status.prog.isRunning;
        m_prevActiveErrors = status.top.activeErrors;
        m_prevPlanner = status.motion.plannerStatus;
        m_prevSafety = status.motion.safetyStatus;
        m_prevInterpolator = status.motion.interpolatorStatus;
        m_prevHal = status.motion.halStatus;
        m_hasBaseline = true;

        if (!status.top.isConnected) {
            append(DiagSeverity::Error, kSourceBridge,
                   QStringLiteral("Controller bridge is not connected - all commands are blocked"));
        }
        if (status.top.isEStop) {
            append(DiagSeverity::Error, kSourceEStop, QStringLiteral("E-Stop is engaged"));
        }
        if (status.top.hasError) {
            append(DiagSeverity::Error, kSourceSystem, faultMessage(status));
        }
        return;
    }

    // --- Bridge connection edges ---
    if (status.top.isConnected != m_prevConnected) {
        if (status.top.isConnected) {
            append(DiagSeverity::Info, kSourceBridge, QStringLiteral("Controller bridge connected"));
        } else {
            append(DiagSeverity::Error, kSourceBridge,
                   QStringLiteral("Controller bridge DISCONNECTED - all commands are blocked"));
        }
        m_prevConnected = status.top.isConnected;
    }

    // --- E-Stop edges ---
    if (status.top.isEStop != m_prevEStop) {
        if (status.top.isEStop) {
            append(DiagSeverity::Error, kSourceEStop, QStringLiteral("E-Stop ENGAGED"));
        } else {
            append(DiagSeverity::Info, kSourceEStop, QStringLiteral("E-Stop released"));
        }
        m_prevEStop = status.top.isEStop;
    }

    // --- Latched fault edges (a changed message while faulted is a NEW fault) ---
    if (status.top.hasError &&
        (!m_prevHasError || status.top.activeErrors != m_prevActiveErrors)) {
        append(DiagSeverity::Error, kSourceSystem, faultMessage(status));
    } else if (!status.top.hasError && m_prevHasError) {
        append(DiagSeverity::Info, kSourceSystem, QStringLiteral("Fault cleared"));
    }
    m_prevHasError = status.top.hasError;
    m_prevActiveErrors = status.top.activeErrors;

    // --- Subsystem status transitions ---
    trackSubsystem(kSourcePlanner, m_prevPlanner, status.motion.plannerStatus);
    trackSubsystem(kSourceSafety, m_prevSafety, status.motion.safetyStatus);
    trackSubsystem(kSourceInterpolator, m_prevInterpolator, status.motion.interpolatorStatus);
    trackSubsystem(kSourceHal, m_prevHal, status.motion.halStatus);

    // --- Program context (Info level: useful ordering context around faults) ---
    if (status.prog.isRunning != m_prevRunning) {
        if (status.prog.isRunning) {
            append(DiagSeverity::Info, kSourceProgram,
                   QStringLiteral("Program started: %1").arg(status.prog.loadedProgramName));
        } else {
            append(DiagSeverity::Info, kSourceProgram, QStringLiteral("Program stopped"));
        }
        m_prevRunning = status.prog.isRunning;
    }
}

void DiagnosticsLog::trackSubsystem(const QString& source, QString& previous,
                                    const QString& current) {
    if (current == previous) {
        return;
    }
    if (current.trimmed().isEmpty()) {
        previous = current;   // telemetry gap: remember it, but an empty string is not an event
        return;
    }
    DiagSeverity severity = DiagSeverity::Info;
    switch (healthForStatus(current)) {
    case DiagHealth::Error:   severity = DiagSeverity::Error; break;
    case DiagHealth::Warning: severity = DiagSeverity::Warning; break;
    case DiagHealth::Healthy:
    case DiagHealth::Inactive: severity = DiagSeverity::Info; break;
    }
    const QString message = previous.trimmed().isEmpty()
                                ? current
                                : QStringLiteral("%1 -> %2").arg(previous, current);
    append(severity, source, message);
    previous = current;
}

void DiagnosticsLog::append(DiagSeverity severity, const QString& source, const QString& message) {
    DiagEvent event;
    event.timestampMs = QDateTime::currentMSecsSinceEpoch();
    event.severity = severity;
    event.source = source;
    event.message = message;
    m_events.append(event);
    while (m_events.size() > kMaxEvents) {
        m_events.removeFirst();
    }
    emit eventAppended(event);
}

} // namespace hexa
// --- END OF FILE: HexaStudio/overlays/DiagnosticsLog.cpp ---
