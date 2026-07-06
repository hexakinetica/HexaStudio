// --- START OF FILE: HexaStudio/guide/GuideCallout.cpp ---
#include "GuideCallout.h"

#include "HexaTheme.h"
#include "HexaWidgets.h"

#include <QFontMetrics>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace hexa {

namespace {
// Fixed card width: fits either side column (300 px) with the docking margin; the body label
// word-wraps within it and the card grows vertically only.
constexpr int kCalloutWidthPx = 280;
constexpr int kCalloutPaddingPx = 14;
constexpr int kCalloutButtonHeightPx = 42;
constexpr int kExitButtonWidthPx = 84;
constexpr int kNextButtonWidthPx = 118;
constexpr int kShowMeButtonWidthPx = 118;
constexpr int kHintFontPx = 14;
// Presentation-distance readability (boss review 2026-07-06, enlarged again on the second
// review): the callout is read from across the room during investor demos, so its type runs
// well above the panel-instrument scale.
constexpr int kProgressFontPx = 15;
constexpr int kTitleFontPx = 20;
constexpr int kBodyFontPx = 16;
constexpr int kProgressBarHeightPx = 10;

// Hover shade of the violet guide accent (Hexa::Colors::Active has no theme hover companion);
// pressed reuses the darker BadgeActive violet.
const QString kAccentHover = QStringLiteral("#B3A5FF");

// Wrapped/measured text gets a REAL QFont; stylesheets carry colours only. A stylesheet font is
// invisible to size hints and height-for-width (house precedent: HexaWidgets status label) —
// with QSS fonts the callout under-measures its labels and clips at real Segoe UI metrics.
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

// Set wrapped text and pin the exact height measured with the SAME QFont the label renders
// with (house precedent, jog readout cells): hint-driven heights under-measure wrapped labels
// and clipped the callout at real font metrics.
void setTextMeasured(QLabel* label, const QString& text) {
    label->setText(text);
    const int textHeightPx = QFontMetrics(label->font())
        .boundingRect(QRect(0, 0, label->width(), 0), Qt::TextWordWrap, text)
        .height();
    label->setFixedHeight(textHeightPx);
}
} // namespace

GuideCallout::GuideCallout(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("GuideCallout"));
    setFixedWidth(kCalloutWidthPx);
    applyFrameStyle(false);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kCalloutPaddingPx, kCalloutPaddingPx,
                               kCalloutPaddingPx, kCalloutPaddingPx);
    layout->setSpacing(10);

    // Violet is the guide's accent throughout (boss decision 2026-07-06): counter, progress
    // fill and NEXT match the pulse on the highlighted target.
    m_progress = new QLabel(this);
    QFont progressFont = uiFont(kProgressFontPx, QFont::Bold);
    progressFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    m_progress->setFont(progressFont);
    m_progress->setStyleSheet(labelColorOnly(Hexa::Colors::Active));
    layout->addWidget(m_progress);

    // Scenario progress at a glance (boss review): a filled accent bar under the step counter —
    // the audience sees how far the demo is without reading anything.
    m_progressBar = new QProgressBar(this);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(kProgressBarHeightPx);
    m_progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar { background-color: %1; border: 1px solid %2; border-radius: 4px; }"
        "QProgressBar::chunk { background-color: %3; border-radius: 3px; }")
        .arg(Hexa::Colors::CellInset, Hexa::Colors::HairlineSoft, Hexa::Colors::Active));
    layout->addWidget(m_progressBar);

    m_title = new QLabel(this);
    m_title->setWordWrap(true);
    m_title->setFixedWidth(kCalloutWidthPx - 2 * kCalloutPaddingPx);
    m_title->setFont(uiFont(kTitleFontPx, QFont::DemiBold));
    layout->addWidget(m_title);

    m_body = new QLabel(this);
    m_body->setWordWrap(true);
    m_body->setFixedWidth(kCalloutWidthPx - 2 * kCalloutPaddingPx);
    m_body->setFont(uiFont(kBodyFontPx));
    m_body->setStyleSheet(labelColorOnly(Hexa::Colors::TextMain));
    layout->addWidget(m_body);

    // TRY hint line: hidden until the user idles on a hands-on step, then shown in the violet
    // accent so "do this now" reads apart from the descriptive body.
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setFixedWidth(kCalloutWidthPx - 2 * kCalloutPaddingPx);
    QFont hintFont = uiFont(kHintFontPx, QFont::DemiBold);
    hintFont.setItalic(true);
    m_hint->setFont(hintFont);
    m_hint->setStyleSheet(labelColorOnly(Hexa::Colors::Active));
    m_hint->hide();
    layout->addWidget(m_hint);

    QHBoxLayout* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    m_exit = HexaWidgets::createButtonDanger(QStringLiteral("EXIT"), this,
                                             kExitButtonWidthPx, kCalloutButtonHeightPx);
    connect(m_exit, &QPushButton::clicked, this, &GuideCallout::exitRequested);
    buttonRow->addWidget(m_exit);
    buttonRow->addStretch();
    // SHOW ME (TRY fallback): an OUTLINED violet button — present, but quieter than the filled
    // NEXT, so the demo nudges the user to press the real control first.
    m_showMe = new QPushButton(QStringLiteral("SHOW ME"), this);
    m_showMe->setFixedSize(kShowMeButtonWidthPx, kCalloutButtonHeightPx);
    m_showMe->setCursor(Qt::PointingHandCursor);
    m_showMe->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: 1px solid %1; color: %1;"
        " border-radius: %2px; font-family: '%3'; font-size: 13px; font-weight: 700; }"
        "QPushButton:hover { background-color: %4; color: %5; }"
        "QPushButton:pressed { background-color: %6; color: %5; }")
        .arg(Hexa::Colors::Active, QString::number(Hexa::Dim::Radius), Hexa::Fonts::familyUI(),
             kAccentHover, Hexa::Colors::BadgeFgDark, Hexa::Colors::BadgeActive));
    connect(m_showMe, &QPushButton::clicked, this, &GuideCallout::showMeRequested);
    buttonRow->addWidget(m_showMe);
    // Violet-filled NEXT (not the teal ButtonPrimary): the callout's CTA carries the guide
    // accent, matching the pulse on the target it advances past.
    m_next = new QPushButton(QStringLiteral("NEXT"), this);
    m_next->setFixedSize(kNextButtonWidthPx, kCalloutButtonHeightPx);
    m_next->setCursor(Qt::PointingHandCursor);
    m_next->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; border: none; color: %2;"
        " border-radius: %3px; font-family: '%4'; font-size: 14px; font-weight: 700; }"
        "QPushButton:hover { background-color: %5; }"
        "QPushButton:pressed { background-color: %6; }")
        .arg(Hexa::Colors::Active, Hexa::Colors::BadgeFgDark,
             QString::number(Hexa::Dim::Radius), Hexa::Fonts::familyUI(),
             kAccentHover, Hexa::Colors::BadgeActive));
    connect(m_next, &QPushButton::clicked, this, &GuideCallout::nextRequested);
    buttonRow->addWidget(m_next);
    layout->addLayout(buttonRow);
}

