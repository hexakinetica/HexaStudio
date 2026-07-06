// --- START OF FILE: HexaStudio/guide/GuidePanel.cpp ---
#include "GuidePanel.h"
#include "GuideScenarios.h"

#include "HexaTheme.h"
#include "HexaWidgets.h"

#include <QButtonGroup>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace hexa {

namespace {
// Card text width is FIXED (same proven pattern as the callout labels): with a fixed width the
// word-wrap height-for-width is exact, so the card takes precisely the height its text needs —
// no clipping at any font metrics, no dead space. Any fixed CARD HEIGHT was tried twice and
// clipped under real Segoe UI metrics (boss review, pass 3). The width fits the guide page of
// the 300 px left column (minus sidebar, panel/card margins and the scrollbar).
constexpr int kCardTextWidthPx = 176;
constexpr int kModeChipHeightPx = 36;
constexpr int kPanelProgressBarHeightPx = 8;
// Presentation-distance type (boss review 2026-07-06, second pass — larger everywhere).
constexpr int kCardNameFontPx = 14;
constexpr int kCardDescriptionFontPx = 12;
constexpr int kHintFontPx = 12;
constexpr int kChipFontPx = 12;
constexpr int kStatusFontPx = 14;
// QButtonGroup ids of the pace chips; mapped explicitly to GuideMode (no enum-cast coupling).
constexpr int kModeIdTry = 0;    // hands-on; the DEFAULT (boss 2026-07-07)
constexpr int kModeIdStep = 1;
constexpr int kModeIdPlay = 2;

// Wrapped/measured text gets a REAL QFont; stylesheets carry colours only. A stylesheet font is
// invisible to size hints and height-for-width (house precedent: HexaWidgets status label,
// program title row) — QSS-fonted cards clipped their descriptions under real metrics.
QFont uiFont(int pixelSize, QFont::Weight weight = QFont::Normal) {
    QFont font(Hexa::Fonts::familyUI());
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    return font;
}

QString labelColorOnly(const QString& color) {
    return QStringLiteral(
        "QLabel { border: none; background: transparent; color: %1; }").arg(color);
}

// Translucent fill of the violet guide accent (selected card / checked chip backgrounds),
// derived from the theme colour so the two can never drift apart.
QString guideAccentTint() {
    const QColor accent(Hexa::Colors::Active);
    return QStringLiteral("rgba(%1, %2, %3, 0.16)")
        .arg(accent.red()).arg(accent.green()).arg(accent.blue());
}

QString cardStyle(bool selected) {
    // Same visual language as the checked nav chip, in the guide's VIOLET accent: quiet card at
    // rest, accent-tinted rounded border when selected.
    return QStringLiteral(
        "QFrame#GuideCard { background-color: %1; border: 1px solid %2; border-radius: 10px; }")
        .arg(selected ? guideAccentTint() : Hexa::Colors::SurfaceLight,
             selected ? Hexa::Colors::Active : Hexa::Colors::Hairline);
}
} // namespace

// ---------------------------------------------------------------------------------------------
// GuideScenarioCard
// ---------------------------------------------------------------------------------------------

GuideScenarioCard::GuideScenarioCard(const GuideScenario& scenario, QWidget* parent)
    : QFrame(parent), m_scenarioId(scenario.id) {
    setObjectName(QStringLiteral("GuideCard"));
    setCursor(Qt::PointingHandCursor);
    setSelected(false);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);

    QHBoxLayout* titleRow = new QHBoxLayout();
    titleRow->setSpacing(6);
    QLabel* name = new QLabel(scenario.name, this);
    name->setFont(uiFont(kCardNameFontPx, QFont::DemiBold));
    name->setStyleSheet(labelColorOnly(Hexa::Colors::TextMain));
    titleRow->addWidget(name);
    titleRow->addStretch();
    // The MOTION badge is the advance warning for the motion confirm dialog (phase P3).
    QLabel* badge = scenario.involvesMotion
        ? HexaWidgets::createBadge(QStringLiteral("MOTION"), 0,
                                   Hexa::Colors::BadgeWarn, Hexa::Colors::BadgeFgDark)
        : HexaWidgets::createBadge(QStringLiteral("NO MOTION"));
    titleRow->addWidget(badge);
    layout->addLayout(titleRow);

    QLabel* description = new QLabel(
        QStringLiteral("%1 — %2").arg(scenario.durationText, scenario.description), this);
    description->setWordWrap(true);
    description->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    const QFont descriptionFont = uiFont(kCardDescriptionFontPx);
    description->setFont(descriptionFont);
    description->setStyleSheet(labelColorOnly(Hexa::Colors::TextMuted));
    // Measure the wrapped text with the SAME QFont the label renders with and pin the exact
    // size (house precedent, jog readout cells): QScrollArea performs no height-for-width
    // negotiation for its content, so a hint-driven height squashed the card and clipped the
    // text — explicit metrics are the only layout-independent guarantee.
    const int textHeightPx = QFontMetrics(descriptionFont)
        .boundingRect(QRect(0, 0, kCardTextWidthPx, 0), Qt::TextWordWrap, description->text())
        .height();
    description->setFixedSize(kCardTextWidthPx, textHeightPx);
    layout->addWidget(description);
}

