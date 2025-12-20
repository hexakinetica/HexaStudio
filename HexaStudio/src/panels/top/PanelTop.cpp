#include "PanelTop.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QDebug>
#include <QIntValidator>
#include <QLineEdit>

PanelTop::PanelTop(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(80);
    setAttribute(Qt::WA_TranslucentBackground);
    setupUi();
}

void PanelTop::updateState(const HmiTopStatus &status, bool isMoving)
{
    m_isRobotMoving = isMoving;

    // Update E-Stop Button Visuals
    HexaWidgets::updateButtonDangerState(m_btnEStop, status.isEStop);
    m_btnEStop->setText(status.isEStop ? "RESET" : "E-STOP");

    // Update Status Label (More descriptive)
    QString statusText = "SYSTEM READY";
    Hexa::State state = Hexa::State::Success;

    if (status.isEStop) {
        statusText = "EMERGENCY STOP";
        state = Hexa::State::Error;
        m_switchMode->setEnabled(false);
    } else if (!status.isConnected) {
        statusText = "DISCONNECTED";
        state = Hexa::State::Error;
        m_switchMode->setEnabled(false);
    } else if (isMoving) {
        statusText = "ROBOT MOVING";
        state = Hexa::State::Active;
        m_switchMode->setEnabled(false);
    } else {
        if (!status.activeErrors.isEmpty()) {
            statusText = status.activeErrors;
            state = Hexa::State::Warning;
        }
        m_switchMode->setEnabled(true);
    }

    m_lblStatus->setText(statusText);
    HexaWidgets::updateStatusLabel(m_lblStatus, state);

    bool realModeFromStatus = (status.mode == "AUTO" || status.mode == "REAL");

    if (m_switchMode->isChecked() != realModeFromStatus) {
        bool wasBlocked = m_switchMode->blockSignals(true);
        m_switchMode->setChecked(realModeFromStatus);
        m_switchMode->blockSignals(wasBlocked);
    }

    m_lblCpu->setText(QString::number(status.cpuLoad, 'f', 1) + "%");
    m_lblTemp->setText(QString::number(status.controllerTemp, 'f', 1) + "°C");
    m_lblPing->setText(QString::number(status.networkLatency, 'f', 1) + "ms");
}

void PanelTop::onModeToggled(bool checked)
{
    if (m_isRobotMoving) {
        bool wasBlocked = m_switchMode->blockSignals(true);
        m_switchMode->setChecked(!checked);
        m_switchMode->blockSignals(wasBlocked);
        return;
    }

    if (checked) {
        m_switchMode->blockSignals(true);
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Safety Warning",
                                                                  "Switching to REAL ROBOT mode.\nEnsure workspace is clear.\n\nProceed?",
                                                                  QMessageBox::Yes|QMessageBox::No);
        m_switchMode->blockSignals(false);

        if (reply == QMessageBox::No) {
            bool wasBlocked = m_switchMode->blockSignals(true);
            m_switchMode->setChecked(false);
            m_switchMode->blockSignals(wasBlocked);
            return;
        }
    }

    emit modeChanged(checked);
}

void PanelTop::setupUi()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(Hexa::Dim::PanelPadding, 5, Hexa::Dim::PanelPadding, 5);
    mainLayout->setSpacing(20);
    mainLayout->addWidget(createLeftSection());
    mainLayout->addWidget(HexaWidgets::createSectionSeparator());
    mainLayout->addWidget(createCenterSection(), 1);
    mainLayout->addWidget(HexaWidgets::createSectionSeparator());
    mainLayout->addWidget(createRightSection());
}

QWidget* PanelTop::createLeftSection()
{
    QWidget *w = new QWidget(this);
    w->setAttribute(Qt::WA_TranslucentBackground);
    w->setFixedWidth(400);
    QHBoxLayout *l = new QHBoxLayout(w);
    l->setContentsMargins(0,0,0,0);
    l->setSpacing(15);
    m_lblBrand = HexaWidgets::createLabelHeader("HEXAKINETICA");
    l->addWidget(m_lblBrand);
    l->addWidget(HexaWidgets::createSeparatorV());
    m_lblStatus = HexaWidgets::createLabelStatus("SYSTEM READY");
    l->addWidget(m_lblStatus);
    return w;
}

QWidget* PanelTop::createCenterSection()
{
    m_centerStack = new QStackedWidget(this);
    m_centerStack->setAttribute(Qt::WA_TranslucentBackground);
    m_centerStack->setStyleSheet("background: transparent; border: none;");
    m_centerStack->addWidget(createCenterControls());
    m_centerStack->addWidget(createCenterMonitor());
    return m_centerStack;
}

