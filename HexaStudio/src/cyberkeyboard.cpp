#include "cyberkeyboard.h"
#include "styles/HexaTheme.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QFrame>

QString CyberKeyboard::getString(QWidget *parent, const QString &initialValue, const QString &placeholder, bool *ok)
{
    CyberKeyboard dlg(parent);
    dlg.m_display->setText(initialValue);
    dlg.m_display->setPlaceholderText(placeholder);

    dlg.resize(800, 450);
    if (parent) {
        dlg.move(parent->mapToGlobal(parent->rect().center()) - dlg.rect().center());
    }

    if (dlg.exec() == QDialog::Accepted) {
        if (ok) *ok = true;
        return dlg.m_display->text();
    }
    if (ok) *ok = false;
    return initialValue;
}

CyberKeyboard::CyberKeyboard(QWidget *parent) : QDialog(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    setupUi();
}

void CyberKeyboard::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QFrame *frame = new QFrame(this);

    QString frameStyle = QString(
                             "QFrame { "
                             "   background-color: %1; "
                             "   border-radius: 20px; "
                             "   border: 2px solid %2; "
                             "}"
                             ).arg(Hexa::Colors::Background, Hexa::Colors::Primary);
    frame->setStyleSheet(frameStyle);


    QVBoxLayout *frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(20, 20, 20, 20);
    frameLayout->setSpacing(15);

    m_display = new QLineEdit();
    m_display->setFont(QFont(Hexa::Fonts::familyMono(), 20));
    m_display->setStyleSheet(QString(
                                 "background-color: %1; color: %2; border: 1px solid %3; padding: 10px; border-radius: 4px;"
                                 ).arg(Hexa::Colors::Surface, Hexa::Colors::Accent, Hexa::Colors::Primary));
    frameLayout->addWidget(m_display);

    m_charGroup = new QButtonGroup(this);
    QVBoxLayout *keysLayout = new QVBoxLayout();
    keysLayout->setSpacing(5);

    QList<QStringList> rows = {
        {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="},
        {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]"},
        {"A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'", "\\"},
        {"Z", "X", "C", "V", "B", "N", "M", ",", ".", "/"}
    };

    int btnId = 0;
    for (const QStringList &rowKeys : rows) {
        QHBoxLayout *rowLayout = new QHBoxLayout();
        rowLayout->setSpacing(5);
        for (const QString &key : rowKeys) {
            QPushButton *btn = new QPushButton(key);
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            btn->setFixedHeight(50);
            btn->setStyleSheet(QString(
                                   "QPushButton { background-color: %1; border: 1px solid %2; color: %3; font-size: 16px; font-weight: bold; border-radius: 4px; }"
                                   "QPushButton:pressed { background-color: %4; color: black; }"
                                   ).arg(Hexa::Colors::Surface, Hexa::Colors::Primary, Hexa::Colors::TextMain, Hexa::Colors::Accent));

            m_charGroup->addButton(btn, btnId++);
            rowLayout->addWidget(btn);
        }
        keysLayout->addLayout(rowLayout);
    }
    connect(m_charGroup, &QButtonGroup::idClicked, this, &CyberKeyboard::onKeyClicked);
    frameLayout->addLayout(keysLayout);

    QHBoxLayout *controlLayout = new QHBoxLayout();

    QPushButton *btnShift = new QPushButton("SHIFT");
    btnShift->setFixedHeight(50);
    btnShift->setStyleSheet(QString("QPushButton { border: 1px solid %1; color: %1; border-radius: 4px; } QPushButton:hover { background-color: %2; }").arg(Hexa::Colors::Active, Hexa::Colors::StateHover));
    connect(btnShift, &QPushButton::clicked, this, &CyberKeyboard::onShift);

    QPushButton *btnSpace = new QPushButton("SPACE");
    btnSpace->setFixedHeight(50);
    btnSpace->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(btnSpace, &QPushButton::clicked, this, &CyberKeyboard::onSpace);

    QPushButton *btnBack = new QPushButton("BACKSPACE");
    btnBack->setFixedHeight(50);
    btnBack->setStyleSheet(QString("QPushButton { border: 1px solid %1; color: %1; border-radius: 4px; } QPushButton:hover { background-color: %1; color: black; }").arg(Hexa::Colors::Alert));
    connect(btnBack, &QPushButton::clicked, this, &CyberKeyboard::onBackspace);

    controlLayout->addWidget(btnShift, 1);
    controlLayout->addWidget(btnSpace, 4);
    controlLayout->addWidget(btnBack, 1);
    frameLayout->addLayout(controlLayout);

    QHBoxLayout *actionLayout = new QHBoxLayout();
    QPushButton *btnCancel = new QPushButton("CANCEL");
    btnCancel->setFixedHeight(50);
    btnCancel->setStyleSheet(Hexa::Styles::ButtonAlert);
    connect(btnCancel, &QPushButton::clicked, this, &CyberKeyboard::onCancel);

    QPushButton *btnOk = new QPushButton("ENTER");
    btnOk->setFixedHeight(50);
    btnOk->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(btnOk, &QPushButton::clicked, this, &CyberKeyboard::onEnter);

    actionLayout->addWidget(btnCancel);
    actionLayout->addWidget(btnOk);
    frameLayout->addLayout(actionLayout);

    layout->addWidget(frame);
    setLayout(layout);
}

void CyberKeyboard::onKeyClicked(int id)
{
    QAbstractButton *btn = m_charGroup->button(id);
    if (btn) {
        m_display->setText(m_display->text() + btn->text());
    }
}

void CyberKeyboard::onShift()
{
    m_isShifted = !m_isShifted;
    updateKeys();
}

void CyberKeyboard::updateKeys()
{
    QList<QAbstractButton*> buttons = m_charGroup->buttons();
    for (auto *btn : buttons) {
        QString text = btn->text();
        if (text.length() == 1) {
            btn->setText(m_isShifted ? text.toUpper() : text.toLower());
        }
    }
}

void CyberKeyboard::onSpace() { m_display->setText(m_display->text() + " "); }
void CyberKeyboard::onBackspace() {
    QString t = m_display->text();
    t.chop(1);
    m_display->setText(t);
}
void CyberKeyboard::onEnter() { accept(); }
void CyberKeyboard::onCancel() { reject(); }
