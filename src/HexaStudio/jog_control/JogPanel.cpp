// --- START OF FILE: HexaStudio/jog_control/JogPanel.cpp ---
#include "JogPanel.h"
#include "FramePickerDialog.h"   // module-owned tool/base picker

#include "HexaTheme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFontMetrics>
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QTimer>

namespace hexa {

namespace {
// Absolute cap for the jog-button gate: even if "robot moving" never clears (lost telemetry, stuck
// drive), the direction buttons are force-released after this many ms. The real release is the
// motion-stopped signal (see updateState); this is only the safety net.
constexpr int kJogBlockMaxMs = 2000;
constexpr int kNoticeAutoHideMs = 4000;
// Jog-pad geometry (v0.4.0 industrial carbon): the framed centre cell is a DRO-style readout -
// full row height (boss approval of the carbon mockup 2026-07-04 supersedes the earlier 2/3-height
// request), near-black inset, axis label left, right-aligned mono value with a muted unit suffix.
// The cell width is computed from real font metrics over the worst-case contents (see
// createJogSection), so the minus/plus jog keys still get every remaining pixel of the row.
constexpr int kJogKeyMinHeightPx = 44;
constexpr int kJogValueFontPx = 16;        // DRO digits
constexpr int kJogUnitFontPx = 11;         // unit suffix (degree sign / mm), muted
constexpr int kJogAxisLabelFontPx = 12;    // axis name inside the cell
// Non-text chrome of the DRO cell, must match the cell layout in createJogSection:
// 8px side paddings (x2) + 4px item spacing (x2) + 1px borders (x2).
constexpr int kJogCellChromePx = 8 * 2 + 4 * 2 + 1 * 2;
// At-limit display band: displayJoints is the commanded target (not jittering telemetry), so half of
// the 0.01-deg display resolution is enough to catch "clamped exactly at the limit".
constexpr double kAxisLimitBandDeg = 0.05;

// Desired-value DRO cell style: near-black inset with the sharp instrument radius (the decorative
// violet identity stripe of v0.3.0 was removed with the boss-approved carbon redesign). At a joint
// limit the cell turns to the theme warning colour (same as JOG MOVING) so the blocked axis is
// visible at a glance. One warning colour across the HMI - no panel-local ambers.
QString valueBoxStyle(bool atLimit) {
    const QString bg = atLimit ? Hexa::Colors::WarningSoft : Hexa::Colors::CellInset;
    const QString border = atLimit ? Hexa::Colors::Warning : Hexa::Colors::Hairline;
    return QStringLiteral(
        "QFrame { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
        .arg(bg, border, QString::number(Hexa::Dim::RadiusCell));
}

// Colour-only label style: the font is set as a real QFont at the call site, so size hints and
// font metrics always match what is rendered (a stylesheet font does not feed either).
QString labelColorStyle(const QString& color) {
    return QStringLiteral("QLabel { border: none; background: transparent; color: %1; }").arg(color);
}
} // namespace

JogPanel::JogPanel(QWidget* parent) : QWidget(parent) {
    // Translucent by design: the panel paints no background of its own; the host window's colour shows
    // through. Both hosts (MainWindow and the bench) paint the application background colour - the
    // parent window colour is configured at integration, not here.
    setAttribute(Qt::WA_TranslucentBackground);

    // Jog is always discrete for safety - no continuous mode. (Fine calibration steps live on the
    // HAL panel - the calibration overlay and the jog calibration mode were cancelled by the boss.)
    m_jointStepOptions            = {"1.0", "5.0", "10.0"};
    m_cartStepOptions             = {"1.0", "10.0", "100"};   // translation (mm)
    m_cartOrientStepOptions       = {"1.0", "5.0", "15.0"};   // orientation (deg), index-aligned
    m_stepOptions = m_jointStepOptions;
    m_stepIndex = 0;
    m_axisLimits = QVector<AxisLimitRange>(6);
    m_axisAtLimit = QVector<bool>(6, false);

    setupUi();
    updateJogLabels(0);
    // The monitor base combo defaults to "JOINT", so the labels must start as A1..A6 too (the
    // shipping panel starts them Cartesian, which mislabels the joint values until the first
    // manual base change - fixed here).
    updateMonitorLabels(true);

    m_jogReleaseTimer = new QTimer(this);
    m_jogReleaseTimer->setSingleShot(true);
    connect(m_jogReleaseTimer, &QTimer::timeout, this, &JogPanel::unblockJog);

    m_noticeTimer = new QTimer(this);
    m_noticeTimer->setSingleShot(true);
    connect(m_noticeTimer, &QTimer::timeout, this, &JogPanel::onJogNoticeTimeout);

    refreshJogStatus();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void JogPanel::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);
    // Jog section is the elastic element (fills the panel); the monitor sits below it. No dead band.
    mainLayout->addWidget(createJogSection(), 1);
    mainLayout->addWidget(HexaWidgets::createSeparatorH());
    mainLayout->addWidget(createMonitorSection(), 0);
}

QWidget* JogPanel::createJogSection() {
    QWidget* container = new QWidget();
    container->setAttribute(Qt::WA_TranslucentBackground);
    container->setStyleSheet("background: transparent; border: none;");
    QVBoxLayout* layout = new QVBoxLayout(container);
    // Narrow side margins: every pixel saved here widens the minus/plus jog keys.
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(6);

    // --- Panel identity strip ---
    // Quiet small-caps title over a hairline, same visual language as the MONITOR SYSTEM strip
    // below: on the carbon background each panel reads as a named instrument module.
    QLabel* titleStrip = new QLabel(QStringLiteral("JOG CONTROL"), container);
    titleStrip->setAlignment(Qt::AlignCenter);
    titleStrip->setFixedHeight(24);
    titleStrip->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; border: none; border-bottom: 1px solid %1; color: %2;"
        " font-family: '%3'; font-size: 11px; font-weight: 600; letter-spacing: 2px; }")
        .arg(Hexa::Colors::Hairline, Hexa::Colors::TextMuted, Hexa::Fonts::familyUI()));
    layout->addWidget(titleStrip);

    // --- Context row: Tool / Base ---
    QHBoxLayout* ctxLayout = new QHBoxLayout();
    ctxLayout->setSpacing(8);
    m_btnJogTool = HexaWidgets::createButtonStd("T: ---", this, 0, 0);   // theme-standard touch height
    connect(m_btnJogTool, &QPushButton::clicked, this, &JogPanel::onToolButtonClicked);
    ctxLayout->addWidget(m_btnJogTool, 1);
    m_btnJogBase = HexaWidgets::createButtonStd("B: ---", this, 0, 0);
    connect(m_btnJogBase, &QPushButton::clicked, this, &JogPanel::onBaseButtonClicked);
    ctxLayout->addWidget(m_btnJogBase, 1);
    layout->addLayout(ctxLayout);

    // --- JOG button (arm toggle + folded status: SIM / ENABLE / READY / MOVING) ---
    m_btnJogEnable = HexaWidgets::createButtonStd("ENABLE JOG", this, 0, Hexa::Dim::BtnTouch);
    m_btnJogEnable->setObjectName(QStringLiteral("btnJogArm"));
    m_btnJogEnable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_btnJogEnable->setMaximumWidth(QWIDGETSIZE_MAX);
    connect(m_btnJogEnable, &QPushButton::clicked, this, &JogPanel::onJogEnableClicked);
    layout->addWidget(m_btnJogEnable);

    // Transient notice (axis limit / unreachable); hidden until a warning arrives.
    m_noticeLabel = new QLabel(this);
    m_noticeLabel->setAlignment(Qt::AlignCenter);
    m_noticeLabel->setWordWrap(true);
    m_noticeLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(Hexa::Colors::Warning));
    m_noticeLabel->hide();
    layout->addWidget(m_noticeLabel);

    // --- Coordinate frame (drop-down) + step, two equal columns (mirrors the Tool/Base row) ---
    QHBoxLayout* modeRow = new QHBoxLayout();
    modeRow->setSpacing(8);
    m_comboCoord = HexaWidgets::createComboBox();
    m_comboCoord->addItems({"JOINT", "WORLD", "TOOL"});
    m_comboCoord->setCurrentIndex(0);
    m_comboCoord->setMinimumHeight(32);
    connect(m_comboCoord, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &JogPanel::onCoordModeChanged);
    modeRow->addWidget(m_comboCoord, 1);
    m_btnStep = HexaWidgets::createButtonSm("STEP  1.0", this, 0, 0);
    m_btnStep->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_btnStep->setStyleSheet(Hexa::Styles::ButtonBase + "QPushButton { border: 1px solid " +
        Hexa::Colors::Primary + "; color: white; } QPushButton:hover { background-color: " +
        Hexa::Colors::StateHover + "; }");
    connect(m_btnStep, &QPushButton::clicked, this, &JogPanel::onStepClicked);
    applyStep(m_stepOptions[m_stepIndex]);
    modeRow->addWidget(m_btnStep, 1);
    layout->addLayout(modeRow);

    // --- Jog pad: full-width grid "[-] [axis | desired value | unit] [+]" per axis ---
    // The framed centre cell shows the DESIRED (commanded) value between the jog keys - this display
    // is a required function of the panel, it must stay readable at all times. The minus/plus keys
    // expand to fill the remaining width and share the spare height (no dead bands, pendant-style).
    QGridLayout* pad = new QGridLayout();
    pad->setHorizontalSpacing(6);
    pad->setVerticalSpacing(6);
    pad->setColumnStretch(0, 1);
    pad->setColumnStretch(1, 0);
    pad->setColumnStretch(2, 1);

    // DRO cell width from real font metrics - no hand-tuned pixel constant, immune to DPI and
    // font substitution. Bounded shrink (same proven pattern as the status-bar makeWidthElastic):
    // preferred width fits the Cartesian worst case ("-1234.56"), and under panel compression the
    // cell shrinks down to the joint worst case ("-170.00") INSTEAD of overflowing the row and
    // colliding with the jog keys (boss live-review defect).
    QFont axisFont(Hexa::Fonts::familyUI());
    axisFont.setPixelSize(kJogAxisLabelFontPx);
    QFont valueFont(Hexa::Fonts::familyMono());
    valueFont.setPixelSize(kJogValueFontPx);
    QFont unitFont(Hexa::Fonts::familyMono());
    unitFont.setPixelSize(kJogUnitFontPx);
    const int cellFixedPartPx = QFontMetrics(axisFont).horizontalAdvance(QStringLiteral("Rx"))
                              + QFontMetrics(unitFont).horizontalAdvance(QStringLiteral("mm"))
                              + kJogCellChromePx;
    const int cellPreferredPx = cellFixedPartPx
        + QFontMetrics(valueFont).horizontalAdvance(QStringLiteral("-1234.56"));
    const int cellMinimumPx = cellFixedPartPx
        + QFontMetrics(valueFont).horizontalAdvance(QStringLiteral("-170.00"));

    for (int i = 0; i < 6; ++i) {
        // U+2212 MINUS SIGN (same optical weight as "+"); \u escapes keep the source encoding-proof.
        // Key minimum width = the gloved-finger touch floor: under panel compression the keys give
        // up width down to it (together with the elastic value cell) instead of overflowing.
        QPushButton* btnMinus = HexaWidgets::createButtonIcon(QStringLiteral("\u2212"), this, 0, 0);
        btnMinus->setMinimumSize(Hexa::Dim::BtnTouch, kJogKeyMinHeightPx);
        btnMinus->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        btnMinus->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        btnMinus->setObjectName(QStringLiteral("jogMinus_%1").arg(i));
        btnMinus->setProperty("axis", i);
        btnMinus->setProperty("dir", -1.0);
        connect(btnMinus, &QPushButton::clicked, this, &JogPanel::onJogBtnPressed);
        m_jogButtons.append(btnMinus);

        QFrame* valueBox = new QFrame(this);
        // Bounded shrink between the two metric widths; Preferred = grow to max, shrink to min.
        valueBox->setMinimumWidth(cellMinimumPx);
        valueBox->setMaximumWidth(cellPreferredPx);
        // Full row height: the DRO cell stretches with its row, like the jog keys beside it.
        valueBox->setMinimumHeight(kJogKeyMinHeightPx);
        valueBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        valueBox->setStyleSheet(valueBoxStyle(false));
        m_valueBoxes.append(valueBox);
        QHBoxLayout* boxLayout = new QHBoxLayout(valueBox);
        boxLayout->setContentsMargins(8, 2, 8, 2);
        boxLayout->setSpacing(4);

        // The three readout labels get REAL QFonts (the same objects the cell widths were measured
        // with) and colour-only stylesheets: a stylesheet font is invisible to size hints, and at
        // the compressed minimum that mismatch would clip the minus sign of the value - a wrong
        // displayed value on a jog readout, not a cosmetic defect.
        // Axis name: muted, so the value stays the dominant element of the readout.
        QLabel* axisLabel = new QLabel(QStringLiteral("A1"), valueBox);
        axisLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        axisLabel->setFont(axisFont);
        axisLabel->setStyleSheet(labelColorStyle(Hexa::Colors::TextMuted));
        m_axisLabels.append(axisLabel);
        boxLayout->addWidget(axisLabel, 0);

        QLabel* value = new QLabel(QStringLiteral("0.00"), valueBox);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setFont(valueFont);
        value->setStyleSheet(labelColorStyle(Hexa::Colors::TextMain));
        m_activeDisplays.append(value);
        boxLayout->addWidget(value, 1);

        // Unit suffix (degree sign / mm, kept in sync with the jog frame by updateJogLabels).
        QLabel* unit = new QLabel(QString(QChar(0x00B0)), valueBox);
        unit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        unit->setFont(unitFont);
        unit->setStyleSheet(labelColorStyle(Hexa::Colors::TextMuted));
        m_unitLabels.append(unit);
        boxLayout->addWidget(unit, 0);

        QPushButton* btnPlus = HexaWidgets::createButtonIcon(QStringLiteral("+"), this, 0, 0);
        btnPlus->setMinimumSize(Hexa::Dim::BtnTouch, kJogKeyMinHeightPx);
        btnPlus->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        btnPlus->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        btnPlus->setObjectName(QStringLiteral("jogPlus_%1").arg(i));
        btnPlus->setProperty("axis", i);
        btnPlus->setProperty("dir", 1.0);
        connect(btnPlus, &QPushButton::clicked, this, &JogPanel::onJogBtnPressed);
        m_jogButtons.append(btnPlus);

        pad->addWidget(btnMinus, i, 0);
        pad->addWidget(valueBox, i, 1);   // no alignment: the DRO cell fills the full row height
        pad->addWidget(btnPlus, i, 2);
        pad->setRowStretch(i, 1);
    }
    layout->addLayout(pad, 1); // the pad is the elastic element of the jog section

    m_btnHome = HexaWidgets::createButtonStd("GO HOME", this, 0, 0);
    m_btnHome->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_btnHome->setMaximumWidth(QWIDGETSIZE_MAX); // full row, like the JOG button above
    connect(m_btnHome, &QPushButton::clicked, this, &JogPanel::onGoHomeClicked);
    layout->addWidget(m_btnHome);
    return container;
}

