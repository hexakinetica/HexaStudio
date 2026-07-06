#include "cyberkeyboard.h"
#include "HexaTheme.h"   // module-owned layout: theme header sits alongside (was styles/HexaTheme.h)
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QFrame>

QString CyberKeyboard::getString(QWidget *parent, const QString &initialValue, const QString &placeholder, bool *ok) {
    CyberKeyboard dlg(parent);
    dlg.m_display->setText(initialValue);
    dlg.m_display->setPlaceholderText(placeholder);
    dlg.m_title->setText(placeholder.isEmpty() ? QStringLiteral("KEYBOARD") : placeholder.toUpper());
    // Cursor-aware editing (boss request): start with the cursor at the end; keyboard focus stays
    // on the display so a physical keyboard types straight into it alongside the on-screen keys.
    dlg.m_display->setCursorPosition(static_cast<int>(initialValue.length()));
    dlg.resize(800, 470);
    // Centre on the top-level WINDOW, not on the (often narrow, side-docked) widget that spawned
    // the prompt - centring on the caller opened the keyboard visually "nowhere" (boss review).
    QWidget *anchor = parent ? parent->window() : nullptr;
    if (anchor) {
        dlg.move(anchor->frameGeometry().center() - dlg.rect().center());
    }
    dlg.m_display->setFocus();
    if (dlg.exec() == QDialog::Accepted) {
        if (ok) *ok = true;
        return dlg.m_display->text();
    }
    if (ok) *ok = false;
    return initialValue;
}

CyberKeyboard::CyberKeyboard(QWidget *parent) : QDialog(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    setupUi();
}

