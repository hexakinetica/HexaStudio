// --- START OF FILE: HexaStudio/status_bar/StatusBarPanel.cpp ---
#include "StatusBarPanel.h"
#include "ConfirmDialog.h"

#include "HexaTheme.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace hexa {

namespace {
constexpr int kPanelHeightPx = 64;   // slimmer top bar (boss request); still clears the 48px E-STOP

// System-status vocabulary (v0.4.x industrial carbon): one short word per state (no decorations -
// the LED-dot prefix was rejected at the boss live review), so the status block stays compact.
// Precedence is unchanged (E-STOP > OFFLINE > MOVING > ERROR > READY, see updateState). The label
// width is computed from font metrics over EVERY display string (HexaWidgets::statusLabelMinWidth)
// - the previous hand-tuned 160px clipped "SYSTEM READY".
const QLatin1String kStatusEStop("E-STOP");
const QLatin1String kStatusOffline("OFFLINE");
const QLatin1String kStatusMoving("MOVING");
const QLatin1String kStatusError("ERROR");
const QLatin1String kStatusPaused("PAUSED");
const QLatin1String kStatusReady("READY");

QStringList statusVocabulary() {
    return {kStatusEStop, kStatusOffline, kStatusMoving, kStatusError, kStatusPaused, kStatusReady};
}

// ONE spacing for the whole bar (boss live review: the previous mixed 10/15px gaps read as
// misaligned). The larger kControlGroupGapPx below stays as the deliberate group boundary.
constexpr int kControlSpacingPx = 12;
// Speed overrides above this fraction require an explicit operator confirmation (shipping spec).
constexpr int kSpeedConfirmThresholdPercent = 50;
// Forced gap between control groups (SIM/REAL | SPEED | SETTINGS/STATS). Added as fixed spacer
// items, so it is part of the layout's MINIMUM size and can never collapse - unlike setSpacing,
// which the layout sacrifices first when the window narrows or the DPI scale changes.
constexpr int kControlGroupGapPx = 18;
// Stats instrument cell must fit "CPU LOAD" over "100.0%" in 15px mono.
constexpr int kStatCellMinWidthPx = 96;

// Centre-controls sizing (v0.4.3): the SPEED selector and the SETTINGS/STATS buttons share one
// stretched width and one height, so the SIM|SPEED|SETTINGS/STATS cluster reads as a single aligned
// instrument row instead of a value field next to two smaller buttons (boss live review). Elastic so
// the cluster still compresses under STB-REQ-0010.
constexpr int kCenterControlMinWidthPx  = 104;
constexpr int kCenterControlPrefWidthPx = 132;

// Bounded-shrink width: the control keeps its touch-friendly preferred width while space allows and
// compresses smoothly down to a readable minimum when the operator narrows the window. Fixed widths
// do not scale at all - they only move the hard floor up.
void makeWidthElastic(QWidget* widget, int minimumPx, int preferredPx) {
    widget->setMinimumWidth(minimumPx);
    widget->setMaximumWidth(preferredPx);
    QSizePolicy policy = widget->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Preferred); // Grow+Shrink between the two bounds
    widget->setSizePolicy(policy);
}
} // namespace

StatusBarPanel::StatusBarPanel(QWidget* parent) : QWidget(parent) {
    setFixedHeight(kPanelHeightPx);
    // Translucent by design: the host window paints the application background (same module standard
    // as JogPanel).
    setAttribute(Qt::WA_TranslucentBackground);
    setupUi();
}

void StatusBarPanel::setAppVersion(const QString& version) {
    m_appVersion = version;
}

void StatusBarPanel::onConfigReceived(const HmiSystemConfig& config) {
    m_controllerIp = config.network.controllerIp;
}

// ---------------------------------------------------------------------------
// Feedback in
// ---------------------------------------------------------------------------