QWidget* JogPanel::createMonitorSection() {
    QWidget* container = new QWidget();
    container->setAttribute(Qt::WA_TranslucentBackground);
    container->setStyleSheet("background: transparent; border: none;");
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);

    QPushButton* titleButton = HexaWidgets::createTitleButton("MONITOR SYSTEM", this);
    titleButton->setChecked(false);   // monitor collapsed by default (boss request)
    connect(titleButton, &QPushButton::toggled, this, &JogPanel::onToggleMonitor);
    layout->addWidget(titleButton);

    // Card: the ACTUAL values sit in a framed Surface panel (same look as the app's main panels) so
    // the monitor reads as an instrument, not as loose text floating on the window background.
    QFrame* card = HexaWidgets::createMainPanel();
    m_monitorContent = card;
    // Hidden by default (boss request): the card starts collapsed so the jog pad uses the full height;
    // the MONITOR SYSTEM title button expands it on demand. Not retain-size-when-hidden, so the freed
    // space is given to the pad instead of being reserved as an empty slot.
    card->setVisible(false);
    QVBoxLayout* contentLayout = new QVBoxLayout(card);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(6);

    QHBoxLayout* ctxLayout = new QHBoxLayout();
    ctxLayout->setSpacing(8);
    m_comboMonTool = HexaWidgets::createComboBox();
    m_comboMonTool->setObjectName(QStringLiteral("comboMonitorTool"));
    m_comboMonTool->addItems({"..."});
    // Always selectable (a greyed-out control reads as broken): with base JOINT the joints are shown
    // and the tool has no effect on them; the remembered selection applies once a Cartesian base is
    // chosen.
    m_comboMonTool->setToolTip(QStringLiteral(
        "Tool for the Cartesian monitor pose (takes effect with a non-JOINT base)"));
    connect(m_comboMonTool, &QComboBox::currentTextChanged, this, &JogPanel::onMonitorContextChanged);
    m_comboMonBase = HexaWidgets::createComboBox();
    m_comboMonBase->setObjectName(QStringLiteral("comboMonitorBase"));
    m_comboMonBase->addItems({"JOINT", "..."});
    connect(m_comboMonBase, &QComboBox::currentTextChanged, this, &JogPanel::onMonitorContextChanged);
    ctxLayout->addWidget(m_comboMonTool, 1);
    ctxLayout->addWidget(m_comboMonBase, 1);
    contentLayout->addLayout(ctxLayout);

    // Two "label  value" pairs per row: labels left-aligned (fixed width), values right-aligned in a
    // stretched column so the numbers form a tidy right-justified column in each half.
    QGridLayout* grid = new QGridLayout();
    grid->setVerticalSpacing(6);
    grid->setHorizontalSpacing(8);
    // Columns: [label][value][gap][label][value]; the two value columns absorb the width.
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(4, 1);
    grid->setColumnMinimumWidth(2, 12); // breathing room between the two halves
    for (int i = 0; i < 6; ++i) {
        QLabel* label = HexaWidgets::createLabelText("X:");
        label->setFixedWidth(28);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_passiveLabels.append(label);

        QLabel* display = HexaWidgets::createLabelData("0.00");
        display->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_passiveDisplays.append(display);

        const int row = i / 2;
        const int col = (i % 2) * 3;
        grid->addWidget(label, row, col);
        grid->addWidget(display, row, col + 1);
    }
    contentLayout->addLayout(grid);
    layout->addWidget(card);
    return container;
}

