// --- START OF FILE: HexaStudio/overlays/DiagnosticsPanel.cpp ---
#include "DiagnosticsPanel.h"

#include "HexaTheme.h"
#include "HexaWidgets.h"

#include <QColor>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace hexa {

namespace {
// Wider than the shipping 260 px popup: the event log needs readable rows.
constexpr int kPanelWidthPx = 520;
constexpr int kEventListHeightPx = 210;
// No status update within this window means the diagnostics are STALE, not "still OK".
constexpr int kTelemetryStaleTimeoutMs = 1500;
constexpr int kSubsystemBadgeMinWidthPx = 150;

QString severityToken(DiagSeverity severity) {
    switch (severity) {
    case DiagSeverity::Error:   return QStringLiteral("ERR");
    case DiagSeverity::Warning: return QStringLiteral("WRN");
    case DiagSeverity::Info:    return QStringLiteral("INF");
    }
    return QStringLiteral("???");
}

QString severityColor(DiagSeverity severity) {
    switch (severity) {
    case DiagSeverity::Error:   return Hexa::Colors::Alert;
    case DiagSeverity::Warning: return QStringLiteral("#ff9800");
    case DiagSeverity::Info:    return Hexa::Colors::TextMuted;
    }
    return Hexa::Colors::TextMuted;
}

QString messageStyle(bool isFault) {
    if (isFault) {
        return QStringLiteral(
            "QLabel { color: %1; font-family: '%2'; font-size: 12px; font-weight: bold;"
            " background-color: rgba(255, 49, 49, 0.12); border: none;"
            " border-left: 3px solid %1; border-radius: 0px; padding: 6px 8px; }")
            .arg(Hexa::Colors::Alert, Hexa::Fonts::familyMono());
    }
    return QStringLiteral(
        "QLabel { color: %1; font-family: '%2'; font-size: 12px;"
        " background-color: rgba(0,0,0,0.35); border: none;"
        " border-left: 3px solid rgba(69, 162, 158, 0.5); border-radius: 0px; padding: 6px 8px; }")
        .arg(Hexa::Colors::TextMuted, Hexa::Fonts::familyMono());
}
} // namespace

DiagnosticsPanel::DiagnosticsPanel(QWidget* parent) : QWidget(parent) {
    setupUi();
    adjustSize();   // content-based size before the first on-demand reveal (shipping behaviour)

    connect(&m_log, &DiagnosticsLog::eventAppended, this, &DiagnosticsPanel::onEventAppended);
    connect(&m_log, &DiagnosticsLog::cleared, this, [this]() { m_eventList->clear(); });

    m_staleTimer.setSingleShot(true);
    m_staleTimer.setInterval(kTelemetryStaleTimeoutMs);
    connect(&m_staleTimer, &QTimer::timeout, this, &DiagnosticsPanel::onTelemetryStale);
    // Until the first status arrives the panel IS stale - show that, not fake health. (Quiet on
    // the log side: the stream has not started yet, so there is no "gap" to record.)
    m_telemetryStale = true;
    onTelemetryStale();
}