void GuideScenarioCard::setSelected(bool selected) {
    setStyleSheet(cardStyle(selected));
}

void GuideScenarioCard::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        emit cardClicked(m_scenarioId);
    }
    QFrame::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------------------------------------
// GuidePanel
// ---------------------------------------------------------------------------------------------

GuidePanel::GuidePanel(QWidget* parent) : QWidget(parent) {
    setupUi();
}

void GuidePanel::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    layout->addWidget(HexaWidgets::createLabelSectionTitle(QStringLiteral("GUIDED DEMO")));
    layout->addWidget(HexaWidgets::createSeparatorH());

    QLabel* hint = new QLabel(
        QStringLiteral("Pick a scenario and a pace, then press START. The demo drives the real "
                       "interface — every press travels the production path."), this);
    hint->setWordWrap(true);
    hint->setFont(uiFont(kHintFontPx));
    hint->setStyleSheet(labelColorOnly(Hexa::Colors::TextMuted));
    layout->addWidget(hint);

    // Scenario cards: vertical list inside a scroll area, so the panel stays usable when the
    // compiled-in table outgrows the column height.
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    QWidget* cardHost = new QWidget();
    cardHost->setStyleSheet(Hexa::Styles::PanelTransparent);
    QVBoxLayout* cardLayout = new QVBoxLayout(cardHost);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(8);
    for (const GuideScenario& scenario : GuideScenarios::all()) {
        auto* card = new GuideScenarioCard(scenario, cardHost);
        connect(card, &GuideScenarioCard::cardClicked, this, &GuidePanel::onCardClicked);
        cardLayout->addWidget(card);
        m_cards.append(card);
    }
    cardLayout->addStretch();
    scroll->setWidget(cardHost);
    layout->addWidget(scroll, 1);

    // Pace chips (shared, not per-card — one global mode, less state).
    QHBoxLayout* modeRow = new QHBoxLayout();
    modeRow->setSpacing(8);
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);
    // TRY first and default: a first-contact user learns by pressing the real buttons.
    const struct {
        const char* label;
        const char* tooltip;
        int id;
    } kModeChips[] = {
        {"TRY", "Hands-on: you press the real buttons; the guide waits and helps", kModeIdTry},
        {"STEP", "Presenter-paced: advance with NEXT on the instruction card", kModeIdStep},
        {"PLAY", "Self-running: steps advance on their own timers", kModeIdPlay},
    };
    for (const auto& chip : kModeChips) {
        QPushButton* button = new QPushButton(QString::fromLatin1(chip.label), this);
        button->setCheckable(true);
        button->setFixedHeight(kModeChipHeightPx);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(QString::fromLatin1(chip.tooltip));
        button->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: 1px solid %1; border-radius: 6px;"
            " color: %2; font-family: '%3'; font-size: %9px; font-weight: 600; }"
            "QPushButton:checked { background-color: %4; border-color: %5; color: %5; }"
            "QPushButton:hover:!checked { color: %6; }"
            "QPushButton:disabled { border-color: %7; color: %8; }")
            .arg(Hexa::Colors::Hairline, Hexa::Colors::TextMuted, Hexa::Fonts::familyUI(),
                 guideAccentTint(), Hexa::Colors::Active, Hexa::Colors::Accent,
                 Hexa::Colors::HairlineSoft, Hexa::Colors::TextDisabled)
            .arg(kChipFontPx));
        m_modeGroup->addButton(button, chip.id);
        modeRow->addWidget(button, 1);
    }
    m_modeGroup->button(kModeIdTry)->setChecked(true);   // TRY is the default (boss 2026-07-07)
    layout->addLayout(modeRow);

    m_btnStartStop = HexaWidgets::createButtonPrimary(QStringLiteral("START"), this,
                                                      0, Hexa::Dim::BtnTouch);
    connect(m_btnStartStop, &QPushButton::clicked, this, &GuidePanel::onStartStopClicked);
    layout->addWidget(m_btnStartStop);

    m_lblStatus = new QLabel(this);
    m_lblStatus->setWordWrap(true);
    m_lblStatus->setFont(uiFont(kStatusFontPx, QFont::DemiBold));
    layout->addWidget(m_lblStatus);

    // Scenario progress mirror (boss review): the same filled accent bar the callout shows, so
    // the run's position is visible on the panel too, not only near the highlighted target.
    m_progressBar = new QProgressBar(this);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(kPanelProgressBarHeightPx);
    m_progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar { background-color: %1; border: 1px solid %2; border-radius: 3px; }"
        "QProgressBar::chunk { background-color: %3; border-radius: 2px; }")
        .arg(Hexa::Colors::CellInset, Hexa::Colors::HairlineSoft, Hexa::Colors::Active));
    m_progressBar->setVisible(false);
    layout->addWidget(m_progressBar);

    if (m_cards.isEmpty()) {
        // Authoring defect (the compiled-in table is empty): dead START would be a silent trap.
        m_btnStartStop->setEnabled(false);
        setStatus(QStringLiteral("NO SCENARIOS COMPILED IN"), true);
    } else {
        onCardClicked(m_cards.first()->scenarioId());
        setStatus(QStringLiteral("READY"), false);
    }
}

