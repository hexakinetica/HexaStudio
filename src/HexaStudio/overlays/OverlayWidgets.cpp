// --- START OF FILE: HexaStudio/overlays/OverlayWidgets.cpp ---
#include "OverlayWidgets.h"

#include "HexaTheme.h"
#include "HexaWidgets.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

namespace hexa {
namespace overlay {

QString fieldStyle() {
    return QStringLiteral(
        "QLineEdit, QAbstractSpinBox { background-color: rgba(0,0,0,0.45);"
        " border: 1px solid rgba(69, 162, 158, 0.5); border-radius: 4px; color: %1;"
        " padding: 4px 8px; font-family: '%2'; font-size: 12px;"
        " selection-background-color: rgba(102, 252, 241, 0.3); }"
        "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {"
        " width: 18px; background-color: rgba(69, 162, 158, 0.18); border: none; }"
        "QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover {"
        " background-color: rgba(102, 252, 241, 0.25); }")
        .arg(Hexa::Colors::TextMain, Hexa::Fonts::familyMono());
}

QFrame* createCard(const QString& title, QVBoxLayout** contentLayout) {
    QFrame* card = new QFrame();
    // Slightly darker than the overlay frame (Surface) so the card reads as an inset section.
    card->setStyleSheet(QStringLiteral(
        "QFrame { background-color: rgba(0,0,0,0.25); border: 1px solid rgba(69, 162, 158, 0.35);"
        " border-radius: 4px; }"));
    QVBoxLayout* v = new QVBoxLayout(card);
    v->setContentsMargins(12, 10, 12, 12);
    v->setSpacing(8);
    QLabel* lblTitle = new QLabel(title, card);
    lblTitle->setStyleSheet(Hexa::Styles::LabelHeaderSimple);
    v->addWidget(lblTitle);
    v->addWidget(HexaWidgets::createSeparatorH());
    *contentLayout = v;
    return card;
}

QLabel* createCaption(const QString& text) {
    QLabel* caption = new QLabel(text);
    caption->setWordWrap(true);
    caption->setStyleSheet(Hexa::Styles::LabelBorderless.arg(
        Hexa::Colors::TextMuted, Hexa::Fonts::familyUI(), "11"));
    return caption;
}

} // namespace overlay
} // namespace hexa
// --- END OF FILE: HexaStudio/overlays/OverlayWidgets.cpp ---