QWidget* PanelTop::createCenterControls()
{
    QWidget *w = new QWidget();
    w->setStyleSheet("background: transparent; border: none;");
    QHBoxLayout *l = new QHBoxLayout(w);
    l->setContentsMargins(0,0,0,0);
    l->setSpacing(15);
    l->setAlignment(Qt::AlignCenter);

    l->addWidget(HexaWidgets::createLabelText("SIM"));
    m_switchMode = new HexaToggle(this);
    connect(m_switchMode, &QAbstractButton::toggled, this, &PanelTop::onModeToggled);
    l->addWidget(m_switchMode);
    l->addWidget(HexaWidgets::createLabelText("REAL"));

    l->addWidget(HexaWidgets::createSeparatorV());

    l->addWidget(HexaWidgets::createLabelText("SPEED:"));
    m_comboSpeed = HexaWidgets::createComboBox(this);
    m_comboSpeed->setFixedWidth(120);
    m_comboSpeed->setEditable(true);
    m_comboSpeed->addItems({"10%", "25%", "50%", "75%"});
    m_comboSpeed->setCurrentText("50%");
    if (m_comboSpeed->lineEdit()) {
        m_comboSpeed->lineEdit()->setValidator(new QIntValidator(1, 100, this));
        m_comboSpeed->lineEdit()->setAlignment(Qt::AlignCenter);
    }
    connect(m_comboSpeed, &QComboBox::currentTextChanged, this, &PanelTop::onSpeedChanged);
    l->addWidget(m_comboSpeed);

    l->addWidget(HexaWidgets::createSeparatorV());

    m_btnSettings = HexaWidgets::createButtonSm("SETTINGS", this, 100, 35);
    connect(m_btnSettings, &QPushButton::clicked, this, &PanelTop::settingsRequested);
    l->addWidget(m_btnSettings);

    m_btnMonitor = HexaWidgets::createButtonSm("MONITOR", this, 100, 35);
    connect(m_btnMonitor, &QPushButton::clicked, this, &PanelTop::onToggleMonitor);
    l->addWidget(m_btnMonitor);

    return w;
}

QWidget* PanelTop::createCenterMonitor()
{
    QWidget *w = new QWidget();
    w->setAttribute(Qt::WA_TranslucentBackground);
    QHBoxLayout *l = new QHBoxLayout(w);
    l->setContentsMargins(0,0,0,0);
    l->setSpacing(20);
    l->setAlignment(Qt::AlignLeft);

    auto addStat = [&](QString label, QLabel*& valLbl) {
        QVBoxLayout *vb = new QVBoxLayout();
        vb->setSpacing(2);
        vb->addWidget(HexaWidgets::createLabelText(label));
        valLbl = new QLabel("---");
        valLbl->setFont(Hexa::Fonts::getMono(14));
        valLbl->setStyleSheet("color: " + Hexa::Colors::Accent + "; font-weight: bold;");
        vb->addWidget(valLbl);
        l->addLayout(vb);
    };

    addStat("CPU LOAD", m_lblCpu);
    l->addWidget(HexaWidgets::createSeparatorV());
    addStat("TEMP", m_lblTemp);
    l->addWidget(HexaWidgets::createSeparatorV());
    addStat("NETWORK", m_lblPing);

    l->addStretch();
    m_btnCloseMonitor = HexaWidgets::createButtonStd("CLOSE MONITOR", this, 140, 35);
    connect(m_btnCloseMonitor, &QPushButton::clicked, this, &PanelTop::onToggleMonitor);
    l->addWidget(m_btnCloseMonitor);
    return w;
}

QWidget* PanelTop::createRightSection()
{
    QWidget *w = new QWidget(this);
    w->setAttribute(Qt::WA_TranslucentBackground);
    QHBoxLayout *l = new QHBoxLayout(w);
    l->setContentsMargins(0,0,0,0);
    l->setAlignment(Qt::AlignRight);

    m_btnEStop = HexaWidgets::createButtonDanger("E-STOP", this, 120, 40);
    connect(m_btnEStop, &QPushButton::clicked, this, &PanelTop::onEStopClicked);
    l->addWidget(m_btnEStop);

    return w;
}

void PanelTop::onToggleMonitor() {
    int current = m_centerStack->currentIndex();
    m_centerStack->setCurrentIndex(current == 0 ? 1 : 0);
}

void PanelTop::onSpeedChanged(const QString &text) {
    QString clean = text; clean.remove('%');
    bool ok; int val = clean.toInt(&ok);
    if (ok) emit speedChanged(val);
}

void PanelTop::onEStopClicked() { emit eStopRequested(); }