void JogPanel::onToggleMonitor(bool visible) {
    if (m_monitorContent) m_monitorContent->setVisible(visible);
}

// ---------------------------------------------------------------------------
// Feedback in
// ---------------------------------------------------------------------------

void JogPanel::updateState(const HmiMotionStatus& status) {
    m_isSimulated = status.isSimulated;
    m_jogEnabled = status.jogEnabled;
    const bool armed = m_jogEnabled && !m_isSimulated;

    // Coordinate frame sync (controller is authoritative).
    if (status.currentJogFrame != m_currentMode) {
        m_currentMode = status.currentJogFrame;
        updateJogLabels(m_currentMode);
        updateStepOptions();
        if (m_comboCoord) {
            m_comboCoord->blockSignals(true);
            m_comboCoord->setCurrentIndex(m_currentMode);
            m_comboCoord->blockSignals(false);
        }
    }

    // Tool / base context sync.
    if (status.activeToolId != m_currentJogToolId) {
        for (const auto& tool : m_availableTools) {
            if (tool.id == status.activeToolId) {
                m_currentJogToolId = tool.id;
                m_currentJogToolName = tool.name;
                if (m_btnJogTool) m_btnJogTool->setText("T: " + m_currentJogToolName);
                break;
            }
        }
    }
    if (status.activeBaseId != m_currentJogBaseId) {
        for (const auto& base : m_availableBases) {
            if (base.id == status.activeBaseId) {
                m_currentJogBaseId = base.id;
                m_currentJogBaseName = base.name;
                if (m_btnJogBase) m_btnJogBase->setText("B: " + m_currentJogBaseName);
                break;
            }
        }
    }

    // Active displays: joints in JOINT mode, TCP otherwise.
    const QVector<double>& src = (m_currentMode == 0) ? status.displayJoints : status.displayTcp;
    for (int i = 0; i < 6; ++i) {
        if (i < src.size()) m_activeDisplays[i]->setText(QString::number(src[i], 'f', 2));
    }
    // At-limit highlight is joint-space only (axis limits are per joint); in Cartesian modes the
    // helper clears every highlight and unreachable targets are reported via jogNotice instead.
    updateAxisLimitHighlights(src);

    // Jog-button gating (anti-stick): release once the robot has started AND stopped moving; the
    // watchdog is the safety net if the motion signal never clears.
    if (!armed) {
        unblockJog();
    } else if (m_jogBlocked) {
        if (status.robotMoving) {
            m_jogSawMotion = true;
        } else if (m_jogSawMotion) {
            unblockJog();
        }
    }
    setJogButtonsEnabled(armed && !m_jogBlocked);
    refreshJogStatus();

    // Transient jog notice (once per distinct message).
    if (status.jogNotice != m_lastNotice) {
        m_lastNotice = status.jogNotice;
        if (!status.jogNotice.isEmpty()) {
            showJogNotice(status.jogNotice);
        }
    }

    // Passive monitor. The tool combo stays enabled even for JOINT (the selection is remembered and
    // applies with a Cartesian base) - a disabled selector reads as a broken control.
    const QString currentBase = m_comboMonBase->currentText();
    const bool isJointMonitor = (currentBase == "JOINT");
    for (int i = 0; i < 6; ++i) {
        double val = 0.0;
        if (isJointMonitor) {
            if (i < status.actualJoints.size()) val = status.actualJoints[i];
        } else {
            if (i < status.monitorPose.size()) val = status.monitorPose[i];
        }
        m_passiveDisplays[i]->setText(QString::number(val, 'f', 2));
    }
}

