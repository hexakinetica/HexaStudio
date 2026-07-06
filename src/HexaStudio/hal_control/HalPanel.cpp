// --- START OF FILE: HexaStudio/hal_control/HalPanel.cpp ---
#include "HalPanel.h"
#include "OverlayWidgets.h"   // reach-through to the overlays module blocks (hexa_ui_kit candidate)

#include "HexaTheme.h"
#include "HexaWidgets.h"

#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace hexa {

namespace {
// Slightly wider than the shipping overlay (1240): the themed cards carry their own margins, so
// the axis table needs the extra width to fit without a horizontal scrollbar.
constexpr int kFrameWidthPx = 1280;
constexpr int kFrameHeightPx = 720;
constexpr int kSidebarWidthPx = 248;
constexpr int kDimAlpha = 200;
constexpr int kAxisCount = 6;

// Per-axis telemetry stale detection, 1:1 with the shipping overlay / tcp_client_app.
constexpr int kAxisStaleTimeoutMs = 1500;
constexpr int kStaleTimerPeriodMs = 250;

// Axis table geometry, 1:1 with the shipping overlay.
constexpr int kAxisColumnWidth = 56;
constexpr int kStateColumnWidth = 142;
constexpr int kPositionColumnWidth = 126;
constexpr int kProtectionColumnWidth = 104;
constexpr int kJogColumnWidth = 164;
constexpr int kServiceColumnWidth = 288;
constexpr int kJogButtonWidth = 74;
constexpr int kJogButtonHeight = 30;
constexpr int kServiceButtonWidth = 64;
constexpr int kServiceButtonHeight = 28;

constexpr double kMinimumCustomStepDeg = 0.01;
constexpr double kMaximumCustomStepDeg = 30.0;
constexpr double kDefaultCustomStepDeg = 2.5;

// Motor Configurator axis states (wire contract of the HAL telemetry).
enum AxisState : int {
    AxisUnknown = 0,
    AxisDisabled = 1,
    AxisReady = 2,
    AxisEnabled = 3,
    AxisFault = 4,
    AxisHoming = 5,
    AxisAutoHoming = 6,
    AxisHomingOffset = 7
};

bool isHomingState(int state) {
    return state == AxisHoming || state == AxisAutoHoming || state == AxisHomingOffset;
}

// Motor command controls require the HexaStudio<->HexaMotion bridge. Kept 1:1 with the shipping
// overlay, INCLUDING the temporary MKS bring-up policy:
// ===================== TEMPORARY (MKS bring-up) =========================
// The HAL panel is a direct-hardware panel and must be usable regardless of the SIM/REAL view mode
// and regardless of the HAL hard-error status, so the operator can bring up the Motor Configurator
// link. Only the bridge connection is required.
// Production gate policy to restore after MKS bring-up validation:
//   if (status.motion.isSimulated) return false;       // no motor commands in simulation
//   const QString &hal = status.motion.halStatus;
//   return hal == QLatin1String("Ok") || hal.startsWith(QLatin1String("Warning"));
// ========================================================================
bool halCommandsAllowed(const HmiRobotStatus& status) {
    return status.top.isConnected;
}

QLabel* makeColumnHeader(const QString& text, int width) {
    QLabel* label = new QLabel(text);
    label->setFixedWidth(width);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(Hexa::Styles::LabelBorderless.arg(
        Hexa::Colors::TextMuted, Hexa::Fonts::familyUI(), "10"));
    return label;
}
} // namespace

// ---------------------------------------------------------------------------
// Static text helpers (wire-contract state codes), 1:1 with the shipping overlay
// ---------------------------------------------------------------------------

QString HalPanel::stateText(int state) {
    switch (state) {
    case AxisUnknown:      return QStringLiteral("Unknown");
    case AxisDisabled:     return QStringLiteral("Disabled");
    case AxisReady:        return QStringLiteral("Ready");
    case AxisEnabled:      return QStringLiteral("Enabled");
    case AxisFault:        return QStringLiteral("Fault");
    case AxisHoming:       return QStringLiteral("Homing...");
    case AxisAutoHoming:   return QStringLiteral("Auto-homing...");
    case AxisHomingOffset: return QStringLiteral("Homing offset...");
    default:               return QStringLiteral("State %1").arg(state);
    }
}

QString HalPanel::stateShortText(int state) {
    switch (state) {
    case AxisUnknown:      return QStringLiteral("Unknown");
    case AxisDisabled:     return QStringLiteral("Disabled");
    case AxisReady:        return QStringLiteral("Ready");
    case AxisEnabled:      return QStringLiteral("Enabled");
    case AxisFault:        return QStringLiteral("Fault");
    case AxisHoming:       return QStringLiteral("Homing");
    case AxisAutoHoming:   return QStringLiteral("AutoHome");
    case AxisHomingOffset: return QStringLiteral("Offset");
    default:               return QStringLiteral("State %1").arg(state);
    }
}

QString HalPanel::positionText(double deg) {
    const QChar sign = deg >= 0.0 ? QLatin1Char('+') : QLatin1Char('-');
    return QStringLiteral("%1%2°").arg(sign).arg(std::abs(deg), 6, 'f', 2, QLatin1Char('0'));
}

QString HalPanel::protectionText(int protectionCode) {
    return QStringLiteral("%1").arg(protectionCode, 3, 10, QLatin1Char('0'));
}

// ---------------------------------------------------------------------------

HalPanel::HalPanel(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setupUi();
    for (int i = 0; i < kAxisCount; ++i) createAxisRow(i);
    updateJogButtonLabels();

    m_staleTimer.setInterval(kStaleTimerPeriodMs);
    connect(&m_staleTimer, &QTimer::timeout, this, &HalPanel::refreshStaleRows);
    m_staleTimer.start();
}

// ---------------------------------------------------------------------------
// Feedback in
// ---------------------------------------------------------------------------

