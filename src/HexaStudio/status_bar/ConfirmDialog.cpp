// --- START OF FILE: HexaStudio/status_bar/ConfirmDialog.cpp ---
#include "ConfirmDialog.h"

#include "HexaTheme.h"
#include "HexaWidgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace hexa {

namespace {
constexpr int kDialogWidthPx = 360;
} // namespace

ConfirmDialog::ConfirmDialog(QWidget* parent) : QDialog(parent) {
    setModal(true);
    // Frameless + application background with a warning-coloured border: the dialog must read as a
    // pendant safety prompt, not as an OS window. CANCEL / confirm are the only exits (modal flow).
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QDialog { background-color: %1; border: 1px solid %2; }")
                      .arg(Hexa::Colors::Background, Hexa::Colors::Warning));
    setupUi();
    setFixedWidth(kDialogWidthPx);
}

void ConfirmDialog::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 14);
    layout->setSpacing(10);

    m_title = new QLabel(QStringLiteral("SAFETY WARNING"), this);
    m_title->setStyleSheet(QStringLiteral(
        "QLabel { border: none; background: transparent; color: %1; font-family: '%2';"
        " font-size: 12px; font-weight: bold; letter-spacing: 1px; }")
        .arg(Hexa::Colors::Warning, Hexa::Fonts::familyHeader()));
    m_title->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_title);
    layout->addWidget(HexaWidgets::createSeparatorH());

    m_message = new QLabel(this);
    m_message->setStyleSheet(Hexa::Styles::LabelBorderless.arg(
        Hexa::Colors::TextMain, Hexa::Fonts::familyUI(), "13"));
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    layout->addWidget(m_message);

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->setSpacing(8);

    // CANCEL is the safe path: neutral style, keyboard default and initial focus.
    QPushButton* btnCancel = HexaWidgets::createButtonStd("CANCEL", this, 0, 38);
    btnCancel->setMaximumWidth(QWIDGETSIZE_MAX);
    btnCancel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btnCancel->setDefault(true);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(btnCancel, 1);

    // The confirm action is danger-styled and carries an explicit action label (set via setContent):
    // the operator must read WHAT is being confirmed, not just "Yes".
    m_btnConfirm = new QPushButton(QStringLiteral("PROCEED"), this);
    m_btnConfirm->setFixedHeight(38);
    m_btnConfirm->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_btnConfirm->setCursor(Qt::PointingHandCursor);
    m_btnConfirm->setStyleSheet(Hexa::Styles::ButtonDangerNormal);
    connect(m_btnConfirm, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(m_btnConfirm, 1);

    layout->addLayout(buttons);
}

void ConfirmDialog::setContent(const QString& title, const QString& message,
                               const QString& confirmText) {
    m_title->setText(title);
    m_message->setText(message);
    m_btnConfirm->setText(confirmText);
}

bool ConfirmDialog::confirm(QWidget* parent, const QString& title, const QString& message,
                            const QString& confirmText) {
    ConfirmDialog dialog(parent);
    dialog.setContent(title, message, confirmText);
    return dialog.exec() == QDialog::Accepted;
}

void ConfirmDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (parentWidget() != nullptr) {
        const QRect hostRect = parentWidget()->window()->geometry();
        move(hostRect.center() - rect().center());
    }
}

} // namespace hexa
// --- END OF FILE: HexaStudio/status_bar/ConfirmDialog.cpp ---
