#ifndef HEXATHEME_H
#define HEXATHEME_H

#include <QString>
#include <QFont>

// =====================================================================================
// HexaStudioNG design language (v0.4.0, "industrial carbon"; boss-approved 2026-07-04).
// Target device: 10-inch touch panel (~1280x800). Principles:
//   - near-black carbon window with layered panel surfaces (base -> surface -> card),
//     separation by lightness, hairline borders instead of saturated outlines;
//   - two visual planes: CONTROLS keep the lighter graphite fill and the soft Radius
//     (explicit boss decision 2026-07-04 - buttons must read brighter than the data
//     plane); DATA lives in near-black inset cells (CellInset) with the sharper
//     RadiusCell, so instruments read as machined readouts, not web badges;
//   - ONE accent (teal) used sparingly; state colours muted but unambiguous;
//   - generous touch targets (42px standard / 48px gloved);
//   - Michroma is the brand wordmark only; all UI text is Segoe UI, data is Plex Mono.
// Safety surfaces (E-STOP, danger buttons) intentionally stay the loudest elements.
// =====================================================================================

namespace Hexa {

enum class State { Normal, Success, Warning, Error, Active };

namespace Colors {
// Layered carbon backgrounds: near-black window, panels visibly lighter. The deeper base plus
// stronger hairlines give instrument contrast instead of the previous washed graphite.
const QString Background    = "#050608";
const QString Surface       = "#0C0F13";
const QString SurfaceLight  = "#151A21";

// Instrument-cell fill (DRO value cells, status block, stats cells): darker than any surface, so
// data readouts sit visually BEHIND the panel like an inset display window.
const QString CellInset     = "#04060A";

// Control fill: a barely-visible vertical gradient gives buttons body without gloss. Deliberately
// UNCHANGED by the v0.4.0 carbon pass (boss decision 2026-07-04): buttons keep the lighter
// graphite fill so the control plane reads brighter than the data plane.
const QString ButtonNormal  = "qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #262C36, stop:1 #1E242D)";

// Hairline separators/borders: white at low alpha reads as machined edges, not outlines.
const QString Hairline      = "rgba(255, 255, 255, 0.10)";
const QString HairlineSoft  = "rgba(255, 255, 255, 0.06)";

const QString Border        = "#3A7A74";   // muted teal, pressed-state fill of the primary CTA

const QString Primary       = "#57D9CE";   // the single accent
const QString PrimaryBright = "#7CE8DF";   // hover shade of Primary
const QString Accent        = "#B9C1CC";   // secondary text / idle button labels
const QString Active        = "#9D8CFF";   // "motion active" identity (violet, softened)
const QString Execution     = "#FFD700";   // executing program row highlight (carried over from the shipping theme)
const QString Alert         = "#E5484D";
const QString Success       = "#3ECF8E";
const QString Warning       = "#E5B454";
// Translucent Warning for cell/field backgrounds (e.g. an axis clamped at its limit).
const QString WarningSoft   = "rgba(229, 180, 84, 0.22)";

const QString TextMain      = "#ECEFF4";   // off-white: pure #FFF glares on a dark panel
const QString TextMuted     = "#7E8894";
const QString TextDisabled  = "#4A525C";   // dimmer than TextMuted: gated controls must read as OFF

const QString StateHover    = "rgba(87, 217, 206, 0.12)";
const QString StatePressed  = "rgba(87, 217, 206, 0.22)";

// Filled status-badge palette (muted fills, same semantics as the main state colours).
const QString BadgeNeutralBg = "#262B33";
const QString BadgeNeutralFg = "#8A93A0";
const QString BadgeError     = "#B3393E"; // not-connected / error
const QString BadgeOk        = "#2E7D4F"; // connected / completed
const QString BadgeActive    = "#6E56CF"; // in-progress (e.g. homing)
const QString BadgeWarn      = "#C99A3C"; // warning (dark fg)
const QString BadgeFgLight   = "#F2F4F7";
const QString BadgeFgDark    = "#14171D";
}

namespace Dim {
const int Radius        = 10;   // buttons, combos, cards (control plane - soft)
const int RadiusSmall   = 6;    // badges, inline fields
const int RadiusCell    = 4;    // instrument/data cells (DRO, status block, stats) - sharper than controls
const int PanelPadding  = 12;
const int ItemSpacing   = 8;
const int BorderWidth   = 1;

// System-status block metrics. Single source for BOTH the stylesheet (Styles::LabelStatusBase)
// and the font-metrics sizing (HexaWidgets::statusLabelMinWidth) - the two must never diverge,
// otherwise the widest status word clips again.
const int StatusFontPx          = 13;
const int StatusLetterSpacingPx = 1;
const int StatusSidePaddingPx   = 14;

// Button height scale (10-inch touch panel). Three named sizes instead of per-call literals:
const int BtnCompact    = 30;   // inline title-row actions (UNDO/REDO, collapse toggles)
const int BtnHeight     = 42;   // standard controls (finger-sized)
const int BtnTouch      = 48;   // gloved-finger controls (jog arm, RUN/STOP class actions)
const int BtnIconSize   = 42;
}

namespace Fonts {
// Michroma is reserved for the brand wordmark; everything else is Segoe UI.
inline QString familyHeader() { return "Michroma"; }
// "Segoe UI" ships with every supported Windows workstation. The previous value ("Inter") was
// never bundled in resources/, so every UI-font widget silently fell back to an arbitrary
// substitute; naming an installed font makes the rendered look deterministic.
inline QString familyUI() { return "Segoe UI"; }
inline QString familyMono() { return "IBM Plex Mono"; }

inline QFont getHeader(int size = 12) { return QFont(familyHeader(), size, QFont::Bold); }
inline QFont getMono(int size = 12) { return QFont(familyMono(), size); }
inline QFont getInterface(int size = 12, bool bold = false) { return QFont(familyUI(), size, bold ? QFont::Bold : QFont::Normal); }
}

namespace Styles {

// --- PANELS ---
// Card surface: one lightness step above the window, hairline edge, soft corners. The shell wraps
// each work column in this frame so the HMI reads as three instruments on a bench.
const QString PanelMain = QStringLiteral("QFrame { background-color: %1; border: 1px solid %2; border-radius: 12px; }")
                              .arg(Colors::Surface, Colors::Hairline);

const QString PanelTransparent = QStringLiteral("QWidget { background: transparent; border: none; }");

const QString Separator = QStringLiteral("background-color: %1; border: none;").arg(Colors::HairlineSoft);
const QString SectionSeparator = QStringLiteral("background-color: %1; border: none;").arg(Colors::Hairline);

// --- SPLITTER ---
// Invisible until touched: the gap between cards is the handle; hover/drag shows the accent tint.
const QString Splitter = QStringLiteral(
                             "QSplitter::handle { background-color: transparent; }"
                             "QSplitter::handle:hover { background-color: %1; }"
                             ).arg(Colors::StateHover);

// --- LABELS ---
const QString LabelBorderless = QStringLiteral(
    "QLabel { border: none; background: transparent; color: %1; font-family: '%2'; font-size: %3px; padding: 0px; }"
    );

// Section overline: quiet, letter-spaced small caps in the UI font (not the brand font).
const QString LabelHeaderSimple = QStringLiteral(
                                      "QLabel { border: none; background: transparent; color: %1; font-family: '%2'; font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: 2px; }"
                                      ).arg(Colors::TextMuted, Fonts::familyUI());

// System-status block (one short word, RadiusCell corners - an instrument, not a web pill).
// Deliberately NO font properties here: the status font is a real QFont set in
// HexaWidgets::createLabelStatus from the same Dim::Status* constants, because a stylesheet font
// does not participate in the label's size hint / font metrics - that gap is exactly how the
// previous pill clipped its text. Markers %1 (padding) and %2 (radius) are filled at definition;
// %3 background, %4 border and %5 text are state-tinted at runtime by updateStatusLabel.
const QString LabelStatusBase = QStringLiteral(
                                    "QLabel { background-color: %3; padding: 0 %1px;"
                                    " border-radius: %2px; border: 1px solid %4; color: %5; }"
                                    ).arg(QString::number(Dim::StatusSidePaddingPx),
                                          QString::number(Dim::RadiusCell));

// Filled status badge. %1 = background color, %2 = text color (filled at call site via .arg()).
const QString BadgeBase = QStringLiteral(
                              "background-color: %1; color: %2; font-weight: 600; padding: 4px 10px; border-radius: 6px;");


// --- BUTTONS ---
// Every button style carries an explicit :disabled rule. Without it Qt keeps the enabled colors,
// so a gated control looks live - unacceptable on an operator HMI.
const QString ButtonBase = QString(
                               "QPushButton { "
                               "   background-color: %1; "
                               "   border: 1px solid %2; "
                               "   color: %3; "
                               "   border-radius: %4px; "
                               "   font-family: '%5'; "
                               "   font-size: 12px; "
                               "}"
                               "QPushButton:disabled { background-color: rgba(255, 255, 255, 0.03);"
                               " border-color: %6; color: %7; }"
                               ).arg(Colors::ButtonNormal, Colors::Hairline, Colors::Accent, QString::number(Dim::Radius), Fonts::familyUI(), Colors::HairlineSoft, Colors::TextDisabled);

const QString ButtonInteract = QString(
                                   "QPushButton:hover { background-color: #2B323D; border-color: rgba(87, 217, 206, 0.45); color: %1; }"
                                   "QPushButton:pressed { background-color: %2; border-color: %3; color: %1; }"
                                   "QPushButton:checked { background-color: %2; border-color: %3; color: %1; }"
                                   ).arg(Colors::TextMain, Colors::StatePressed, Colors::Primary);

// Filled call-to-action: at most ONE per panel (RUN, ENABLE JOG). The operator must find the
// panel's main action by peripheral vision, not by reading labels.
const QString ButtonPrimary = QString(
                                  "QPushButton { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #63E0D5, stop:1 #45C4B8);"
                                  " border: none; color: %1;"
                                  " border-radius: %2px; font-family: '%3'; font-size: 13px; font-weight: 600; }"
                                  "QPushButton:hover { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #7CE8DF, stop:1 #57D9CE); }"
                                  "QPushButton:pressed { background-color: %4; }"
                                  "QPushButton:disabled { background-color: rgba(87, 217, 206, 0.16);"
                                  " color: rgba(14, 16, 20, 0.55); }"
                                  ).arg(Colors::Background, QString::number(Dim::Radius), Fonts::familyUI(), Colors::Border);

// Glyph button (+/-, arrows): the standard button with a larger glyph-friendly font. A dedicated
// constant so the factory does not string-patch the base style at runtime.
const QString ButtonIcon = QString(
                               "QPushButton { background-color: %1; border: 1px solid %2; color: %3;"
                               " border-radius: %4px; font-family: '%5'; font-size: 18px; }"
                               "QPushButton:disabled { background-color: rgba(255, 255, 255, 0.03);"
                               " border-color: %6; color: %7; }"
                               ).arg(Colors::ButtonNormal, Colors::Hairline, Colors::Accent, QString::number(Dim::Radius), Fonts::familyUI(), Colors::HairlineSoft, Colors::TextDisabled);

// Latched warning state for checkable mode buttons (e.g. BLEND): amber fill while checked so a
// non-default recording mode stays visible at a glance. Append AFTER ButtonInteract - the later
// :checked rule wins at equal specificity.
const QString ButtonCheckedWarning = QStringLiteral(
                                         "QPushButton:checked { background-color: %1; border-color: %1;"
                                         " color: %2; font-weight: 600; }")
                                         .arg(Colors::Warning, Colors::BadgeFgDark);

const QString ButtonDangerNormal = QString(
                                       "QPushButton { background-color: rgba(229, 72, 77, 0.10); border: 1px solid rgba(229, 72, 77, 0.55);"
                                       " color: #F2555A; border-radius: %2px; font-family: '%3'; font-size: 12px; font-weight: 600; }"
                                       "QPushButton:hover { background-color: %1; border-color: %1; color: white; }"
                                       "QPushButton:disabled { background-color: transparent;"
                                       " border-color: rgba(229, 72, 77, 0.22); color: rgba(229, 72, 77, 0.35); }"
                                       ).arg(Colors::Alert, QString::number(Dim::Radius), Fonts::familyUI());

const QString ButtonDangerActive = QString(
                                       "QPushButton { background-color: %1; border: 1px solid rgba(255, 255, 255, 0.35); color: white;"
                                       " border-radius: %2px; font-family: '%3'; font-size: 12px; font-weight: 700; }"
                                       "QPushButton:hover { background-color: #F05B60; }"
                                       ).arg(Colors::Alert, QString::number(Dim::Radius), Fonts::familyUI());

const QString ListView = QString(
                             "QListView { background-color: rgba(0, 0, 0, 0.18); border: 1px solid %1; border-radius: %2px; outline: none; padding: 4px; }"
                             "QListView::item:selected { background-color: %3; border: 1px solid rgba(87, 217, 206, 0.35); border-radius: %4px; }"
                             ).arg(Colors::HairlineSoft, QString::number(Dim::Radius), Colors::StateHover, QString::number(Dim::RadiusSmall));

// --- COMBO BOX ---
const QString ComboBox = QString(
                             "QComboBox { background-color: #1B2027; border: 1px solid %1; border-radius: %2px; padding: 4px 12px; color: %3; font-family: '%4'; font-size: 12px; }"
                             "QComboBox:disabled { border-color: %5; color: %6; }"
                             "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 22px; border-left: none; }"
                             "QComboBox QAbstractItemView { background-color: %7; border: 1px solid %1; border-radius: 6px; selection-background-color: %8; color: %3; outline: none; }"
                             "QComboBox QAbstractItemView::item { padding: 8px 10px; }"
                             "QComboBox QAbstractItemView::item:selected { background-color: %8; color: %9; }"
                             ).arg(Colors::Hairline, QString::number(Dim::Radius), Colors::TextMain, Fonts::familyUI(), Colors::HairlineSoft, Colors::TextDisabled, Colors::SurfaceLight, Colors::StateHover, Colors::TextMain);

}
}

#endif // HEXATHEME_H