void HalPanel::setHalConfigCurrent(const HmiHalConfig& config) {
    m_currentHalConfig = config;
    if (m_currentHalConfig.axes.size() < kAxisCount) {
        m_currentHalConfig.axes.clear();
        for (int i = 0; i < kAxisCount; ++i) {
            HmiHalAxisConfig axis;
            axis.axisIndex = i;
            m_currentHalConfig.axes.append(axis);
        }
    }

    if (m_hostEdit != nullptr && !m_hostEdit->hasFocus()) {
        const QString host = m_currentHalConfig.transportIp.trimmed().isEmpty()
                                 ? QStringLiteral("127.0.0.1")
                                 : m_currentHalConfig.transportIp.trimmed();
        m_hostEdit->setText(host);
    }
    if (m_portSpin != nullptr && !m_portSpin->hasFocus()) {
        m_portSpin->setValue(m_currentHalConfig.transportPort > 0 ? m_currentHalConfig.transportPort
                                                                  : 30110);
    }
    updateConnectionIndicator(m_currentHalConfig.transportConnected);
    setOwnerIndicator(m_currentHalConfig.controlOwnerId);

    // Overall HomeAll sequence progress (read-only mirror of the backend supervisor status);
    // per-axis homing phase is shown on each axis row.
    if (m_homingStatusLabel != nullptr) {
        const HmiHalConfig& h = m_currentHalConfig;
        if (h.homingSequenceActive) {
            HexaWidgets::setBadge(m_homingStatusLabel,
                QStringLiteral("Homing: axis %1 (%2/%3) — %4")
                    .arg(h.homingCurrentAxisId).arg(h.homingCurrentIndex + 1)
                    .arg(h.homingAxisCount).arg(h.homingState),
                Hexa::Colors::BadgeActive, Hexa::Colors::BadgeFgLight);
        } else if (h.homingState == QLatin1String("Aborted")) {
            HexaWidgets::setBadge(m_homingStatusLabel, QStringLiteral("Homing: ABORTED"),
                Hexa::Colors::BadgeError, Hexa::Colors::BadgeFgLight);
        } else if (h.homingState == QLatin1String("Completed")) {
            HexaWidgets::setBadge(m_homingStatusLabel, QStringLiteral("Homing: completed"),
                Hexa::Colors::BadgeOk, Hexa::Colors::BadgeFgLight);
        } else {
            HexaWidgets::setBadge(m_homingStatusLabel, QStringLiteral("Homing: —"),
                Hexa::Colors::BadgeNeutralBg, Hexa::Colors::BadgeNeutralFg);
        }
        m_homingStatusLabel->setToolTip(h.homingDiagnostic);
    }

    // Last backend rejection (audit F-01): a rejected command must be visible to the operator.
    if (m_backendErrorLabel != nullptr) {
        const QString& err = m_currentHalConfig.lastIpcError;
        if (err.isEmpty()) {
            HexaWidgets::setBadge(m_backendErrorLabel, QStringLiteral("Backend: —"),
                Hexa::Colors::BadgeNeutralBg, Hexa::Colors::BadgeNeutralFg);
        } else {
            HexaWidgets::setBadge(m_backendErrorLabel, QStringLiteral("Backend: rejected"),
                Hexa::Colors::BadgeError, Hexa::Colors::BadgeFgLight);
        }
        m_backendErrorLabel->setToolTip(err);
    }

    for (int i = 0; i < kAxisCount; ++i) updateAxisRow(i);
}

void HalPanel::setTcpSimulatorRunning(bool running) {
    if (m_startTcpSimButton == nullptr || m_stopTcpSimButton == nullptr) return;
    m_startTcpSimButton->setEnabled(!running);
    m_stopTcpSimButton->setEnabled(running);
    m_startTcpSimButton->setText(running ? QStringLiteral("TCP SIM RUNNING")
                                         : QStringLiteral("START TCP SIM"));
    m_startTcpSimButton->setToolTip(running
        ? QStringLiteral("The local TCP HAL simulator process is already running")
        : QStringLiteral("Launch the local TCP HAL simulator process"));
    m_stopTcpSimButton->setToolTip(running
        ? QStringLiteral("Stop the local TCP HAL simulator process")
        : QStringLiteral("The local TCP HAL simulator process is not running"));
}

