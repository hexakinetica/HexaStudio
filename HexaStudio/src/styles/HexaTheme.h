#ifndef HEXATHEME_H
#define HEXATHEME_H

#include <QString>
#include <QFont>

namespace Hexa {

enum class State { Normal, Success, Warning, Error, Active };

namespace Colors {
const QString Background    = "#0B0C10";
const QString Surface       = "#1F2833";
const QString SurfaceLight  = "#2C3E50";

const QString ButtonNormal  = "rgba(31, 40, 51, 0.6)";

const QString Border        = "#45A29E";
const QString BorderThick   = "#66FCF1";

const QString Primary       = "#66FCF1";
const QString Accent        = "#C5C6C7";
const QString Active        = "#9F50FF";
const QString Alert         = "#FF3131";
const QString Success       = "#00E676";
const QString Warning       = "#FFD700";

// Missing color added
const QString Execution     = "#FFD700";

const QString TextMain      = "#FFFFFF";
const QString TextMuted     = "#8899A6";

const QString StateHover    = "rgba(102, 252, 241, 0.15)";
const QString StatePressed  = "rgba(102, 252, 241, 0.3)";
}

namespace Dim {
const int Radius        = 4;
const int RadiusLg      = 6;
const int PanelPadding  = 10;
const int ItemSpacing   = 8;
const int BorderWidth   = 1;

const int BtnHeight     = 36;
const int BtnWidthSm    = 80;
const int BtnWidthStd   = 140;
const int BtnIconSize   = 36;
}

namespace Fonts {
inline QString familyHeader() { return "Michroma"; }
inline QString familyUI() { return "Inter"; }
inline QString familyMono() { return "IBM Plex Mono"; }

inline QFont getHeader(int size = 12) { return QFont(familyHeader(), size, QFont::Bold); }
inline QFont getMono(int size = 12) { return QFont(familyMono(), size); }
inline QFont getInterface(int size = 11, bool bold = false) { return QFont(familyUI(), size, bold ? QFont::Bold : QFont::Normal); }
}

namespace Styles {

// --- PANELS ---
const QString PanelMain = QStringLiteral("QFrame { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
                              .arg(Colors::Surface, QStringLiteral("rgba(69, 162, 158, 0.3)"), QString::number(Dim::Radius));

const QString PanelTransparent = QStringLiteral("QWidget { background: transparent; border: none; }");

const QString Separator = QStringLiteral("background-color: %1; border: none;").arg(QStringLiteral("rgba(69, 162, 158, 0.3)"));
const QString SectionSeparator = QStringLiteral("background-color: %1; border: none;").arg(Colors::Border);

// --- SPLITTER (Missing style added) ---
const QString Splitter = QStringLiteral(
                             "QSplitter::handle { background-color: rgba(69, 162, 158, 0.2); }"
                             "QSplitter::handle:hover { background-color: %1; }"
                             ).arg(Colors::Primary);

// --- LABELS ---
const QString LabelBorderless = QStringLiteral(
    "QLabel { border: none; background: transparent; color: %1; font-family: '%2'; font-size: %3px; padding: 0px; }"
    );

const QString LabelHeaderSimple = QStringLiteral(
                                      "QLabel { border: none; background: transparent; color: %1; font-family: '%2'; font-size: 11px; text-transform: uppercase; letter-spacing: 1px; }"
                                      ).arg(Colors::Primary, Fonts::familyHeader());

const QString LabelStatusBase = QStringLiteral(
                                    "QLabel { font-family: '%1'; font-size: 12px; background-color: rgba(0,0,0,0.4); padding: 0 10px; border-radius: %2px; border-left: 2px solid %3; color: %4; }"
                                    ).arg(Fonts::familyMono(), QString::number(Dim::Radius));


// --- BUTTONS ---
const QString ButtonBase = QString(
                               "QPushButton { "
                               "   background-color: %1; "
                               "   border: 1px solid %2; "
                               "   color: %3; "
                               "   border-radius: %4px; "
                               "   font-family: '%5'; "
                               "   font-size: 11px; "
                               "}"
                               ).arg(Colors::ButtonNormal, "rgba(69, 162, 158, 0.5)", Colors::Accent, QString::number(Dim::Radius), Fonts::familyUI());

const QString ButtonRounded = ButtonBase;

const QString ButtonInteract = QString(
                                   "QPushButton:hover { background-color: %1; border-color: %2; color: white; }"
                                   "QPushButton:pressed { background-color: %3; border-color: %2; color: white; }"
                                   "QPushButton:checked { background-color: %3; border-color: %2; color: white; }"
                                   ).arg(Colors::StateHover, Colors::Primary, Colors::StatePressed);

const QString ButtonPrimaryAddon = ButtonInteract;

const QString ButtonDangerNormal = QString(
                                       "QPushButton { background-color: rgba(255, 49, 49, 0.1); border: 1px solid %1; color: %1; border-radius: %2px; font-family: '%3'; font-size: 11px; }"
                                       "QPushButton:hover { background-color: %1; color: #000000; }"
                                       ).arg(Colors::Alert, QString::number(Dim::Radius), Fonts::familyUI());

const QString ButtonDangerActive = QString(
                                       "QPushButton { background-color: %1; border: 1px solid white; color: white; border-radius: %2px; font-family: '%3'; font-size: 11px; }"
                                       "QPushButton:hover { background-color: #FF5252; }"
                                       ).arg(Colors::Alert, QString::number(Dim::Radius), Fonts::familyUI());

const QString ButtonAlert = ButtonDangerNormal;

const QString ListView = QString(
                             "QListView { background-color: rgba(0,0,0,0.3); border: 1px solid %1; border-radius: %2px; outline: none; }"
                             "QListView::item:selected { background-color: rgba(102, 252, 241, 0.15); border: 1px solid %3; }"
                             ).arg("rgba(69, 162, 158, 0.3)", QString::number(Dim::Radius), Colors::Primary);

// --- COMBO BOX ---
const QString ComboBox = QString(
                             "QComboBox { background-color: %1; border: 1px solid %2; border-radius: %3px; padding: 4px; color: %4; font-family: '%5'; }"
                             "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 20px; border-left: none; }"
                             "QComboBox QAbstractItemView { background-color: #1F2833; border: 1px solid %6; selection-background-color: %7; color: %4; outline: none; }"
                             "QComboBox QAbstractItemView::item { padding: 5px; }"
                             "QComboBox QAbstractItemView::item:selected { background-color: %7; color: white; }"
                             ).arg("rgba(0,0,0,0.5)", "rgba(69, 162, 158, 0.5)", QString::number(Dim::Radius), Colors::TextMain, Fonts::familyUI(), Colors::Primary, Colors::StateHover);

}
}

#endif // HEXATHEME_H
