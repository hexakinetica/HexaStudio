// HexaWidgets.cpp
#include "HexaWidgets.h"
#include <QFontMetrics>
#include <QPainter>
#include <QPropertyAnimation>
#include <QMouseEvent>

#include <algorithm>

// --- PANELS ---
QFrame* HexaWidgets::createMainPanel(QWidget* parent) {
    QFrame* f = new QFrame(parent);
    f->setStyleSheet(Hexa::Styles::PanelMain);
    return f;
}

QFrame* HexaWidgets::createSeparatorV() {
    QFrame* f = new QFrame();
    f->setFrameShape(QFrame::VLine);
    f->setFixedWidth(1);
    f->setFixedHeight(24);
    f->setStyleSheet(Hexa::Styles::Separator);
    return f;
}

QFrame* HexaWidgets::createSeparatorH() {
    QFrame* f = new QFrame();
    f->setFrameShape(QFrame::HLine);
    f->setFixedHeight(1);
    f->setStyleSheet(Hexa::Styles::Separator);
    return f;
}

QFrame* HexaWidgets::createSectionSeparator() {
    QFrame* f = new QFrame();
    f->setFrameShape(QFrame::VLine);
    f->setFixedWidth(2);
    f->setFixedHeight(40);
    f->setStyleSheet(Hexa::Styles::SectionSeparator);
    return f;
}

// --- BUTTONS ---