void HalPanel::onRobotStateChanged(const HmiRobotStatus& status) {
    m_lastStatus = status;

    if (m_backend == QLatin1String("sim")) {
        // Sim backend: the "HAL link" is the built-in simulator - always present, never a
        // transport. The wire halStatus reflects the (absent) realtime link and would read
        // "Not Connected", which is misleading here.
        HexaWidgets::setBadge(m_halStatusLabel, QStringLiteral("HAL: Simulation"),
                              Hexa::Colors::BadgeOk, Hexa::Colors::BadgeFgLight);
    } else if (!status.motion.halStatus.isEmpty()) {
        setHalStatusIndicator(status.motion.halStatus);
    }
    for (int i = 0; i < kAxisCount; ++i) updateAxisRow(i);

    setCommandControlsEnabled(halCommandsAllowed(status));
    // E-STOP is gated only on the bridge connection, never on the HAL link state.
    m_eStopAllButton->setEnabled(status.top.isConnected);

    // Decision P2: warn when the panel can move real hardware while the view says Simulation.
    // With the SIM backend selected there is no real hardware behind the panel - no warning.
    if (m_simWarningLabel != nullptr) {
        m_simWarningLabel->setVisible(status.top.isConnected && status.motion.isSimulated
                                      && m_backend != QLatin1String("sim"));
    }

    if (m_jogEnableButton != nullptr) {
        m_jogEnableButton->setChecked(m_jogArmed);
        m_jogEnableButton->setText(m_jogArmed ? QStringLiteral("DISABLE JOG")
                                              : QStringLiteral("ENABLE JOG"));
        m_jogEnableButton->setEnabled(status.top.isConnected);
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void HalPanel::setupUi() {
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setAlignment(Qt::AlignCenter);
    outerLayout->setContentsMargins(40, 40, 40, 40);

    QFrame* frame = new QFrame();
    QString frameStyle = Hexa::Styles::PanelMain;
    frameStyle.replace("border-radius: " + QString::number(Hexa::Dim::Radius) + "px;",
                       "border-radius: 12px;");
    frame->setStyleSheet(frameStyle);
    frame->setFixedSize(kFrameWidthPx, kFrameHeightPx);

    QVBoxLayout* root = new QVBoxLayout(frame);
    root->setContentsMargins(16, 14, 16, 12);
    root->setSpacing(10);

    QLabel* lblTitle = new QLabel("HAL RUNTIME");
    lblTitle->setStyleSheet(Hexa::Styles::LabelHeaderSimple + "font-size: 16px;");
    root->addWidget(lblTitle);
    root->addWidget(HexaWidgets::createSeparatorH());

    // Body: narrow left sidebar (Connection + Jog Step) | right working column (Status, Global
    // Actions, Axes) - the axes get the width.
    QHBoxLayout* body = new QHBoxLayout();
    body->setSpacing(10);

    QVBoxLayout* leftSidebar = new QVBoxLayout();
    leftSidebar->setSpacing(10);
    leftSidebar->addWidget(setupBackendBlock());
    m_connectionCard = setupConnectionBlock();
    leftSidebar->addWidget(m_connectionCard);
    leftSidebar->addWidget(setupJogStepBlock());
    leftSidebar->addStretch();
    updateBackendView();   // default is "sim": transport card hidden, SIM note shown
    QWidget* leftHost = new QWidget(frame);
    leftHost->setAttribute(Qt::WA_TranslucentBackground);
    leftHost->setFixedWidth(kSidebarWidthPx);
    leftHost->setLayout(leftSidebar);
    body->addWidget(leftHost);

    QVBoxLayout* rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(10);
    rightColumn->addWidget(setupStatusBlock());
    rightColumn->addWidget(setupGlobalActionsBlock());
    rightColumn->addWidget(setupAxisListBlock(), 1);
    body->addLayout(rightColumn, 1);
    root->addLayout(body, 1);

    QHBoxLayout* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    QPushButton* closeButton = HexaWidgets::createButtonStd("CLOSE", frame, 120, 36);
    connect(closeButton, &QPushButton::clicked, this, &HalPanel::closeRequested);
    btnRow->addWidget(closeButton);
    root->addLayout(btnRow);

    outerLayout->addWidget(frame);
}

QFrame* HalPanel::setupStatusBlock() {
    QVBoxLayout* content = nullptr;
    QFrame* card = overlay::createCard(QStringLiteral("HAL DRIVER STATUS"), &content);

    QHBoxLayout* layout = new QHBoxLayout();
    layout->setSpacing(10);

    m_halStatusLabel = HexaWidgets::createBadge(QStringLiteral("HAL: Not Connected"), 170,
                                                Hexa::Colors::BadgeError, Hexa::Colors::BadgeFgLight);
    layout->addWidget(m_halStatusLabel);

    m_simWarningLabel = HexaWidgets::createBadge(QStringLiteral("⚠ HAL drives REAL hardware"),
                                                 200, Hexa::Colors::BadgeWarn,
                                                 Hexa::Colors::BadgeFgDark);
    m_simWarningLabel->setToolTip(QStringLiteral(
        "HexaStudio is in SIMULATION view, but HAL-panel jog / home / set-zero commands are sent "
        "directly to the real Motor Configurator and will physically move the robot."));
    m_simWarningLabel->setVisible(false);
    layout->addWidget(m_simWarningLabel);

    m_ownerLabel = HexaWidgets::createBadge(QStringLiteral("Control Owner: Unknown"), 200,
                                            Hexa::Colors::BadgeNeutralBg,
                                            Hexa::Colors::BadgeNeutralFg);
    layout->addWidget(m_ownerLabel);

    m_homingStatusLabel = HexaWidgets::createBadge(QStringLiteral("Homing: —"), 200,
                                                   Hexa::Colors::BadgeNeutralBg,
                                                   Hexa::Colors::BadgeNeutralFg);
    layout->addWidget(m_homingStatusLabel);

    m_backendErrorLabel = HexaWidgets::createBadge(QStringLiteral("Backend: —"), 170,
                                                   Hexa::Colors::BadgeNeutralBg,
                                                   Hexa::Colors::BadgeNeutralFg);
    layout->addWidget(m_backendErrorLabel);

    layout->addStretch();

    m_statusBarLabel = new QLabel(QStringLiteral("Waiting for HAL data..."));
    m_statusBarLabel->setStyleSheet(Hexa::Styles::LabelBorderless.arg(
        Hexa::Colors::TextMuted, Hexa::Fonts::familyUI(), "10"));
    m_statusBarLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(m_statusBarLabel);

    content->addLayout(layout);
    return card;
}

QFrame* HalPanel::setupBackendBlock() {
    // Realtime backend selector (moved here from Settings, boss directive 2026-07-06): the HAL
    // panel is the place where the backend choice has visible consequences, so the dropdown and
    // the cards it gates live together. Selection rides the persisted controller config; the
    // controller applies sim<->mks live and reports the result in the status line.
    QVBoxLayout* content = nullptr;
    QFrame* card = overlay::createCard(QStringLiteral("BACKEND"), &content);

    m_backendCombo = HexaWidgets::createComboBox();
    m_backendCombo->setObjectName(QStringLiteral("comboHalBackend"));
    m_backendCombo->setMinimumHeight(32);
    m_backendCombo->addItem(QStringLiteral("Simulation (built-in)"), QStringLiteral("sim"));
    m_backendCombo->addItem(QStringLiteral("MKS TCP"), QStringLiteral("mks_tcp"));
    m_backendCombo->addItem(QStringLiteral("UDP HAL"), QStringLiteral("udp"));
    connect(m_backendCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const QString selected = m_backendCombo->currentData().toString();
        if (selected == m_backend) return;
        m_backend = selected;
        updateBackendView();
        emit realtimeBackendSelected(selected);
    });
    content->addWidget(m_backendCombo);

    m_backendNoteLabel = new QLabel();
    m_backendNoteLabel->setWordWrap(true);
    m_backendNoteLabel->setStyleSheet(Hexa::Styles::LabelBorderless.arg(
        Hexa::Colors::TextMuted, Hexa::Fonts::familyUI(), "10"));
    content->addWidget(m_backendNoteLabel);

    return card;
}

void HalPanel::updateBackendView() {
    const bool mks = (m_backend == QLatin1String("mks_tcp"));
    if (m_connectionCard != nullptr) {
        m_connectionCard->setVisible(mks);   // transport endpoint is an MKS-only concern
    }
    if (m_backendNoteLabel != nullptr) {
        if (m_backend == QLatin1String("sim")) {
            m_backendNoteLabel->setText(QStringLiteral(
                "Built-in simulation: no transport, motors enabled - jog and set-zero work "
                "immediately."));
        } else if (mks) {
            m_backendNoteLabel->setText(QStringLiteral(
                "MKS motor configurator over TCP. Use CONNECT below to open the transport."));
        } else {
            m_backendNoteLabel->setText(QStringLiteral(
                "UDP HAL: configuration UI is not available yet. The selection is persisted and "
                "applies on the controller's next start."));
        }
    }
}

void HalPanel::setRealtimeBackend(const QString& backend) {
    const QString normalized = backend.isEmpty() ? QStringLiteral("sim") : backend;
    if (normalized == m_backend) return;
    m_backend = normalized;
    if (m_backendCombo != nullptr) {
        const QSignalBlocker blocker(m_backendCombo);   // an echo must not re-emit the selection
        const int idx = m_backendCombo->findData(normalized);
        m_backendCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    updateBackendView();
}

QFrame* HalPanel::setupConnectionBlock() {
    QVBoxLayout* content = nullptr;
    QFrame* card = overlay::createCard(QStringLiteral("CONNECTION"), &content);

    QHBoxLayout* hostRow = new QHBoxLayout();
    QLabel* hostLabel = HexaWidgets::createLabelText("Host");
    hostLabel->setFixedWidth(36);
    hostRow->addWidget(hostLabel);
    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"));
    m_hostEdit->setStyleSheet(overlay::fieldStyle());
    hostRow->addWidget(m_hostEdit, 1);
    content->addLayout(hostRow);

    QHBoxLayout* portRow = new QHBoxLayout();
    QLabel* portLabel = HexaWidgets::createLabelText("Port");
    portLabel->setFixedWidth(36);
    portRow->addWidget(portLabel);
    m_portSpin = new QSpinBox();
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(30110);
    m_portSpin->setStyleSheet(overlay::fieldStyle());
    m_portSpin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    portRow->addWidget(m_portSpin, 1);
    content->addLayout(portRow);

    m_connectButton = HexaWidgets::createButtonStd(QStringLiteral("CONNECT"), this);
    m_connectButton->setObjectName(QStringLiteral("btnHalConnect"));
    m_disconnectButton = HexaWidgets::createButtonStd(QStringLiteral("DISCONNECT"), this);
    m_startTcpSimButton = HexaWidgets::createButtonStd(QStringLiteral("START TCP SIM"), this);
    m_stopTcpSimButton = HexaWidgets::createButtonStd(QStringLiteral("STOP TCP SIM"), this);
    m_startTcpSimButton->setToolTip(QStringLiteral("Launch the local TCP HAL simulator process"));
    m_stopTcpSimButton->setToolTip(QStringLiteral("Stop the local TCP HAL simulator process"));
    for (QPushButton* b : {m_connectButton, m_disconnectButton, m_startTcpSimButton,
                           m_stopTcpSimButton}) {
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setMaximumWidth(QWIDGETSIZE_MAX);
        content->addWidget(b);
    }

    m_connectionStateLabel = HexaWidgets::createBadge(QStringLiteral("Disconnected"), 0,
                                                      Hexa::Colors::BadgeNeutralBg,
                                                      Hexa::Colors::BadgeNeutralFg);
    m_connectionStateLabel->setAlignment(Qt::AlignCenter);
    content->addWidget(m_connectionStateLabel);

    connect(m_connectButton, &QPushButton::clicked, this, [this]() {
        QString host = m_hostEdit->text().trimmed();
        if (host.isEmpty()) host = QStringLiteral("127.0.0.1");
        emit halConnectRequested(host, m_portSpin->value());
    });
    connect(m_disconnectButton, &QPushButton::clicked, this, &HalPanel::halDisconnectRequested);
    connect(m_startTcpSimButton, &QPushButton::clicked,
            this, &HalPanel::tcpHalSimulatorLaunchRequested);
    connect(m_stopTcpSimButton, &QPushButton::clicked,
            this, &HalPanel::tcpHalSimulatorStopRequested);
    return card;
}

QFrame* HalPanel::setupJogStepBlock() {
    QVBoxLayout* content = nullptr;
    QFrame* card = overlay::createCard(QStringLiteral("JOG STEP"), &content);

    content->addWidget(overlay::createCaption(QStringLiteral("Discrete jog step per click.")));

    m_step1Button = HexaWidgets::createButtonStd(QStringLiteral("1°"), this);
    m_step10Button = HexaWidgets::createButtonStd(QStringLiteral("10°"), this);
    m_stepCustomButton = HexaWidgets::createButtonStd(QStringLiteral("CUSTOM"), this);
    QHBoxLayout* stepRow = new QHBoxLayout();
    stepRow->setSpacing(6);
    for (QPushButton* button : {m_step1Button, m_step10Button, m_stepCustomButton}) {
        button->setCheckable(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        stepRow->addWidget(button);
    }
    m_step1Button->setChecked(true);
    content->addLayout(stepRow);

    auto selectStep = [this](QPushButton* selected) {
        m_step1Button->setChecked(selected == m_step1Button);
        m_step10Button->setChecked(selected == m_step10Button);
        m_stepCustomButton->setChecked(selected == m_stepCustomButton);
        if (m_customStepSpin != nullptr) {
            m_customStepSpin->setEnabled(selected == m_stepCustomButton);
        }
        updateJogButtonLabels();
    };
    connect(m_step1Button, &QPushButton::clicked, this,
            [selectStep, this]() { selectStep(m_step1Button); });
    connect(m_step10Button, &QPushButton::clicked, this,
            [selectStep, this]() { selectStep(m_step10Button); });
    connect(m_stepCustomButton, &QPushButton::clicked, this,
            [selectStep, this]() { selectStep(m_stepCustomButton); });

    QHBoxLayout* customRow = new QHBoxLayout();
    QLabel* customLabel = HexaWidgets::createLabelText("Value");
    customRow->addWidget(customLabel);
    m_customStepSpin = new QDoubleSpinBox();
    m_customStepSpin->setRange(kMinimumCustomStepDeg, kMaximumCustomStepDeg);
    m_customStepSpin->setDecimals(2);
    m_customStepSpin->setSingleStep(0.25);
    m_customStepSpin->setValue(kDefaultCustomStepDeg);
    m_customStepSpin->setSuffix(QStringLiteral(" °"));
    m_customStepSpin->setAlignment(Qt::AlignCenter);
    m_customStepSpin->setEnabled(false);
    m_customStepSpin->setStyleSheet(overlay::fieldStyle());
    m_customStepSpin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    connect(m_customStepSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { updateJogButtonLabels(); });
    customRow->addWidget(m_customStepSpin, 1);
    content->addLayout(customRow);
    return card;
}

QFrame* HalPanel::setupGlobalActionsBlock() {
    QVBoxLayout* content = nullptr;
    QFrame* card = overlay::createCard(QStringLiteral("GLOBAL ACTIONS"), &content);

    QHBoxLayout* layout = new QHBoxLayout();
    layout->setSpacing(8);

    m_enableAllButton = HexaWidgets::createButtonStd(QStringLiteral("ENABLE ALL"), this);
    m_disableAllButton = HexaWidgets::createButtonStd(QStringLiteral("DISABLE ALL"), this);
    m_homeAllButton = HexaWidgets::createButtonStd(QStringLiteral("HOME ALL 1→6"), this);
    m_setZeroAllButton = HexaWidgets::createButtonStd(QStringLiteral("SET ZERO ALL"), this);
    m_clearErrorsButton = HexaWidgets::createButtonStd(QStringLiteral("CLEAR ERRORS"), this);
    m_clearErrorsButton->setToolTip(
        QStringLiteral("Clear latched controller/drive errors (all axes)"));

    // Dedicated jog-arm toggle, OWNED by this panel (independent of the jog panel; works in SIM).
    m_jogEnableButton = HexaWidgets::createButtonStd(QStringLiteral("ENABLE JOG"), this);
    m_jogEnableButton->setObjectName(QStringLiteral("btnHalJogArm"));
    m_jogEnableButton->setCheckable(true);

    // E-STOP: the only red control, large and prominent.
    m_eStopAllButton = HexaWidgets::createButtonDanger(QStringLiteral("E-STOP ALL"), this, 0, 48);

    layout->addWidget(m_enableAllButton);
    layout->addWidget(m_disableAllButton);
    layout->addWidget(m_homeAllButton);
    layout->addWidget(m_setZeroAllButton);
    layout->addWidget(m_clearErrorsButton);
    layout->addWidget(m_jogEnableButton);
    layout->addSpacing(18);
    layout->addWidget(m_eStopAllButton, 1);

    connect(m_enableAllButton, &QPushButton::clicked, this, [this]() {
        HmiHalConfig cmd = m_currentHalConfig;
        for (HmiHalAxisConfig& axis : cmd.axes) axis.motorEnabled = true;
        emit applyHalRequested(cmd);
        setStatusMessage(QStringLiteral("Enable ALL submitted."));
    });
    connect(m_disableAllButton, &QPushButton::clicked, this, [this]() {
        HmiHalConfig cmd = m_currentHalConfig;
        for (HmiHalAxisConfig& axis : cmd.axes) axis.motorEnabled = false;
        emit applyHalRequested(cmd);
        setStatusMessage(QStringLiteral("Disable ALL submitted."));
    });
    connect(m_homeAllButton, &QPushButton::clicked, this, [this]() {
        emit homingRequested(-1);
        setStatusMessage(QStringLiteral("HomeAllSequential submitted for axes 1→6."));
    });
    connect(m_setZeroAllButton, &QPushButton::clicked, this, [this]() {
        // One atomic SetZeroAll command (a per-axis loop would collapse in the one-shot command
        // field before the next network snapshot - only the last axis would zero).
        emit setZeroAllRequested();
        setStatusMessage(QStringLiteral("Set Zero ALL submitted."));
    });
    connect(m_clearErrorsButton, &QPushButton::clicked, this, [this]() {
        emit clearErrorsRequested();
        setStatusMessage(QStringLiteral("Clear Errors submitted (all axes)."));
    });
    connect(m_jogEnableButton, &QPushButton::clicked, this, [this]() {
        setJogArmed(!m_jogArmed);
    });
    connect(m_eStopAllButton, &QPushButton::clicked, this, [this]() {
        emit eStopRequested();
        setStatusMessage(QStringLiteral("E-STOP ALL submitted."));
    });
    content->addLayout(layout);
    return card;
}

QFrame* HalPanel::setupAxisListBlock() {
    QVBoxLayout* content = nullptr;
    QFrame* card = overlay::createCard(QStringLiteral("AXES"), &content);

    QWidget* header = new QWidget(card);
    header->setAttribute(Qt::WA_TranslucentBackground);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 0, 8, 0);
    headerLayout->setSpacing(8);
    headerLayout->addWidget(makeColumnHeader(QStringLiteral("AXIS"), kAxisColumnWidth));
    headerLayout->addWidget(makeColumnHeader(QStringLiteral("STATE"), kStateColumnWidth));
    headerLayout->addWidget(makeColumnHeader(QStringLiteral("POSITION"), kPositionColumnWidth));
    headerLayout->addWidget(makeColumnHeader(QStringLiteral("PROTECTION"), kProtectionColumnWidth));
    headerLayout->addWidget(makeColumnHeader(QStringLiteral("JOG"), kJogColumnWidth));
    headerLayout->addWidget(makeColumnHeader(QStringLiteral("SERVICE"), kServiceColumnWidth));
    headerLayout->addStretch();
    content->addWidget(header);

    QScrollArea* scrollArea = new QScrollArea(card);
    scrollArea->setWidgetResizable(true);
    // Columns are fixed-width and sized to fit the frame: only vertical scrolling is meaningful.
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        " QScrollArea > QWidget > QWidget { background: transparent; }");
    QWidget* container = new QWidget(scrollArea);
    m_axisListLayout = new QVBoxLayout(container);
    m_axisListLayout->setSpacing(6);
    m_axisListLayout->addStretch();   // axis rows are inserted before the stretch
    scrollArea->setWidget(container);
    content->addWidget(scrollArea);
    return card;
}

void HalPanel::createAxisRow(int axisIndex) {
    const int displayId = axisIndex + 1;

    AxisRow row;
    row.container = new QWidget();
    row.container->setObjectName(QStringLiteral("axisRow_%1").arg(displayId));
    row.container->setStyleSheet(QStringLiteral(
        "#axisRow_%1 { background-color: rgba(0,0,0,0.30); border: 1px solid rgba(69, 162, 158, 0.35);"
        " border-radius: 4px; }"
        "#axisRow_%1:hover { border: 1px solid %2; }").arg(displayId).arg(Hexa::Colors::Primary));

    QHBoxLayout* rowLayout = new QHBoxLayout(row.container);
    rowLayout->setContentsMargins(8, 8, 8, 8);
    rowLayout->setSpacing(8);

    row.title = new QLabel(QStringLiteral("A%1").arg(displayId), row.container);
    row.title->setAlignment(Qt::AlignCenter);
    row.title->setStyleSheet(Hexa::Styles::LabelBorderless.arg(
        Hexa::Colors::Primary, Hexa::Fonts::familyUI(), "15") + "font-weight: bold;");
    row.title->setFixedWidth(kAxisColumnWidth);

    row.state = HexaWidgets::createBadge(QStringLiteral("Unknown"), 0,
                                         Hexa::Colors::BadgeNeutralBg, Hexa::Colors::BadgeNeutralFg);
    row.state->setAlignment(Qt::AlignCenter);
    row.state->setFixedWidth(kStateColumnWidth);

    row.position = new QLabel(positionText(0.0), row.container);
    row.position->setAlignment(Qt::AlignCenter);
    row.position->setFixedWidth(kPositionColumnWidth);
    row.position->setStyleSheet(Hexa::Styles::LabelBorderless.arg(
        Hexa::Colors::TextMain, Hexa::Fonts::familyMono(), "13"));

    row.protection = new QLabel(protectionText(0), row.container);
    row.protection->setAlignment(Qt::AlignCenter);
    row.protection->setFixedWidth(kProtectionColumnWidth);
    row.protection->setStyleSheet(Hexa::Styles::LabelBorderless.arg(
        Hexa::Colors::TextMuted, Hexa::Fonts::familyMono(), "13"));

    QWidget* jogBlock = new QWidget(row.container);
    jogBlock->setAttribute(Qt::WA_TranslucentBackground);
    jogBlock->setFixedWidth(kJogColumnWidth);
    QHBoxLayout* jogLayout = new QHBoxLayout(jogBlock);
    jogLayout->setContentsMargins(0, 0, 0, 0);
    jogLayout->setSpacing(8);
    jogLayout->setAlignment(Qt::AlignCenter);
    row.jogMinusButton = HexaWidgets::createButtonStd(QStringLiteral("-1°"), jogBlock,
                                                      kJogButtonWidth, kJogButtonHeight);
    row.jogMinusButton->setObjectName(QStringLiteral("halJogMinus_%1").arg(axisIndex));
    row.jogPlusButton = HexaWidgets::createButtonStd(QStringLiteral("+1°"), jogBlock,
                                                     kJogButtonWidth, kJogButtonHeight);
    row.jogPlusButton->setObjectName(QStringLiteral("halJogPlus_%1").arg(axisIndex));
    jogLayout->addWidget(row.jogMinusButton);
    jogLayout->addWidget(row.jogPlusButton);

    QWidget* serviceBlock = new QWidget(row.container);
    serviceBlock->setAttribute(Qt::WA_TranslucentBackground);
    serviceBlock->setFixedWidth(kServiceColumnWidth);
    QHBoxLayout* serviceLayout = new QHBoxLayout(serviceBlock);
    serviceLayout->setContentsMargins(0, 0, 0, 0);
    serviceLayout->setSpacing(8);
    serviceLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    row.enableButton = HexaWidgets::createButtonStd(QStringLiteral("En"), serviceBlock,
                                                    kServiceButtonWidth, kServiceButtonHeight);
    row.disableButton = HexaWidgets::createButtonStd(QStringLiteral("Dis"), serviceBlock,
                                                     kServiceButtonWidth, kServiceButtonHeight);
    row.homeButton = HexaWidgets::createButtonStd(QStringLiteral("Home"), serviceBlock,
                                                  kServiceButtonWidth, kServiceButtonHeight);
    row.setZeroButton = HexaWidgets::createButtonStd(QStringLiteral("Zero"), serviceBlock,
                                                     kServiceButtonWidth, kServiceButtonHeight);
    row.enableButton->setToolTip(QStringLiteral("Enable axis motor"));
    row.disableButton->setToolTip(QStringLiteral("Disable axis motor"));
    row.homeButton->setToolTip(QStringLiteral("Start homing for this axis"));
    row.setZeroButton->setToolTip(QStringLiteral("Set current axis position to zero"));
    serviceLayout->addWidget(row.enableButton);
    serviceLayout->addWidget(row.disableButton);
    serviceLayout->addWidget(row.homeButton);
    serviceLayout->addWidget(row.setZeroButton);
    serviceLayout->addStretch();

    rowLayout->addWidget(row.title);
    rowLayout->addWidget(row.state);
    rowLayout->addWidget(row.position);
    rowLayout->addWidget(row.protection);
    rowLayout->addWidget(jogBlock);
    rowLayout->addWidget(serviceBlock);
    rowLayout->addStretch();

    // Enable/Disable travel as a config-apply command copy (same seam as the shipping overlay).
    connect(row.enableButton, &QPushButton::clicked, this, [this, axisIndex]() {
        HmiHalConfig cmd = m_currentHalConfig;
        if (axisIndex < cmd.axes.size()) cmd.axes[axisIndex].motorEnabled = true;
        emit applyHalRequested(cmd);
        setStatusMessage(QStringLiteral("Enable submitted for Axis %1").arg(axisIndex + 1));
    });
    connect(row.disableButton, &QPushButton::clicked, this, [this, axisIndex]() {
        HmiHalConfig cmd = m_currentHalConfig;
        if (axisIndex < cmd.axes.size()) cmd.axes[axisIndex].motorEnabled = false;
        emit applyHalRequested(cmd);
        setStatusMessage(QStringLiteral("Disable submitted for Axis %1").arg(axisIndex + 1));
    });
    connect(row.homeButton, &QPushButton::clicked, this, [this, axisIndex]() {
        emit homingRequested(axisIndex);
        setStatusMessage(QStringLiteral("Home submitted for Axis %1").arg(axisIndex + 1));
    });
    connect(row.setZeroButton, &QPushButton::clicked, this, [this, axisIndex]() {
        emit setZeroRequested(axisIndex);
        setStatusMessage(QStringLiteral("Set Zero submitted for Axis %1").arg(axisIndex + 1));
    });
    connect(row.jogMinusButton, &QPushButton::clicked, this, [this, axisIndex]() {
        sendJogStep(axisIndex, -selectedJogStepDeg());
    });
    connect(row.jogPlusButton, &QPushButton::clicked, this, [this, axisIndex]() {
        sendJogStep(axisIndex, selectedJogStepDeg());
    });

    m_rows.insert(axisIndex, row);
    const int insertAt = m_axisListLayout->count() - 1;   // before the stretch
    m_axisListLayout->insertWidget(insertAt, row.container);
}

void HalPanel::updateAxisRow(int axisIndex) {
    if (!m_rows.contains(axisIndex)) return;
    AxisRow& row = m_rows[axisIndex];
    row.lastSeenMs = QDateTime::currentMSecsSinceEpoch();

    // Position source (Convention B, uniform across backends — the sim backend's virtual MC
    // publishes through the same real-driver path, REQ_sim_backend_virtual_mc): in SIM view the
    // MC's live position is realHardwareJoints (the ghost source; plannedJoints is the honest
    // COMMAND since REQ_ghost_semantics_convention_B, and displaying it here showed the command,
    // not the MC); in REAL view the active feedback IS the MC.
    const bool simBackend = (m_backend == QLatin1String("sim"));
    const QVector<double>& mcJoints = m_lastStatus.motion.isSimulated
                                          ? m_lastStatus.motion.realHardwareJoints
                                          : m_lastStatus.motion.actualJoints;
    const double position = axisIndex < mcJoints.size() ? mcJoints[axisIndex] : 0.0;

    int state = AxisUnknown;
    int protection = 0;
    if (axisIndex < m_currentHalConfig.axes.size()) {
        if (simBackend) {
            // No MC wire state exists in sim: the enable state IS the HAL motorEnabled flag
            // (lastStatus carries HalStatus codes here, not the MC AxisState contract).
            state = m_currentHalConfig.axes[axisIndex].motorEnabled ? AxisEnabled : AxisDisabled;
        } else {
            state = m_currentHalConfig.axes[axisIndex].lastStatus;
            protection = m_currentHalConfig.axes[axisIndex].lastErrorCode;
        }
    }

    row.title->setText(QStringLiteral("A%1").arg(axisIndex + 1));
    row.title->setStyleSheet(Hexa::Styles::LabelBorderless.arg(
        Hexa::Colors::Primary, Hexa::Fonts::familyUI(), "15") + "font-weight: bold;");
    row.position->setText(positionText(position));
    row.protection->setText(protectionText(protection));
    row.state->setToolTip(stateText(state));

    // Badge palette by state: fault/protection = error, homing = warn, enabled = ok, else neutral.
    if (state == AxisFault || protection != 0) {
        HexaWidgets::setBadge(row.state, stateShortText(state),
                              Hexa::Colors::BadgeError, Hexa::Colors::BadgeFgLight);
    } else if (isHomingState(state)) {
        HexaWidgets::setBadge(row.state, stateShortText(state),
                              Hexa::Colors::BadgeWarn, Hexa::Colors::BadgeFgDark);
    } else if (state == AxisEnabled) {
        HexaWidgets::setBadge(row.state, stateShortText(state),
                              Hexa::Colors::BadgeOk, Hexa::Colors::BadgeFgLight);
    } else {
        HexaWidgets::setBadge(row.state, stateShortText(state),
                              Hexa::Colors::BadgeNeutralBg, Hexa::Colors::BadgeNeutralFg);
    }

    const bool homing = isHomingState(state);
    const bool axisFault = (state == AxisFault || protection != 0);
    // Gate on the panel's OWN jog arm, not the jog panel's shared jogEnabled.
    const bool jogEnabled = halCommandsAllowed(m_lastStatus) && m_jogArmed && !homing && !axisFault;
    row.jogMinusButton->setEnabled(jogEnabled);
    row.jogPlusButton->setEnabled(jogEnabled);
}

// ---------------------------------------------------------------------------
// Jog step selection
// ---------------------------------------------------------------------------

double HalPanel::selectedJogStepDeg() const {
    if (m_step10Button != nullptr && m_step10Button->isChecked()) return 10.0;
    if (m_stepCustomButton != nullptr && m_stepCustomButton->isChecked() &&
        m_customStepSpin != nullptr) {
        return std::clamp(m_customStepSpin->value(), kMinimumCustomStepDeg, kMaximumCustomStepDeg);
    }
    return 1.0;
}

void HalPanel::updateJogButtonLabels() {
    const double step = selectedJogStepDeg();
    const bool customSelected = m_stepCustomButton != nullptr && m_stepCustomButton->isChecked();
    const QString minusText = customSelected
        ? QStringLiteral("-C")
        : QStringLiteral("-%1°").arg(step, 0, 'f', (step == 10.0 || step == 1.0) ? 0 : 2);
    const QString plusText = customSelected
        ? QStringLiteral("+C")
        : QStringLiteral("+%1°").arg(step, 0, 'f', (step == 10.0 || step == 1.0) ? 0 : 2);
    const QString tooltipSuffix = QStringLiteral("Selected step: %1°").arg(step, 0, 'f', 2);

    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        AxisRow& row = it.value();
        row.jogMinusButton->setText(minusText);
        row.jogMinusButton->setToolTip(
            QStringLiteral("Move this axis by one negative selected step. %1").arg(tooltipSuffix));
        row.jogPlusButton->setText(plusText);
        row.jogPlusButton->setToolTip(
            QStringLiteral("Move this axis by one positive selected step. %1").arg(tooltipSuffix));
    }
}