void JogPanel::onConfigReceived(const HmiSystemConfig& config) {
    m_availableTools = config.tools;
    m_availableBases = config.bases;

    // Joint-axis limits for the at-limit display highlight. Mapped by the explicit axisIndex; axes
    // the config does not cover keep known=false (no highlight). Refreshed on the next status tick.
    m_axisLimits.fill(AxisLimitRange{});
    for (const HmiAxisLimit& limit : config.axisLimits) {
        if (limit.axisIndex >= 0 && limit.axisIndex < m_axisLimits.size()) {
            m_axisLimits[limit.axisIndex] = AxisLimitRange{true, limit.minDeg, limit.maxDeg};
        }
    }

    if (!config.tools.isEmpty()) {
        if (m_currentJogToolId < 0) {
            m_currentJogToolId = config.tools.first().id;
            m_currentJogToolName = config.tools.first().name;
        }
        m_btnJogTool->setText("T: " + m_currentJogToolName);
    } else {
        m_btnJogTool->setText("T: ---");
        m_currentJogToolName = "---";
        m_currentJogToolId = -1;
    }

    if (!config.bases.isEmpty()) {
        if (m_currentJogBaseId < 0) {
            m_currentJogBaseId = config.bases.first().id;
            m_currentJogBaseName = config.bases.first().name;
        }
        m_btnJogBase->setText("B: " + m_currentJogBaseName);
    } else {
        m_btnJogBase->setText("B: ---");
        m_currentJogBaseName = "---";
        m_currentJogBaseId = -1;
    }

    const bool tBlocked = m_comboMonTool->blockSignals(true);
    m_comboMonTool->clear();
    for (const auto& t : config.tools) m_comboMonTool->addItem(t.name, t.id);
    m_comboMonTool->blockSignals(tBlocked);

    const bool bBlocked = m_comboMonBase->blockSignals(true);
    m_comboMonBase->clear();
    m_comboMonBase->addItem("JOINT");
    for (const auto& b : config.bases) m_comboMonBase->addItem(b.name, b.id);
    m_comboMonBase->blockSignals(bBlocked);

    emitContextChanged();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void JogPanel::updateJogLabels(int mode) {
    const QStringList active = (mode == 0)
        ? QStringList{"A1", "A2", "A3", "A4", "A5", "A6"}
        : QStringList{"X", "Y", "Z", "Rx", "Ry", "Rz"};
    for (int i = 0; i < 6; ++i) {
        if (i < m_axisLabels.size()) m_axisLabels[i]->setText(active[i]);
        if (i < m_unitLabels.size()) {
            // JOINT: every axis is degrees; Cartesian: X/Y/Z are millimetres, Rx/Ry/Rz degrees.
            const bool isMillimetres = (mode != 0) && (i < 3);
            m_unitLabels[i]->setText(isMillimetres ? QStringLiteral("mm")
                                                   : QString(QChar(0x00B0)));
        }
    }
}

void JogPanel::updateMonitorLabels(bool isJoint) {
    const QStringList passive = isJoint
        ? QStringList{"A1:", "A2:", "A3:", "A4:", "A5:", "A6:"}
        : QStringList{"X:", "Y:", "Z:", "A:", "B:", "C:"};
    for (int i = 0; i < 6; ++i) {
        if (i < m_passiveLabels.size()) m_passiveLabels[i]->setText(passive[i]);
    }
}

void JogPanel::onCoordModeChanged(int index) {
    m_currentMode = index;
    updateJogLabels(index);
    updateStepOptions();
    emit coordSystemChanged(index);
}

void JogPanel::onStepClicked() {
    if (m_stepOptions.isEmpty()) return;
    m_stepIndex = (m_stepIndex + 1) % m_stepOptions.size();
    applyStep(m_stepOptions[m_stepIndex]);
}

void JogPanel::applyStep(const QString& text) {
    bool ok = false;
    const double val = text.toDouble(&ok);
    if (ok) {
        m_currentStep = val;
    }
    // In Cartesian mode the orientation axes use a separate degree step, index-aligned with the
    // translation list, so a single click can never command a translation-sized angle.
    const bool isCartesian = (m_currentMode != 0);
    if (isCartesian && m_stepIndex >= 0 && m_stepIndex < m_cartOrientStepOptions.size()) {
        const QString degText = m_cartOrientStepOptions[m_stepIndex];
        m_currentOrientStep = degText.toDouble();
        if (m_btnStep) m_btnStep->setText("STEP  " + text + "mm/" + degText + QChar(0x00B0));
    } else {
        if (m_btnStep) m_btnStep->setText("STEP  " + text);
    }
}

void JogPanel::updateStepOptions() {
    if (m_currentMode == 0) {
        m_stepOptions = m_jointStepOptions;
    } else {
        m_stepOptions = m_cartStepOptions;
    }
    if (m_stepOptions.isEmpty()) return;
    if (m_stepIndex >= m_stepOptions.size()) m_stepIndex = 0;
    applyStep(m_stepOptions[m_stepIndex]);
}

void JogPanel::onJogBtnPressed() {
    if (!m_jogEnabled || m_isSimulated) return;
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    const int axis = btn->property("axis").toInt();
    const double dir = btn->property("dir").toDouble();
    // Cartesian orientation axes (indices 3..5) use the degree step; joints and Cartesian X/Y/Z use
    // the primary step.
    const bool isCartesian = (m_currentMode != 0);
    const double step = (isCartesian && axis >= 3) ? m_currentOrientStep : m_currentStep;
    emit jogRequested(axis, dir * step);
    blockJog();
}

void JogPanel::setJogButtonsEnabled(bool enabled) {
    for (QPushButton* btn : m_jogButtons) btn->setEnabled(enabled);
}

void JogPanel::applyValueBoxStyle(int axisIndex, bool atLimit) {
    if (axisIndex < 0 || axisIndex >= m_valueBoxes.size()) return;
    m_valueBoxes[axisIndex]->setStyleSheet(valueBoxStyle(atLimit));
}

// Amber cell background when a joint axis sits at its configured limit. Joint-space only: in
// Cartesian modes (m_currentMode != 0) every highlight is cleared. Restyles only on state change so
// the periodic status stream does not re-set stylesheets every tick.
void JogPanel::updateAxisLimitHighlights(const QVector<double>& displayValues) {
    for (int i = 0; i < m_axisAtLimit.size(); ++i) {
        bool atLimit = false;
        if (m_currentMode == 0 && i < displayValues.size() && m_axisLimits[i].known) {
            atLimit = (displayValues[i] <= m_axisLimits[i].minDeg + kAxisLimitBandDeg) ||
                      (displayValues[i] >= m_axisLimits[i].maxDeg - kAxisLimitBandDeg);
        }
        if (atLimit != m_axisAtLimit[i]) {
            m_axisAtLimit[i] = atLimit;
            applyValueBoxStyle(i, atLimit);
        }
    }
}

void JogPanel::blockJog() {
    m_jogBlocked = true;
    m_jogSawMotion = false;
    m_jogReleaseTimer->start(kJogBlockMaxMs);
    setJogButtonsEnabled(false);
    refreshJogStatus();
}

void JogPanel::unblockJog() {
    if (!m_jogBlocked) return;
    m_jogBlocked = false;
    m_jogSawMotion = false;
    m_jogReleaseTimer->stop();
    setJogButtonsEnabled(m_jogEnabled && !m_isSimulated);
    refreshJogStatus();
}

// Folded status: the JOG button itself shows SIM / ENABLE / READY / MOVING via text + colour.
// All four states use theme colours (one meaning = one colour across the HMI); the un-armed state
// is the panel's filled call-to-action, so the operator's next action is visible from a distance.
void JogPanel::refreshJogStatus() {
    if (!m_btnJogEnable) return;
    const bool armed = m_jogEnabled && !m_isSimulated;
    QString text;
    QString bg;
    QString fg = Hexa::Colors::BadgeFgDark;
    QString border;
    if (m_isSimulated) {
        text = QStringLiteral("JOG - SIM ONLY"); bg = Hexa::Colors::BadgeNeutralBg;
        fg = Hexa::Colors::BadgeNeutralFg; border = Hexa::Colors::BadgeNeutralBg;
    } else if (!armed) {
        text = QStringLiteral("ENABLE JOG"); bg = Hexa::Colors::Primary;
        fg = Hexa::Colors::Background; border = Hexa::Colors::Primary;
    } else if (m_jogBlocked) {
        text = QStringLiteral("\u25CF JOG MOVING"); bg = Hexa::Colors::Warning;
        border = Hexa::Colors::Warning;
    } else {
        text = QStringLiteral("\u25CF JOG READY"); bg = Hexa::Colors::Success;
        border = Hexa::Colors::Success;
    }
    m_btnJogEnable->setEnabled(!m_isSimulated);
    m_btnJogEnable->setText(text);
    m_btnJogEnable->setToolTip(m_isSimulated ? QStringLiteral("Jog is disabled in simulation mode")
                                             : QString());
    m_btnJogEnable->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 10px;"
        " font-family: '%4'; font-size: 13px; font-weight: 600; }")
        .arg(bg, fg, border, Hexa::Fonts::familyUI()));
}