void DiagnosticsPanel::setupUi() {
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "DiagnosticsPanel { background-color: %1; border: 1px solid rgba(69, 162, 158, 0.5);"
        " border-radius: 8px; }").arg(Hexa::Colors::Surface));
    setFixedWidth(kPanelWidthPx);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);

    // --- Header: title + close ---
    QHBoxLayout* titleRow = new QHBoxLayout();
    QLabel* title = new QLabel(QStringLiteral("DIAGNOSTICS"), this);
    title->setStyleSheet(Hexa::Styles::LabelHeaderSimple);
    titleRow->addWidget(title);
    titleRow->addStretch();
    QPushButton* closeButton = new QPushButton(QStringLiteral("✕"), this);
    closeButton->setFixedSize(20, 20);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; font-size: 12px; }"
        "QPushButton:hover { color: %2; }").arg(Hexa::Colors::TextMuted, Hexa::Colors::TextMain));
    connect(closeButton, &QPushButton::clicked, this, &DiagnosticsPanel::closeRequested);
    titleRow->addWidget(closeButton);
    layout->addLayout(titleRow);
    layout->addWidget(HexaWidgets::createSeparatorH());

    // --- Decoded fault / status message (alert strip when faulted) ---
    m_messageLabel = new QLabel(this);
    m_messageLabel->setWordWrap(true);
    layout->addWidget(m_messageLabel);

    // --- Subsystem status rows: full names + shared badge palette ---
    QGridLayout* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    const struct {
        const char* label;
        QLabel** badge;
    } rows[] = {
        {"BRIDGE", &m_bridgeBadge},
        {"PLANNER", &m_plannerBadge},
        {"SAFETY", &m_safetyBadge},
        {"INTERP", &m_interpolatorBadge},
        {"HAL", &m_halBadge},
    };
    int row = 0;
    for (const auto& def : rows) {
        QLabel* name = HexaWidgets::createLabelText(def.label);
        grid->addWidget(name, row, 0);
        *def.badge = HexaWidgets::createBadge(QStringLiteral("---"), kSubsystemBadgeMinWidthPx,
                                              Hexa::Colors::BadgeNeutralBg,
                                              Hexa::Colors::BadgeNeutralFg);
        (*def.badge)->setAlignment(Qt::AlignCenter);
        grid->addWidget(*def.badge, row, 1);
        ++row;
    }
    layout->addLayout(grid);

    // --- Event log: timestamped state transitions, newest first ---
    QHBoxLayout* logHeader = new QHBoxLayout();
    QLabel* logTitle = new QLabel(QStringLiteral("EVENT LOG"), this);
    logTitle->setStyleSheet(Hexa::Styles::LabelHeaderSimple);
    logHeader->addWidget(logTitle);
    logHeader->addStretch();
    m_clearLogButton = HexaWidgets::createButtonSm(QStringLiteral("CLEAR LOG"), this, 90, 24);
    m_clearLogButton->setObjectName(QStringLiteral("btnDiagClearLog"));
    m_clearLogButton->setToolTip(QStringLiteral(
        "Clears this view's history only - controller error latches are untouched (RESET ERROR)"));
    connect(m_clearLogButton, &QPushButton::clicked, this, [this]() { m_log.clear(); });
    logHeader->addWidget(m_clearLogButton);
    layout->addLayout(logHeader);

    m_eventList = new QListWidget(this);
    m_eventList->setObjectName(QStringLiteral("listDiagEvents"));
    m_eventList->setFixedHeight(kEventListHeightPx);
    m_eventList->setSelectionMode(QAbstractItemView::NoSelection);
    m_eventList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_eventList->setWordWrap(false);
    m_eventList->setStyleSheet(Hexa::Styles::ListView + QStringLiteral(
        "QListView { font-family: '%1'; font-size: 10px; }"
        "QListView::item { padding: 2px 6px; }").arg(Hexa::Fonts::familyMono()));
    layout->addWidget(m_eventList);

    // --- Actions: RESET ERROR is armed only while an error is latched; E-STOP always available ---
    QHBoxLayout* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    m_clearErrorButton = HexaWidgets::createButtonStd(QStringLiteral("RESET ERROR"), this, 0, 34);
    m_clearErrorButton->setObjectName(QStringLiteral("btnDiagClearError"));
    m_clearErrorButton->setMaximumWidth(QWIDGETSIZE_MAX);
    m_clearErrorButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_clearErrorButton->setEnabled(false);
    m_clearErrorButton->setToolTip(QStringLiteral("Enabled while a controller error is latched"));
    connect(m_clearErrorButton, &QPushButton::clicked,
            this, &DiagnosticsPanel::clearErrorRequested);
    btnRow->addWidget(m_clearErrorButton, 1);

    m_eStopButton = HexaWidgets::createButtonDanger(QStringLiteral("E-STOP"), this, 0, 34);
    m_eStopButton->setObjectName(QStringLiteral("btnDiagEStop"));
    m_eStopButton->setMaximumWidth(QWIDGETSIZE_MAX);
    m_eStopButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_eStopButton, &QPushButton::clicked, this, &DiagnosticsPanel::eStopRequested);
    btnRow->addWidget(m_eStopButton, 1);
    layout->addLayout(btnRow);
}