// Every blocked jog names its reason on the status line - silent refusals are prohibited.
void HalPanel::sendJogStep(int axisIndex, double signedStepDeg) {
    if (axisIndex < 0 || axisIndex >= kAxisCount) {
        setStatusMessage(QStringLiteral("Jog blocked: invalid axis index."));
        return;
    }
    if (!halCommandsAllowed(m_lastStatus)) {
        setStatusMessage(QStringLiteral("Jog blocked: controller bridge is disconnected."));
        return;
    }
    if (!m_jogArmed) {
        setStatusMessage(QStringLiteral("Jog blocked: press ENABLE JOG on the HAL panel first."));
        return;
    }
    int state = AxisUnknown;
    int protection = 0;
    if (axisIndex < m_currentHalConfig.axes.size()) {
        state = m_currentHalConfig.axes[axisIndex].lastStatus;
        protection = m_currentHalConfig.axes[axisIndex].lastErrorCode;
    }
    if (isHomingState(state)) {
        setStatusMessage(QStringLiteral("Jog blocked: Axis %1 is homing.").arg(axisIndex + 1));
        return;
    }
    if (state == AxisFault || protection != 0) {
        setStatusMessage(QStringLiteral("Jog blocked: Axis %1 is faulted.").arg(axisIndex + 1));
        return;
    }

    emit halJogRequested(axisIndex, signedStepDeg);
    setStatusMessage(QStringLiteral("Jog A%1 %2%3° submitted.")
                         .arg(axisIndex + 1)
                         .arg(signedStepDeg >= 0.0 ? QStringLiteral("+") : QString())
                         .arg(signedStepDeg, 0, 'f', 2));
}