void StatusBarPanel::updateState(const HmiTopStatus& status, bool isMoving, bool isProgramPaused) {
    m_isRobotMoving = isMoving;

    HexaWidgets::updateButtonDangerState(m_btnEStop, status.isEStop);
    m_btnEStop->setText(status.isEStop ? "RESET" : "E-STOP");

    // Traffic-light system status; the SIM/REAL switch is locked in every non-idle state.
    QLatin1String statusWord = kStatusReady;
    Hexa::State state;
    if (status.isEStop) {
        statusWord = kStatusEStop;
        state = Hexa::State::Error;
        m_switchMode->setEnabled(false);
    } else if (!status.isConnected) {
        statusWord = kStatusOffline;
        state = Hexa::State::Error;
        m_switchMode->setEnabled(false);
    } else if (isMoving) {
        statusWord = kStatusMoving;
        state = Hexa::State::Active;
        m_switchMode->setEnabled(false);
    } else if (status.hasError) {
        // The explicit error flag is authoritative: activeErrors may carry informational text.
        statusWord = kStatusError;
        state = Hexa::State::Warning;
        m_switchMode->setEnabled(true);
    } else if (isProgramPaused) {
        // A paused program is a HELD machine, not a ready one (boss review: READY while paused
        // misstated the system state). Error outranks it; mode switching stays locked - the
        // program will continue in whatever mode it was started in.
        statusWord = kStatusPaused;
        state = Hexa::State::Warning;
        m_switchMode->setEnabled(false);
    } else {
        statusWord = kStatusReady;
        state = Hexa::State::Success;
        m_switchMode->setEnabled(true);
    }
    m_lblStatus->setText(statusWord);
    HexaWidgets::updateStatusLabel(m_lblStatus, state);

    // Mode switch sync (controller is authoritative).
    const bool realModeFromStatus = (status.mode == "AUTO" || status.mode == "REAL");
    if (m_switchMode->isChecked() != realModeFromStatus) {
        const bool wasBlocked = m_switchMode->blockSignals(true);
        m_switchMode->setChecked(realModeFromStatus);
        m_switchMode->blockSignals(wasBlocked);
    }

    m_lblCpu->setText(QString::number(status.cpuLoad, 'f', 1) + "%");
    m_lblTemp->setText(QString::number(status.controllerTemp, 'f', 1) + QChar(0x00B0) + "C");
    m_lblPing->setText(QString::number(status.networkLatency, 'f', 1) + "ms");
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void StatusBarPanel::setupUi() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(Hexa::Dim::PanelPadding, 5, Hexa::Dim::PanelPadding, 5);
    mainLayout->setSpacing(kControlSpacingPx);
    mainLayout->addWidget(createLeftSection());
    mainLayout->addWidget(HexaWidgets::createSectionSeparator());
    mainLayout->addWidget(createCenterSection(), 1);
    mainLayout->addWidget(HexaWidgets::createSectionSeparator());
    mainLayout->addWidget(createRightSection());
}

QWidget* StatusBarPanel::createLeftSection() {
    QWidget* w = new QWidget(this);
    m_leftSection = w;
    w->setAttribute(Qt::WA_TranslucentBackground);
    // Sized by content by default (the shipping panel reserved a fixed 400 px here, which alone
    // forbade narrowing the whole bar); the status label keeps an explicit readable minimum. The
    // shell may later pin this width to its left column via setColumnGuides - the brand+status stay
    // left-aligned and the trailing gap carries the group edge to the column boundary.
    QHBoxLayout* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(kControlSpacingPx);

    // All-caps wordmark (boss live review): mixed case read as un-industrial on the carbon bar.
    m_lblBrand = HexaWidgets::createLabelHeader("HEXASTUDIO");
    // Click on the brand opens the About box (data arrives via config/setAppVersion, no singleton).
    m_lblBrand->setCursor(Qt::PointingHandCursor);
    m_lblBrand->setStyleSheet(m_lblBrand->styleSheet() +
                              " QLabel:hover { color: " + Hexa::Colors::Accent + "; }");
    m_lblBrand->installEventFilter(this);
    l->addWidget(m_lblBrand);

    l->addWidget(HexaWidgets::createSeparatorV());

    m_lblStatus = HexaWidgets::createLabelStatus(kStatusReady);
    m_lblStatus->setObjectName(QStringLiteral("lblSystemStatus"));
    // Sized so every word of the status vocabulary fits - metrics, not a pixel constant.
    m_lblStatus->setMinimumWidth(HexaWidgets::statusLabelMinWidth(statusVocabulary()));
    // The status indicator doubles as the entry point to the diagnostics overlay.
    m_lblStatus->setCursor(Qt::PointingHandCursor);
    m_lblStatus->setToolTip("Open diagnostics");
    m_lblStatus->installEventFilter(this);
    l->addWidget(m_lblStatus);
    return w;
}