void CyberKeyboard::setupUi() {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Card look shared with the shell columns: Surface fill, hairline border, soft radius - the
    // previous bespoke teal-bordered frame read as a foreign element (boss review).
    QFrame *frame = new QFrame(this);
    frame->setStyleSheet(QString(
        "QFrame { background-color: %1; border: 1px solid %2; border-radius: 12px; }")
        .arg(Hexa::Colors::Surface, Hexa::Colors::Hairline));
    QVBoxLayout *frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(16, 12, 16, 16);
    frameLayout->setSpacing(10);

    // Title strip: names WHAT is being entered and doubles as an obvious drag surface (the whole
    // non-interactive frame area drags - see mousePressEvent).
    m_title = new QLabel(QStringLiteral("KEYBOARD"));
    m_title->setStyleSheet(QString(
        "color: %1; font-family: '%2'; font-size: 11px; letter-spacing: 1px;"
        " border: none; background: transparent;")
        .arg(Hexa::Colors::TextMuted, Hexa::Fonts::familyUI()));
    frameLayout->addWidget(m_title);

    // Display: an inset instrument cell (data plane), mono font, theme selection colours.
    m_display = new QLineEdit();
    m_display->setFont(QFont(Hexa::Fonts::familyMono(), 18));
    m_display->setFixedHeight(52);
    m_display->setStyleSheet(QString(
        "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; padding: 8px 12px;"
        " border-radius: %4px; selection-background-color: %5; selection-color: #14171D; }")
        .arg(Hexa::Colors::CellInset, Hexa::Colors::TextMain, Hexa::Colors::HairlineSoft,
             QString::number(Hexa::Dim::RadiusCell), Hexa::Colors::Primary));
    frameLayout->addWidget(m_display);

    // One key style for the whole board: graphite control fill from the theme.
    const QString keyStyle = QString(
        "QPushButton { background: %1; border: 1px solid %2; color: %3; font-family: '%4';"
        " font-size: 15px; border-radius: %5px; }"
        "QPushButton:pressed { background: %6; }")
        .arg(Hexa::Colors::ButtonNormal, Hexa::Colors::Hairline, Hexa::Colors::TextMain,
             Hexa::Fonts::familyUI(), QString::number(Hexa::Dim::RadiusSmall),
             Hexa::Colors::StatePressed);

    m_charGroup = new QButtonGroup(this);
    QVBoxLayout *keysLayout = new QVBoxLayout();
    keysLayout->setSpacing(5);
    QList<QStringList> rows = {{"1","2","3","4","5","6","7","8","9","0","-","="},
                               {"Q","W","E","R","T","Y","U","I","O","P","[","]"},
                               {"A","S","D","F","G","H","J","K","L",";","'","\\"},
                               {"Z","X","C","V","B","N","M",",",".","/"}};
    int btnId = 0;
    for (const QStringList &rowKeys : rows) {
        QHBoxLayout *rowLayout = new QHBoxLayout();
        rowLayout->setSpacing(5);
        for (const QString &key : rowKeys) {
            QPushButton *btn = new QPushButton(key);
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            btn->setFixedHeight(46);
            btn->setStyleSheet(keyStyle);
            // Keyboard focus must STAY on the display: the text cursor keeps blinking at its
            // position and a physical keyboard keeps typing into the field.
            btn->setFocusPolicy(Qt::NoFocus);
            m_charGroup->addButton(btn, btnId++);
            rowLayout->addWidget(btn);
        }
        keysLayout->addLayout(rowLayout);
    }
    connect(m_charGroup, &QButtonGroup::idClicked, this, &CyberKeyboard::onKeyClicked);
    frameLayout->addLayout(keysLayout);

    // Control row: SHIFT | cursor left/right | SPACE | CLEAR | BACKSPACE.
    auto makeControl = [this, &keyStyle](const QString &text, void (CyberKeyboard::*slot)()) {
        QPushButton *btn = new QPushButton(text);
        btn->setFixedHeight(46);
        btn->setStyleSheet(keyStyle);
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, slot);
        return btn;
    };
    QHBoxLayout *controlLayout = new QHBoxLayout();
    controlLayout->setSpacing(5);
    controlLayout->addWidget(makeControl(QStringLiteral("SHIFT"), &CyberKeyboard::onShift), 2);
    controlLayout->addWidget(makeControl(QStringLiteral("◀"), &CyberKeyboard::onCursorLeft), 1);
    controlLayout->addWidget(makeControl(QStringLiteral("▶"), &CyberKeyboard::onCursorRight), 1);
    controlLayout->addWidget(makeControl(QStringLiteral("SPACE"), &CyberKeyboard::onSpace), 5);
    controlLayout->addWidget(makeControl(QStringLiteral("CLEAR"), &CyberKeyboard::onClear), 2);
    QPushButton *btnBack = new QPushButton(QStringLiteral("BACKSPACE"));
    btnBack->setFixedHeight(46);
    btnBack->setFocusPolicy(Qt::NoFocus);
    btnBack->setStyleSheet(QString(
        "QPushButton { background: transparent; border: 1px solid %1; color: %1;"
        " font-family: '%2'; font-size: 13px; border-radius: %3px; }"
        "QPushButton:pressed { background: %1; color: #14171D; }")
        .arg(Hexa::Colors::Alert, Hexa::Fonts::familyUI(),
             QString::number(Hexa::Dim::RadiusSmall)));
    connect(btnBack, &QPushButton::clicked, this, &CyberKeyboard::onBackspace);
    controlLayout->addWidget(btnBack, 2);
    frameLayout->addLayout(controlLayout);

    // Action row: CANCEL (danger) + ENTER (the single primary CTA of the dialog).
    QHBoxLayout *actionLayout = new QHBoxLayout();
    actionLayout->setSpacing(8);
    QPushButton *btnCancel = new QPushButton(QStringLiteral("CANCEL"));
    btnCancel->setFixedHeight(Hexa::Dim::BtnHeight);
    btnCancel->setFocusPolicy(Qt::NoFocus);
    btnCancel->setStyleSheet(Hexa::Styles::ButtonDangerNormal);
    connect(btnCancel, &QPushButton::clicked, this, &CyberKeyboard::onCancel);
    QPushButton *btnOk = new QPushButton(QStringLiteral("ENTER"));
    btnOk->setFixedHeight(Hexa::Dim::BtnHeight);
    btnOk->setFocusPolicy(Qt::NoFocus);
    btnOk->setStyleSheet(Hexa::Styles::ButtonPrimary);
    connect(btnOk, &QPushButton::clicked, this, &CyberKeyboard::onEnter);
    actionLayout->addWidget(btnCancel, 1);
    actionLayout->addWidget(btnOk, 2);
    frameLayout->addLayout(actionLayout);

    layout->addWidget(frame);
    setLayout(layout);

    // Physical Enter confirms, matching the on-screen CTA (Escape already rejects via QDialog).
    connect(m_display, &QLineEdit::returnPressed, this, &CyberKeyboard::onEnter);
}

// --- Frameless-window drag: press on any non-interactive frame area and move. -------------------

void CyberKeyboard::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    QDialog::mousePressEvent(event);
}

void CyberKeyboard::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
    }
    QDialog::mouseMoveEvent(event);
}

void CyberKeyboard::mouseReleaseEvent(QMouseEvent *event) {
    m_dragging = false;
    QDialog::mouseReleaseEvent(event);
}

// --- Key handling: everything edits AT the cursor (QLineEdit owns cursor + selection). ----------

void CyberKeyboard::onKeyClicked(int id) { if (auto *btn = m_charGroup->button(id)) m_display->insert(btn->text()); }
void CyberKeyboard::onShift() { m_isShifted = !m_isShifted; updateKeys(); }
void CyberKeyboard::updateKeys() { for (auto *btn : m_charGroup->buttons()) if (btn->text().length() == 1) btn->setText(m_isShifted ? btn->text().toUpper() : btn->text().toLower()); }
void CyberKeyboard::onSpace() { m_display->insert(QStringLiteral(" ")); }
void CyberKeyboard::onBackspace() { m_display->backspace(); }
void CyberKeyboard::onCursorLeft() { m_display->cursorBackward(false); }
void CyberKeyboard::onCursorRight() { m_display->cursorForward(false); }
void CyberKeyboard::onClear() { m_display->clear(); }
void CyberKeyboard::onEnter() { accept(); }
void CyberKeyboard::onCancel() { reject(); }