void GuideCallout::showStep(int stepIndex, int stepCount, const QString& title,
                            const QString& body, bool nextVisible, bool showMeVisible) {
    applyFrameStyle(false);
    m_progress->setText(QStringLiteral("STEP %1 / %2").arg(stepIndex + 1).arg(stepCount));
    m_progressBar->setRange(0, stepCount);
    m_progressBar->setValue(stepIndex + 1);
    m_progressBar->setVisible(true);
    setTextMeasured(m_title, title);
    setTextMeasured(m_body, body);
    m_hint->hide();   // each step starts without a hint; TRY escalation reveals it via showHint()
    m_next->setVisible(nextVisible);
    m_showMe->setVisible(showMeVisible);
    adjustSize();
}

void GuideCallout::showHint(const QString& hintText) {
    if (hintText.isEmpty()) {
        return;
    }
    setTextMeasured(m_hint, hintText);
    m_hint->show();
    adjustSize();
}

void GuideCallout::showAbort(const QString& reason) {
    applyFrameStyle(true);
    m_progress->setText(QStringLiteral("GUIDED DEMO"));
    m_progressBar->setVisible(false);   // no progress to report — the reason is the message
    setTextMeasured(m_title, QStringLiteral("SCENARIO STOPPED"));
    setTextMeasured(m_body, reason);
    m_hint->hide();
    m_next->setVisible(false);
    m_showMe->setVisible(false);
    adjustSize();
}

void GuideCallout::applyFrameStyle(bool alert) {
    // ID selector: a bare QFrame rule would leak onto descendant frames (same precedent as the
    // shell cards). Normal frame carries the muted violet guide accent; alert swaps the border
    // and the title to the alert colour.
    setStyleSheet(QStringLiteral(
        "QFrame#GuideCallout { background-color: %1; border: 1px solid %2; border-radius: 12px; }")
        .arg(Hexa::Colors::SurfaceLight, alert ? Hexa::Colors::Alert : Hexa::Colors::BadgeActive));
    if (m_title) {
        // Font is a real QFont set once in the constructor; only the colour swaps with state.
        m_title->setStyleSheet(labelColorOnly(alert ? Hexa::Colors::Alert
                                                    : Hexa::Colors::TextMain));
    }
}

} // namespace hexa
// --- END OF FILE: HexaStudio/guide/GuideCallout.cpp ---