// ---------------------------------------------------------------------------
// State helpers
// ---------------------------------------------------------------------------

void HalPanel::refreshStaleRows() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        AxisRow& row = it.value();
        const bool stale = row.lastSeenMs > 0 && (now - row.lastSeenMs) > kAxisStaleTimeoutMs;
        if (stale) {
            row.title->setText(QStringLiteral("A%1*").arg(it.key() + 1));
            row.title->setStyleSheet(Hexa::Styles::LabelBorderless.arg(
                Hexa::Colors::TextMuted, Hexa::Fonts::familyUI(), "15") + "font-weight: bold;");
        }
    }
}

void HalPanel::setCommandControlsEnabled(bool enabled) {
    m_enableAllButton->setEnabled(enabled);
    m_disableAllButton->setEnabled(enabled);
    m_homeAllButton->setEnabled(enabled);
    m_setZeroAllButton->setEnabled(enabled);
    m_clearErrorsButton->setEnabled(enabled);
    // E-Stop is intentionally NOT gated here - it must stay available whenever the bridge is up,
    // regardless of the HAL link state (see onRobotStateChanged).
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        setAxisCommandControlsEnabled(it.value(), enabled);
        if (enabled) updateAxisRow(it.key());
    }
}

void HalPanel::setAxisCommandControlsEnabled(AxisRow& row, bool enabled) {
    row.enableButton->setEnabled(enabled);
    row.disableButton->setEnabled(enabled);
    row.homeButton->setEnabled(enabled);
    row.setZeroButton->setEnabled(enabled);
    if (!enabled) {
        row.jogMinusButton->setEnabled(false);
        row.jogPlusButton->setEnabled(false);
    }
}