// Subsystem badge mapping. "Not Connected" and empty are NEUTRAL - unknown is not healthy (the
// shipping overlay painted "Not Connected" green).
void DiagnosticsPanel::applySubsystemBadge(QLabel* badge, const QString& value) {
    const QString text = value.isEmpty() ? QStringLiteral("---") : value;
    if (value == QLatin1String("Ok")) {
        HexaWidgets::setBadge(badge, text, Hexa::Colors::BadgeOk, Hexa::Colors::BadgeFgLight);
    } else if (value.contains(QLatin1String("Warning"))) {
        HexaWidgets::setBadge(badge, text, Hexa::Colors::BadgeWarn, Hexa::Colors::BadgeFgDark);
    } else if (value.contains(QLatin1String("Error")) || value.contains(QLatin1String("Fault"))) {
        HexaWidgets::setBadge(badge, text, Hexa::Colors::BadgeError, Hexa::Colors::BadgeFgLight);
    } else {
        HexaWidgets::setBadge(badge, text, Hexa::Colors::BadgeNeutralBg,
                              Hexa::Colors::BadgeNeutralFg);
    }
}

void DiagnosticsPanel::showMessage(const QString& text, bool isFault) {
    m_messageLabel->setText(text);
    m_messageLabel->setStyleSheet(messageStyle(isFault));
}

void DiagnosticsPanel::updateStatus(const HmiRobotStatus& status) {
    m_staleTimer.start();   // fresh telemetry - re-arm the watchdog
    if (m_telemetryStale) {
        // A resumed stream is only an event if it had actually started before (baseline exists).
        if (m_log.hasBaseline()) {
            m_log.noteTelemetryResumed();
        }
        m_telemetryStale = false;
    }
    m_log.onStatusChanged(status);   // history first: transitions are recorded before repainting

    applySubsystemBadge(m_bridgeBadge, status.top.isConnected ? QStringLiteral("Ok")
                                                              : QStringLiteral("Not Connected"));
    applySubsystemBadge(m_plannerBadge, status.motion.plannerStatus);
    applySubsystemBadge(m_safetyBadge, status.motion.safetyStatus);
    applySubsystemBadge(m_interpolatorBadge, status.motion.interpolatorStatus);
    applySubsystemBadge(m_halBadge, status.motion.halStatus);

    // Decoded fault message; the failing axis is named explicitly.
    const bool fault = status.top.hasError || status.top.isEStop;
    QString message = status.top.activeErrors;
    if (message.isEmpty()) {
        message = fault ? QStringLiteral("Unknown fault") : QStringLiteral("System OK");
    }
    if (fault && status.motion.failingAxisId >= 0) {
        message += QStringLiteral(" (axis A%1)").arg(status.motion.failingAxisId + 1);
    }
    showMessage(message, fault);

    // RESET ERROR is armed only while there is something to reset.
    m_clearErrorButton->setEnabled(status.top.hasError);
    m_eStopButton->setText(status.top.isEStop ? QStringLiteral("RESET E-STOP")
                                              : QStringLiteral("E-STOP"));
}

// Telemetry stopped: show exactly that. Stale values pretending to be live are a hazard.
void DiagnosticsPanel::onTelemetryStale() {
    // Record the gap in the history exactly once per episode (the constructor pre-sets the stale
    // flag, so a never-started stream is not logged as a "gap").
    if (!m_telemetryStale) {
        m_telemetryStale = true;
        m_log.noteTelemetryStale();
    }
    for (QLabel* badge : {m_bridgeBadge, m_plannerBadge, m_safetyBadge, m_interpolatorBadge,
                          m_halBadge}) {
        HexaWidgets::setBadge(badge, QStringLiteral("---"), Hexa::Colors::BadgeNeutralBg,
                              Hexa::Colors::BadgeNeutralFg);
    }
    showMessage(QStringLiteral("Telemetry stale — no status updates"), true);
    m_clearErrorButton->setEnabled(false);
}

// One log row: "HH:mm:ss.zzz  ERR  SOURCE        message", coloured by severity (existing status
// colours). Newest entries on top; the visible list mirrors the log bound.
void DiagnosticsPanel::onEventAppended(const DiagEvent& event) {
    const QString time =
        QDateTime::fromMSecsSinceEpoch(event.timestampMs).toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString row = QStringLiteral("%1  %2  %3  %4")
                            .arg(time,
                                 severityToken(event.severity),
                                 event.source.leftJustified(12, QLatin1Char(' ')),
                                 event.message);
    QListWidgetItem* item = new QListWidgetItem(row);
    item->setForeground(QColor(severityColor(event.severity)));
    item->setToolTip(row);
    m_eventList->insertItem(0, item);
    while (m_eventList->count() > DiagnosticsLog::kMaxEvents) {
        delete m_eventList->takeItem(m_eventList->count() - 1);
    }
}

} // namespace hexa
// --- END OF FILE: HexaStudio/overlays/DiagnosticsPanel.cpp ---