void GuidePanel::onCardClicked(const QString& scenarioId) {
    m_selectedScenarioId = scenarioId;
    for (GuideScenarioCard* card : m_cards) {
        card->setSelected(card->scenarioId() == scenarioId);
    }
}

void GuidePanel::onStartStopClicked() {
    if (m_running) {
        emit guideStopRequested();
        return;
    }
    emit guideStartRequested(m_selectedScenarioId, selectedMode());
}

GuideMode GuidePanel::selectedMode() const {
    switch (m_modeGroup->checkedId()) {
        case kModeIdStep: return GuideMode::Step;
        case kModeIdPlay: return GuideMode::Play;
        default:          return GuideMode::Try;   // TRY is the default chip
    }
}

void GuidePanel::onScenarioStarted(const QString& scenarioId, GuideMode mode, int stepCount) {
    Q_UNUSED(scenarioId)
    m_running = true;
    m_runningMode = mode;
    setRunningUi(true);
    setStatus(QStringLiteral("RUNNING (%1) — STEP 1 / %2").arg(toString(mode)).arg(stepCount),
              false);
    m_progressBar->setRange(0, stepCount);
    m_progressBar->setValue(1);
    m_progressBar->setVisible(true);
}

void GuidePanel::onStepEntered(int stepIndex, int stepCount) {
    setStatus(QStringLiteral("RUNNING (%1) — STEP %2 / %3")
                  .arg(toString(m_runningMode)).arg(stepIndex + 1).arg(stepCount),
              false);
    m_progressBar->setValue(stepIndex + 1);
}

void GuidePanel::onScenarioEnded(GuideOutcome outcome, const QString& detail) {
    m_running = false;
    setRunningUi(false);
    m_progressBar->setVisible(false);
    switch (outcome) {
        case GuideOutcome::Finished:
            setStatus(QStringLiteral("SCENARIO FINISHED"), false);
            break;
        case GuideOutcome::StoppedByUser:
            setStatus(QStringLiteral("STOPPED BY OPERATOR"), false);
            break;
        case GuideOutcome::Aborted:
            setStatus(QStringLiteral("ABORTED: %1").arg(detail), true);
            break;
    }
}

void GuidePanel::setRunningUi(bool running) {
    // One button, two identities: the primary CTA becomes the danger STOP while a scenario runs
    // (same pattern as RUN/STOP elsewhere in the HMI).
    m_btnStartStop->setText(running ? QStringLiteral("STOP") : QStringLiteral("START"));
    m_btnStartStop->setStyleSheet(running ? Hexa::Styles::ButtonDangerNormal
                                          : Hexa::Styles::ButtonPrimary);
    for (GuideScenarioCard* card : m_cards) {
        card->setEnabled(!running);
    }
    for (QAbstractButton* chip : m_modeGroup->buttons()) {
        chip->setEnabled(!running);
    }
}

void GuidePanel::setStatus(const QString& text, bool alert) {
    // Main text colour while informative (boss review: the run state must be readable at
    // presentation distance, not a footnote). Font is a real QFont set once in setupUi;
    // only the colour swaps here.
    m_lblStatus->setText(text);
    m_lblStatus->setStyleSheet(labelColorOnly(alert ? Hexa::Colors::Alert
                                                    : Hexa::Colors::TextMain));
}

} // namespace hexa
// --- END OF FILE: HexaStudio/guide/GuidePanel.cpp ---