void HalPanel::setHalStatusIndicator(const QString& halStatus) {
    if (m_halStatusLabel == nullptr) return;
    if (halStatus == QLatin1String("Ok")) {
        HexaWidgets::setBadge(m_halStatusLabel, QStringLiteral("HAL: Connected"),
                              Hexa::Colors::BadgeOk, Hexa::Colors::BadgeFgLight);
    } else if (halStatus.contains(QLatin1String("Warning"))) {
        HexaWidgets::setBadge(m_halStatusLabel, QStringLiteral("HAL: %1").arg(halStatus),
                              Hexa::Colors::BadgeWarn, Hexa::Colors::BadgeFgDark);
    } else {
        HexaWidgets::setBadge(m_halStatusLabel, QStringLiteral("HAL: %1").arg(halStatus),
                              Hexa::Colors::BadgeError, Hexa::Colors::BadgeFgLight);
    }
}

void HalPanel::setOwnerIndicator(int ownerId) {
    if (m_ownerLabel == nullptr) return;
    if (ownerId == 1) {
        HexaWidgets::setBadge(m_ownerLabel, QStringLiteral("Control Owner: HexaMotion"),
                              Hexa::Colors::BadgeOk, Hexa::Colors::BadgeFgLight);
    } else if (ownerId == 0) {
        HexaWidgets::setBadge(m_ownerLabel, QStringLiteral("Control Owner: UI — TCP blocked"),
                              Hexa::Colors::BadgeWarn, Hexa::Colors::BadgeFgDark);
    } else {
        HexaWidgets::setBadge(m_ownerLabel, QStringLiteral("Control Owner: Unknown"),
                              Hexa::Colors::BadgeNeutralBg, Hexa::Colors::BadgeNeutralFg);
    }
}