void JogPanel::showJogNotice(const QString& text) {
    if (!m_noticeLabel) return;
    m_noticeLabel->setText(text);
    m_noticeLabel->show();
    if (m_noticeTimer) m_noticeTimer->start(kNoticeAutoHideMs);
}

void JogPanel::onJogNoticeTimeout() {
    if (m_noticeLabel) m_noticeLabel->hide();
}

void JogPanel::onJogEnableClicked() {
    if (m_isSimulated) return; // jog cannot be armed in simulation
    emit jogEnableRequested(!m_jogEnabled);
}

void JogPanel::onGoHomeClicked() {
    emit goHomeRequested();
}

void JogPanel::onToolButtonClicked() {
    FramePickerDialog dialog(this);
    dialog.setToolData(m_availableTools, m_currentJogToolId);
    if (dialog.exec() != QDialog::Accepted) return;
    const int selectedId = dialog.getSelectedToolId();
    if (selectedId < 0) return;
    for (const auto& tool : m_availableTools) {
        if (tool.id == selectedId) {
            m_currentJogToolId = selectedId;
            m_currentJogToolName = tool.name;
            m_btnJogTool->setText("T: " + m_currentJogToolName);
            emitContextChanged();
            break;
        }
    }
}

void JogPanel::onBaseButtonClicked() {
    FramePickerDialog dialog(this);
    dialog.setBaseData(m_availableBases, m_currentJogBaseId);
    if (dialog.exec() != QDialog::Accepted) return;
    const int selectedId = dialog.getSelectedBaseId();
    if (selectedId < 0) return;
    for (const auto& base : m_availableBases) {
        if (base.id == selectedId) {
            m_currentJogBaseId = selectedId;
            m_currentJogBaseName = base.name;
            m_btnJogBase->setText("B: " + m_currentJogBaseName);
            emitContextChanged();
            break;
        }
    }
}

void JogPanel::emitContextChanged() {
    if (m_currentJogToolName.isEmpty() || m_currentJogBaseName.isEmpty()) return;
    emit jogContextChanged(m_currentJogToolName, m_currentJogBaseName);
    // Force a coordinate-system notification so the backend refreshes the toolId/baseId in its jog
    // context.
    emit coordSystemChanged(m_currentMode);
}

void JogPanel::onMonitorContextChanged() {
    const QString base = m_comboMonBase->currentText();
    const QString toolName = m_comboMonTool->currentText();
    if (base == "JOINT") {
        updateMonitorLabels(true);
        emit monitorContextChanged("-", base);
    } else {
        updateMonitorLabels(false);
        emit monitorContextChanged(toolName, base);
    }
}

} // namespace hexa
// --- END OF FILE: HexaStudio/jog_control/JogPanel.cpp ---
