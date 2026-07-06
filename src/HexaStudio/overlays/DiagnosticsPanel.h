// --- START OF FILE: HexaStudio/overlays/DiagnosticsPanel.h ---
/**
 * @file DiagnosticsPanel.h
 * @brief Standalone, RobotService-free diagnostics popup for the new-product overlays module.
 *
 * Module-owned re-implementation of the shipping DiagnosticsOverlay (which stays untouched). Same
 * contract, so it drops into the same MainWindow wiring:
 *   - intent out: clearErrorRequested / eStopRequested / closeRequested;
 *   - feedback in: updateStatus(HmiRobotStatus).
 *
 * Differences vs the shipping overlay:
 *   - themed instrument card (Surface, theme border, header title) instead of ad-hoc grays;
 *   - subsystem statuses as the shared badge palette with FULL names (BRIDGE / PLANNER / SAFETY /
 *     INTERP / HAL) instead of colored text with cryptic PLAN/SAFE/INTP prefixes; "Not Connected"
 *     is NEUTRAL, not green (the shipping overlay painted it as healthy);
 *   - the decoded fault message is an alert strip: muted "System OK" normally, alert-bordered and
 *     readable when faulted (failing axis named);
 *   - RESET ERROR is enabled ONLY while an error is actually latched (the shipping overlay showed
 *     a permanently red button);
 *   - telemetry stale watchdog: if no status update arrives within the timeout, every badge drops
 *     to "---" and the message says so - stale diagnostics must never masquerade as live ones;
 *   - EVENT LOG with timestamps: state TRANSITIONS (bridge, E-Stop, faults, subsystem statuses,
 *     program start/stop, telemetry gaps) are recorded by the widget-free DiagnosticsLog
 *     view-model (bounded, headless-testable) and listed newest-first. The shipping overlay showed
 *     the instantaneous state only, so an intermittent fault vanished before the operator could
 *     open the panel - history is the core diagnostics requirement. CLEAR LOG is view-local and
 *     never touches controller latches.
 */
#ifndef HEXA_DIAGNOSTICS_PANEL_H
#define HEXA_DIAGNOSTICS_PANEL_H

#include <QWidget>
#include <QTimer>

#include "DiagnosticsLog.h"
#include "BackendTypes.h"   // HmiRobotStatus

class QLabel;
class QListWidget;
class QPushButton;

namespace hexa {

class DiagnosticsPanel : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticsPanel(QWidget* parent = nullptr);

    // Read-only view-model access for the bench/tests: assert events without scraping the UI.
    const DiagnosticsLog& log() const { return m_log; }

public slots:
    void updateStatus(const HmiRobotStatus& status);

signals:
    void clearErrorRequested();
    void eStopRequested();
    void closeRequested();

private slots:
    void onTelemetryStale();
    void onEventAppended(const DiagEvent& event);

private:
    void setupUi();
    void applySubsystemBadge(QLabel* badge, const QString& value);
    void showMessage(const QString& text, bool isFault);

    DiagnosticsLog m_log;

    QLabel* m_messageLabel = nullptr;
    QLabel* m_bridgeBadge = nullptr;
    QLabel* m_plannerBadge = nullptr;
    QLabel* m_safetyBadge = nullptr;
    QLabel* m_interpolatorBadge = nullptr;
    QLabel* m_halBadge = nullptr;
    QListWidget* m_eventList = nullptr;
    QPushButton* m_clearLogButton = nullptr;
    QPushButton* m_clearErrorButton = nullptr;
    QPushButton* m_eStopButton = nullptr;

    // Stale watchdog: diagnostics that stopped updating are a hazard, not information.
    QTimer m_staleTimer;
    bool m_telemetryStale = false;
};

} // namespace hexa

#endif // HEXA_DIAGNOSTICS_PANEL_H
// --- END OF FILE: HexaStudio/overlays/DiagnosticsPanel.h ---