void HalPanel::updateConnectionIndicator(bool connected) {
    if (m_connectionStateLabel != nullptr) {
        if (connected) {
            HexaWidgets::setBadge(m_connectionStateLabel, QStringLiteral("Connected"),
                                  Hexa::Colors::BadgeOk, Hexa::Colors::BadgeFgLight);
        } else {
            HexaWidgets::setBadge(m_connectionStateLabel, QStringLiteral("Disconnected"),
                                  Hexa::Colors::BadgeNeutralBg, Hexa::Colors::BadgeNeutralFg);
        }
    }
    if (m_connectButton != nullptr) m_connectButton->setEnabled(!connected);
    if (m_disconnectButton != nullptr) m_disconnectButton->setEnabled(connected);
}

// Local jog arm: the ONLY jog gate this panel owns. Arms/disarms the controller through the
// jogArmRequested intent (the shipping overlay called RobotService::setJogEnabled directly).
void HalPanel::setJogArmed(bool armed) {
    m_jogArmed = armed;
    emit jogArmRequested(armed);
    if (m_jogEnableButton != nullptr) {
        m_jogEnableButton->setChecked(armed);
        m_jogEnableButton->setText(armed ? QStringLiteral("DISABLE JOG")
                                         : QStringLiteral("ENABLE JOG"));
    }
    setStatusMessage(armed ? QStringLiteral("Jog armed (HAL panel).")
                           : QStringLiteral("Jog disarmed (HAL panel)."));
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) updateAxisRow(it.key());
}

void HalPanel::setStatusMessage(const QString& text) {
    if (m_statusBarLabel != nullptr) m_statusBarLabel->setText(text);
}

// ---------------------------------------------------------------------------
// QWidget overrides
// ---------------------------------------------------------------------------

void HalPanel::mousePressEvent(QMouseEvent* event) {
    event->accept();
}

void HalPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, kDimAlpha));
}

void HalPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    // Safety: leaving the HAL panel disarms its jog (re-engages the brake) so a stale local arm
    // can never linger after the operator moves on.
    if (m_jogArmed) {
        setJogArmed(false);
    }
}

} // namespace hexa
// --- END OF FILE: HexaStudio/hal_control/HalPanel.cpp ---