QWidget* StatusBarPanel::createCenterSection() {
    m_centerStack = new QStackedWidget(this);
    m_centerStack->setAttribute(Qt::WA_TranslucentBackground);
    m_centerStack->setStyleSheet("background: transparent; border: none;");
    m_centerStack->addWidget(createCenterControls());
    m_centerStack->addWidget(createCenterStats());
    syncStackPagePolicies();
    return m_centerStack;
}

// Only the VISIBLE stack page contributes to the size hint / minimum: hidden pages get the Ignored
// policy. Without this, the widest page (the stats page) dictates the whole bar's minimum width even
// while hidden, and the window can only be stretched, never narrowed.
void StatusBarPanel::syncStackPagePolicies() {
    for (int i = 0; i < m_centerStack->count(); ++i) {
        QWidget* page = m_centerStack->widget(i);
        const bool isCurrent = (i == m_centerStack->currentIndex());
        page->setSizePolicy(isCurrent ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                            isCurrent ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        page->updateGeometry();
    }
}

QWidget* StatusBarPanel::createCenterControls() {
    QWidget* w = new QWidget();
    w->setStyleSheet("background: transparent; border: none;");
    QHBoxLayout* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(kControlSpacingPx);
    l->setAlignment(Qt::AlignCenter);

    l->addWidget(HexaWidgets::createLabelText("SIM"));
    m_switchMode = new HexaToggle(this);
    connect(m_switchMode, &QAbstractButton::toggled, this, &StatusBarPanel::onModeToggled);
    l->addWidget(m_switchMode);
    l->addWidget(HexaWidgets::createLabelText("REAL"));

    l->addSpacing(kControlGroupGapPx);
    l->addWidget(HexaWidgets::createSeparatorV());
    l->addSpacing(kControlGroupGapPx);

    l->addWidget(HexaWidgets::createLabelText("SPEED"));
    m_comboSpeed = HexaWidgets::createComboBox(this);
    m_comboSpeed->setObjectName(QStringLiteral("comboSpeed"));
    // SPEED is an interactive selector living among the SETTINGS/STATS buttons, so it reads as a
    // control (graphite fill + a VISIBLE drop-down chevron), not a data cell: the previous
    // near-black field with no arrow looked like a bare text input (boss live review). Same width
    // and height as the buttons so the row aligns; centred bold mono value. Behaviour unchanged -
    // still editable with the 1..100 validator and the >50% confirmation.
    makeWidthElastic(m_comboSpeed, kCenterControlMinWidthPx, kCenterControlPrefWidthPx);
    // Crisp chevron from a small SVG resource: Qt's stylesheet ::down-arrow does NOT render the CSS
    // border-triangle trick (it draws a filled box), so a real image asset is the reliable way to
    // show a drop-down affordance without an OS-styled arrow.
    m_comboSpeed->setStyleSheet(Hexa::Styles::ComboBox + QStringLiteral(
        "QComboBox { font-family: '%1'; font-size: 14px; font-weight: 600; }"
        "QComboBox::down-arrow { image: url(:/resources/chevron_down.svg);"
        " width: 12px; height: 8px; margin-right: 8px; }")
        .arg(Hexa::Fonts::familyMono()));
    m_comboSpeed->setEditable(true);
    m_comboSpeed->addItems({"10%", "25%", "50%", "75%", "100%"});
    m_comboSpeed->setCurrentText("50%");
    m_lastAcceptedSpeedPercent = 50;
    if (m_comboSpeed->lineEdit() != nullptr) {
        m_comboSpeed->lineEdit()->setValidator(new QIntValidator(1, 100, this));
        m_comboSpeed->lineEdit()->setAlignment(Qt::AlignCenter);
    }
    connect(m_comboSpeed, &QComboBox::currentTextChanged, this, &StatusBarPanel::onSpeedChanged);
    l->addWidget(m_comboSpeed);

    l->addSpacing(kControlGroupGapPx);
    l->addWidget(HexaWidgets::createSeparatorV());
    l->addSpacing(kControlGroupGapPx);

    // SETTINGS and STATS share one stretched, elastic width so they align with each other and with
    // the SPEED selector (all three read as one instrument row).
    m_btnSettings = HexaWidgets::createButtonSm("SETTINGS", this, 0, 0);
    m_btnSettings->setObjectName(QStringLiteral("btnSettings"));
    makeWidthElastic(m_btnSettings, kCenterControlMinWidthPx, kCenterControlPrefWidthPx);
    connect(m_btnSettings, &QPushButton::clicked, this, &StatusBarPanel::settingsRequested);
    l->addWidget(m_btnSettings);

    // "STATS", not "MONITOR": the jog panel already has a position monitor ("MONITOR SYSTEM");
    // two controls named MONITOR with different meanings would confuse the operator.
    m_btnStats = HexaWidgets::createButtonSm("STATS", this, 0, 0);
    makeWidthElastic(m_btnStats, kCenterControlMinWidthPx, kCenterControlPrefWidthPx);
    m_btnStats->setObjectName(QStringLiteral("btnStats"));
    m_btnStats->setToolTip(QStringLiteral("Controller system stats (CPU / temperature / network)"));
    connect(m_btnStats, &QPushButton::clicked, this, &StatusBarPanel::onToggleStats);
    l->addWidget(m_btnStats);
    return w;
}

QWidget* StatusBarPanel::createCenterStats() {
    QWidget* w = new QWidget();
    w->setAttribute(Qt::WA_TranslucentBackground);
    QHBoxLayout* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(kControlSpacingPx);
    l->setAlignment(Qt::AlignLeft);

    // Instrument cell in the standard theme, MONOCHROME by boss decision (no per-state colour
    // coding): muted label on top, white mono value below, framed like the jog panel's cells.
    // Only stats HmiTopStatus actually carries are shown; VOLTAGE / CYCLE TIME return here once
    // the DTO reports them (cross-module item) - no dead "---" slots.
    auto addStatCell = [&](const QString& label, QLabel*& valueLabel) {
        QFrame* cell = new QFrame(this);
        cell->setMinimumWidth(kStatCellMinWidthPx);
        cell->setStyleSheet(QStringLiteral(
            "QFrame { background-color: %1; border: 1px solid %2;"
            " border-radius: %3px; }").arg(Hexa::Colors::CellInset, Hexa::Colors::Hairline,
                                           QString::number(Hexa::Dim::RadiusCell)));
        QVBoxLayout* cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(10, 4, 10, 4);
        cellLayout->setSpacing(0);
        cellLayout->addWidget(HexaWidgets::createLabelText(label));
        valueLabel = new QLabel("---", cell);
        valueLabel->setStyleSheet(Hexa::Styles::LabelBorderless.arg(
            Hexa::Colors::TextMain, Hexa::Fonts::familyMono(), "15"));
        cellLayout->addWidget(valueLabel);
        l->addWidget(cell);
    };

    addStatCell("CPU LOAD", m_lblCpu);
    addStatCell("TEMP", m_lblTemp);
    addStatCell("NETWORK", m_lblPing);

    l->addStretch();
    m_btnCloseStats = HexaWidgets::createButtonStd("CLOSE STATS", this, 120, 0);
    makeWidthElastic(m_btnCloseStats, 100, 120);
    connect(m_btnCloseStats, &QPushButton::clicked, this, &StatusBarPanel::onToggleStats);
    l->addWidget(m_btnCloseStats);
    return w;
}

QWidget* StatusBarPanel::createRightSection() {
    QWidget* w = new QWidget(this);
    m_rightSection = w;
    w->setAttribute(Qt::WA_TranslucentBackground);
    QHBoxLayout* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(kControlSpacingPx);

    // E-STOP is the ONLY item, so it fills the section (Preferred policy grows to its maximum with
    // no competing stretch). Standalone: the section is content-sized, so E-STOP keeps its 132 px
    // preferred width and sits at the far right (the centre section's stretch pushes it there).
    // Guided: the shell fixes the section to the right column and raises E-STOP's maximum, so the
    // button grows to fill it - its left edge lands on the column boundary and it becomes the widest
    // safe target on the bar. No leading stretch / AlignRight: either would stop E-STOP filling.
    m_btnEStop = HexaWidgets::createButtonDanger("E-STOP", this, 132, Hexa::Dim::BtnTouch);
    makeWidthElastic(m_btnEStop, 112, 132);
    m_btnEStop->setObjectName(QStringLiteral("btnEStop"));
    connect(m_btnEStop, &QPushButton::clicked, this, &StatusBarPanel::onEStopClicked);
    l->addWidget(m_btnEStop);
    return w;
}

void StatusBarPanel::setColumnGuides(int leftColumnPx, int rightColumnPx) {
    // The bar content is inset by PanelPadding more than the splitter cards (which sit flush against
    // the window margin), so a group whose width equals (column - PanelPadding) ends exactly at the
    // column boundary and its section separator falls into the inter-card gap below.
    const int leftGuidePx = leftColumnPx - Hexa::Dim::PanelPadding;
    const int rightGuidePx = rightColumnPx - Hexa::Dim::PanelPadding;
    if (m_leftSection) {
        m_leftSection->setFixedWidth(leftGuidePx);
    }
    if (m_rightSection && m_btnEStop) {
        m_rightSection->setFixedWidth(rightGuidePx);
        // Let E-STOP fill the pinned section (its makeWidthElastic max was 132). Minimum stays the
        // touch floor so the button never becomes unusably narrow if the guide is ever small.
        m_btnEStop->setMaximumWidth(rightGuidePx);
    }
}

// ---------------------------------------------------------------------------
// Intent out
// ---------------------------------------------------------------------------

void StatusBarPanel::onModeToggled(bool checked) {
    // The mode switch is locked while the robot is moving; revert any programmatic race.
    if (m_isRobotMoving) {
        const bool wasBlocked = m_switchMode->blockSignals(true);
        m_switchMode->setChecked(!checked);
        m_switchMode->blockSignals(wasBlocked);
        return;
    }

    if (checked) {
        // Switching to REAL can move hardware on the next command: explicit confirmation required.
        m_switchMode->blockSignals(true);
        const bool confirmed = ConfirmDialog::confirm(
            this, QStringLiteral("SAFETY WARNING"),
            QStringLiteral("Switching to REAL ROBOT mode.\nEnsure the workspace is clear."),
            QStringLiteral("SWITCH TO REAL"));
        m_switchMode->blockSignals(false);
        if (!confirmed) {
            const bool wasBlocked = m_switchMode->blockSignals(true);
            m_switchMode->setChecked(false);
            m_switchMode->blockSignals(wasBlocked);
            return;
        }
    }
    emit modeChanged(checked);
}

void StatusBarPanel::onSpeedChanged(const QString& text) {
    QString clean = text;
    clean.remove('%');
    bool ok = false;
    const int value = clean.toInt(&ok);
    if (!ok) return;

    if (value > kSpeedConfirmThresholdPercent) {
        const bool confirmed = ConfirmDialog::confirm(
            this, QStringLiteral("SAFETY WARNING"),
            QStringLiteral("Speed override %1%.\nAbove %2% may be unsafe.")
                .arg(value).arg(kSpeedConfirmThresholdPercent),
            QStringLiteral("APPLY %1%").arg(value));
        if (!confirmed) {
            const bool wasBlocked = m_comboSpeed->blockSignals(true);
            m_comboSpeed->setCurrentText(QString::number(m_lastAcceptedSpeedPercent) + "%");
            m_comboSpeed->blockSignals(wasBlocked);
            return;
        }
    }
    m_lastAcceptedSpeedPercent = value;
    emit speedChanged(value);
}

void StatusBarPanel::onEStopClicked() {
    emit eStopRequested();
}

void StatusBarPanel::onToggleStats() {
    m_centerStack->setCurrentIndex(m_centerStack->currentIndex() == 0 ? 1 : 0);
    syncStackPagePolicies(); // the new current page defines the minimum size again
}

bool StatusBarPanel::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        if (obj == m_lblStatus) {
            emit diagnosticsRequested();
            return true;
        }
        if (obj == m_lblBrand) {
            showAboutBox();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void StatusBarPanel::showAboutBox() {
    const QString version = m_appVersion.isEmpty() ? QStringLiteral("---") : m_appVersion;
    const QString ip = m_controllerIp.isEmpty() ? QStringLiteral("Not configured") : m_controllerIp;
    QMessageBox::about(this, "About HexaStudio",
                       QString("HEXA KINETIC CONTROL\n\nVersion: %1\nController IP: %2")
                           .arg(version, ip));
}

} // namespace hexa
// --- END OF FILE: HexaStudio/status_bar/StatusBarPanel.cpp ---
