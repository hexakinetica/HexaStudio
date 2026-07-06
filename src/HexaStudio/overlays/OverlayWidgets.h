// --- START OF FILE: HexaStudio/overlays/OverlayWidgets.h ---
/**
 * @file OverlayWidgets.h
 * @brief Shared themed building blocks for the overlays module panels (SettingsPanel, HalPanel).
 *
 * One visual language for every overlay editor: an inset section card with a header-styled title,
 * one dark mono field style for all inputs, and a muted caption. Extracted because two panels in
 * this module use the identical blocks (real duplication, not speculative generality).
 */
#ifndef HEXA_OVERLAY_WIDGETS_H
#define HEXA_OVERLAY_WIDGETS_H

#include <QString>

class QFrame;
class QLabel;
class QVBoxLayout;

namespace hexa {
namespace overlay {

// Stylesheet for QLineEdit + QAbstractSpinBox: dark inset, theme border, white mono value.
QString fieldStyle();

// Inset section card: darker than the overlay frame, header-styled title, separator, content below.
QFrame* createCard(const QString& title, QVBoxLayout** contentLayout);

// One-line muted description (replaces the shipping overlays' INFO side columns).
QLabel* createCaption(const QString& text);

} // namespace overlay
} // namespace hexa

#endif // HEXA_OVERLAY_WIDGETS_H
// --- END OF FILE: HexaStudio/overlays/OverlayWidgets.h ---