QPushButton* HexaWidgets::createButtonStd(const QString& text, QWidget* parent, int w, int h) {
    QPushButton* btn = new QPushButton(text, parent);

    // Logic: If W provided -> Fixed. If 0 -> Flexible but robust.
    if (w > 0) {
        btn->setFixedWidth(w);
    } else {
        btn->setMinimumWidth(40); // Allow shrinking
        btn->setMaximumWidth(250);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    if (h > 0) btn->setFixedHeight(h);
    else btn->setFixedHeight(Hexa::Dim::BtnHeight);

    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    return btn;
}

QPushButton* HexaWidgets::createButtonPrimary(const QString& text, QWidget* parent, int w, int h) {
    QPushButton* btn = new QPushButton(text, parent);

    if (w > 0) {
        btn->setFixedWidth(w);
    } else {
        btn->setMinimumWidth(40);
        btn->setMaximumWidth(250);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    if (h > 0) btn->setFixedHeight(h);
    else btn->setFixedHeight(Hexa::Dim::BtnHeight);

    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(Hexa::Styles::ButtonPrimary);
    return btn;
}

QPushButton* HexaWidgets::createButtonSm(const QString& text, QWidget* parent, int w, int h) {
    QPushButton* btn = new QPushButton(text, parent);

    if (w > 0) {
        btn->setFixedWidth(w);
    } else {
        btn->setMinimumWidth(30); // Allow shrinking
        btn->setMaximumWidth(150);
        btn->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    }

    if (h > 0) btn->setFixedHeight(h);
    else btn->setFixedHeight(Hexa::Dim::BtnHeight);

    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonInteract);
    return btn;
}

QPushButton* HexaWidgets::createButtonIcon(const QString& text, QWidget* parent, int w, int h) {
    QPushButton* btn = new QPushButton(text, parent);

    if (w > 0) {
        // Even if fixed width is requested, allow slight compression if layout is tight
        btn->setMinimumWidth(w / 2);
        btn->setMaximumWidth(w);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    } else {
        btn->setMinimumWidth(24); // Very small minimum
        btn->setMaximumWidth(60);
        btn->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    }

    if (h > 0) btn->setFixedHeight(h);
    else btn->setFixedHeight(Hexa::Dim::BtnIconSize);

    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(Hexa::Styles::ButtonIcon + Hexa::Styles::ButtonInteract);
    return btn;
}

QPushButton* HexaWidgets::createButtonDanger(const QString& text, QWidget* parent, int w, int h) {
    QPushButton* btn = new QPushButton(text, parent);

    if (w > 0) {
        btn->setFixedWidth(w);
    } else {
        btn->setMinimumWidth(40);
        btn->setMaximumWidth(200);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    if (h > 0) btn->setFixedHeight(h);
    else btn->setFixedHeight(Hexa::Dim::BtnHeight);

    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(Hexa::Styles::ButtonDangerNormal);
    return btn;
}

// --- INPUTS ---
QComboBox* HexaWidgets::createComboBox(QWidget* parent) {
    QComboBox* cb = new QComboBox(parent);
    cb->setFixedHeight(Hexa::Dim::BtnHeight);
    cb->setCursor(Qt::PointingHandCursor);
    cb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    cb->setMinimumWidth(40);
    cb->setStyleSheet(Hexa::Styles::ComboBox);
    return cb;
}

// --- LABELS ---
QLabel* HexaWidgets::createLabelHeader(const QString& text) {
    QLabel* l = new QLabel(text);
    l->setFont(Hexa::Fonts::getHeader(14));
    l->setStyleSheet(Hexa::Styles::LabelBorderless.arg(Hexa::Colors::TextMain, Hexa::Fonts::familyHeader(), "14"));
    return l;
}

QLabel* HexaWidgets::createLabelSectionTitle(const QString& text) {
    // Section overline in the UI font (letter-spaced small caps); Michroma stays brand-only.
    QLabel* l = new QLabel(text);
    l->setAlignment(Qt::AlignCenter);
    l->setStyleSheet(Hexa::Styles::LabelHeaderSimple);
    return l;
}

QLabel* HexaWidgets::createLabelAxis(const QString& text) {
    QLabel* l = new QLabel(text);
    l->setAlignment(Qt::AlignCenter);
    l->setMinimumWidth(15); // Allow shrinking
    l->setStyleSheet(Hexa::Styles::LabelBorderless.arg(Hexa::Colors::Accent, Hexa::Fonts::familyUI(), "12"));
    return l;
}

namespace {
// The ONE status font, used both for rendering (createLabelStatus sets it on the widget) and for
// sizing (statusLabelMinWidth measures with it). Styles::LabelStatusBase deliberately carries no
// font properties: a stylesheet font is invisible to size hints and font metrics, and that gap is
// exactly how the former status pill clipped its widest word.
QFont statusLabelFont() {
    QFont font(Hexa::Fonts::familyMono());
    font.setPixelSize(Hexa::Dim::StatusFontPx);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, Hexa::Dim::StatusLetterSpacingPx);
    return font;
}
} // namespace

QLabel* HexaWidgets::createLabelStatus(const QString& text) {
    QLabel* l = new QLabel(text);
    l->setFont(statusLabelFont());
    l->setFixedHeight(Hexa::Dim::BtnHeight);
    l->setAlignment(Qt::AlignCenter);
    l->setMinimumWidth(40); // hosts widen it via statusLabelMinWidth over their status vocabulary
    l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    updateStatusLabel(l, Hexa::State::Success);
    return l;
}

int HexaWidgets::statusLabelMinWidth(const QStringList& texts) {
    // Measures with the exact font createLabelStatus renders with (same QFont object recipe,
    // including the synthesized semibold - only the Regular Plex Mono face ships in resources -
    // and the per-character letter spacing), so the width can never disagree with the rendering.
    const QFontMetrics metrics(statusLabelFont());
    int widestTextPx = 0;
    for (const QString& text : texts) {
        widestTextPx = std::max(widestTextPx, metrics.horizontalAdvance(text));
    }
    return widestTextPx + 2 * Hexa::Dim::StatusSidePaddingPx + 2 * Hexa::Dim::BorderWidth;
}

QLabel* HexaWidgets::createBadge(const QString& text, int minWidth, const QString& bg, const QString& fg) {
    QLabel* l = new QLabel(text);
    l->setAlignment(Qt::AlignCenter);
    if (minWidth > 0) l->setMinimumWidth(minWidth);
    l->setStyleSheet(Hexa::Styles::BadgeBase.arg(bg, fg));
    return l;
}

QLabel* HexaWidgets::createLabelText(const QString& text) {
    QLabel* l = new QLabel(text);
    l->setStyleSheet(Hexa::Styles::LabelBorderless.arg(Hexa::Colors::TextMuted, Hexa::Fonts::familyUI(), "11"));
    return l;
}

QLabel* HexaWidgets::createLabelData(const QString& text) {
    QLabel* l = new QLabel(text);
    l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    l->setStyleSheet(Hexa::Styles::LabelBorderless.arg(Hexa::Colors::Accent, Hexa::Fonts::familyMono(), "14"));
    return l;
}

// --- LOGIC ---
void HexaWidgets::updateStatusLabel(QLabel* lbl, Hexa::State state) {
    if (!lbl) return;
    QString stateColor;
    switch (state) {
    case Hexa::State::Success: stateColor = Hexa::Colors::Success; break;
    case Hexa::State::Error:   stateColor = Hexa::Colors::Alert;   break;
    case Hexa::State::Warning: stateColor = Hexa::Colors::Warning; break;
    case Hexa::State::Active:  stateColor = Hexa::Colors::Active;  break;
    default:                   stateColor = Hexa::Colors::TextMuted; break;
    }

    // Status pill: fill, border and text all derive from the ONE state colour (fill at low alpha,
    // border a little stronger, text at full strength) - state reads from across the cell.
    const QColor c(stateColor);
    const QString fill   = QStringLiteral("rgba(%1, %2, %3, 0.14)").arg(c.red()).arg(c.green()).arg(c.blue());
    const QString border = QStringLiteral("rgba(%1, %2, %3, 0.40)").arg(c.red()).arg(c.green()).arg(c.blue());
    lbl->setStyleSheet(Hexa::Styles::LabelStatusBase.arg(fill, border, stateColor));
}

void HexaWidgets::setBadge(QLabel* lbl, const QString& text, const QString& bg, const QString& fg) {
    if (!lbl) return;
    lbl->setText(text);
    lbl->setStyleSheet(Hexa::Styles::BadgeBase.arg(bg, fg));
}

void HexaWidgets::setBadge(QLabel* lbl, const QString& text, Hexa::State state) {
    switch (state) {
    case Hexa::State::Success: setBadge(lbl, text, Hexa::Colors::BadgeOk,      Hexa::Colors::BadgeFgLight); break;
    case Hexa::State::Error:   setBadge(lbl, text, Hexa::Colors::BadgeError,   Hexa::Colors::BadgeFgLight); break;
    case Hexa::State::Warning: setBadge(lbl, text, Hexa::Colors::BadgeWarn,    Hexa::Colors::BadgeFgDark);  break;
    case Hexa::State::Active:  setBadge(lbl, text, Hexa::Colors::BadgeActive,  Hexa::Colors::BadgeFgLight); break;
    default:                   setBadge(lbl, text, Hexa::Colors::BadgeNeutralBg, Hexa::Colors::BadgeNeutralFg); break;
    }
}

void HexaWidgets::updateButtonDangerState(QPushButton* btn, bool isActive) {
    if (isActive) {
        btn->setStyleSheet(Hexa::Styles::ButtonDangerActive);
    } else {
        btn->setStyleSheet(Hexa::Styles::ButtonDangerNormal);
    }
}

// --- TOGGLE ---
HexaToggle::HexaToggle(QWidget *parent) : QAbstractButton(parent), m_offset(0) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(56, 28);   // finger-sized on the 10-inch panel
}
QSize HexaToggle::sizeHint() const { return QSize(56, 28); }
void HexaToggle::checkStateSet() {
    QPropertyAnimation *anim = new QPropertyAnimation(this, "offset", this);
    anim->setDuration(150);
    anim->setStartValue(isChecked() ? 0 : 100);
    anim->setEndValue(isChecked() ? 100 : 0);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
void HexaToggle::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) click();
}
void HexaToggle::resizeEvent(QResizeEvent *) { m_offset = isChecked() ? 100 : 0; }
void HexaToggle::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // ON = the accent (a real state, not decoration); OFF = neutral badge fill. Disabled toggles
    // fade the whole control so a locked mode switch reads as OFF-limits.
    QColor colBgOff = QColor(Hexa::Colors::BadgeNeutralBg);
    QColor colBgOn  = QColor(Hexa::Colors::Primary);
    QColor colThumb = QColor(Hexa::Colors::TextMain);
    if (!isEnabled()) {
        colBgOff.setAlphaF(0.45);
        colBgOn.setAlphaF(0.35);
        colThumb.setAlphaF(0.40);
    }
    const qreal trackRadius = (height() - 2) / 2.0;
    QRectF trackRect = rect().adjusted(1, 1, -1, -1);
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? colBgOn : colBgOff);
    p.drawRoundedRect(trackRect, trackRadius, trackRadius);
    int padding = 3;
    int thumbSize = height() - (padding * 2);
    double progress = m_offset / 100.0;
    double minX = padding;
    double maxX = width() - thumbSize - padding;
    double x = minX + (maxX - minX) * progress;
    p.setBrush(colThumb);
    p.drawEllipse(QRectF(x, padding, thumbSize, thumbSize));
}

QPushButton* HexaWidgets::createTitleButton(const QString& text, QWidget* parent) {
    QPushButton* btn = new QPushButton(text, parent);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(24);
    btn->setStyleSheet(QString(
                           "QPushButton { "
                           "   background: transparent; border: none; text-align: center; "
                           "   font-family: '%1'; font-size: 11px; font-weight: 600; letter-spacing: 2px; color: %2; "
                           "   border-bottom: 1px solid %3;"
                           "}"
                           "QPushButton:hover { color: %4; }"
                           "QPushButton:checked { border-bottom: 2px solid %5; color: %4; }"
                           ).arg(Hexa::Fonts::familyUI(), Hexa::Colors::TextMuted, Hexa::Colors::Hairline, Hexa::Colors::TextMain, Hexa::Colors::Primary));

    return btn;
}
