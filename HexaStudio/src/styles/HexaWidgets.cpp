// HexaWidgets.cpp
#include "HexaWidgets.h"
#include <QPainter>
#include <QPropertyAnimation>
#include <QMouseEvent>

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
    btn->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonPrimaryAddon);
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
    btn->setStyleSheet(Hexa::Styles::ButtonBase + Hexa::Styles::ButtonPrimaryAddon);
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

    QString iconStyle = Hexa::Styles::ButtonBase + Hexa::Styles::ButtonPrimaryAddon;
    iconStyle.replace("font-size: 11px;", "font-size: 16px;");
    btn->setStyleSheet(iconStyle);
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
    QLabel* l = new QLabel(text);
    l->setAlignment(Qt::AlignCenter);
    l->setStyleSheet(Hexa::Styles::LabelBorderless.arg(Hexa::Colors::TextMain, Hexa::Fonts::familyHeader(), "11"));
    return l;
}

QLabel* HexaWidgets::createLabelAxis(const QString& text) {
    QLabel* l = new QLabel(text);
    l->setAlignment(Qt::AlignCenter);
    l->setMinimumWidth(15); // Allow shrinking
    l->setStyleSheet(Hexa::Styles::LabelBorderless.arg(Hexa::Colors::Accent, Hexa::Fonts::familyUI(), "12"));
    return l;
}

QLabel* HexaWidgets::createLabelStatus(const QString& text) {
    QLabel* l = new QLabel(text);
    l->setFixedHeight(Hexa::Dim::BtnHeight);
    l->setAlignment(Qt::AlignCenter);
    l->setMinimumWidth(40); // Allow shrinking
    l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    // Set initial state
    updateStatusLabel(l, Hexa::State::Success);
    l->setText(text); // Set text after style to ensure it's visible
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
    QString borderColor;
    QString textColor = Hexa::Colors::Accent; // Default text color

    switch (state) {
    case Hexa::State::Success: borderColor = Hexa::Colors::Success; break;
    case Hexa::State::Error:   borderColor = Hexa::Colors::Alert;   break;
    case Hexa::State::Warning: borderColor = Hexa::Colors::Warning; break;
    case Hexa::State::Active:  borderColor = Hexa::Colors::Active;  break;
    default:                   borderColor = Hexa::Colors::TextMuted; break;
    }

    // FIX: The base string now has two placeholders (%3, %4) that need to be filled for colors.
    // We use a temporary string to avoid issues with the base constant.
    QString finalStyle = Hexa::Styles::LabelStatusBase;
    lbl->setStyleSheet(finalStyle.arg(borderColor, textColor));
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
    setFixedSize(50, 24);
}
QSize HexaToggle::sizeHint() const { return QSize(50, 24); }
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
    QColor colBgOff = QColor(Hexa::Colors::SurfaceLight);
    QColor colBgOn  = QColor(Hexa::Colors::Accent);
    QColor colThumb = QColor(Hexa::Colors::TextMain);
    QRectF trackRect = rect().adjusted(1, 1, -1, -1);
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? colBgOn : colBgOff);
    p.drawRoundedRect(trackRect, 11, 11);
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
                           "   font-family: '%1'; font-size: 11px; font-weight: bold; color: %2; "
                           "   border-bottom: 1px solid %3;"
                           "}"
                           "QPushButton:hover { color: %4; }"
                           "QPushButton:checked { border-bottom: 2px solid %5; }"
                           ).arg(Hexa::Fonts::familyHeader(), Hexa::Colors::TextMuted, Hexa::Colors::Border, Hexa::Colors::TextMain, Hexa::Colors::Accent));

    return btn;
}
