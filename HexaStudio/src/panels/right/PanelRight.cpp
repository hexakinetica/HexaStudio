#include "PanelRight.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDebug>
#include <QLineEdit>

PanelRight::PanelRight(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);

    // Define available steps
    m_stepOptions = {"0.1", "1.0", "10.0", "CONT"};
    // Default to "1.0" (Index 1)
    m_stepIndex = 1;

    setupUi();
    updateJogLabels(0);
    updateMonitorLabels(false);
}

void PanelRight::updateState(const HmiMotionStatus &status)
{
    if (m_currentMode == 0) {
        // JOINT Mode
        for(int i=0; i<6; ++i) {
            if(i < status.plannedJoints.size())
                m_activeDisplays[i]->setText(QString::number(status.plannedJoints[i], 'f', 2));
        }
    } else {
        // WORLD/TOOL Mode
        for(int i=0; i<6; ++i) {
            if(i < status.monitorPose.size())
                m_activeDisplays[i]->setText(QString::number(status.monitorPose[i], 'f', 2));
        }
    }

    // Monitor Section
    QString currentBase = m_comboMonBase->currentText();
    bool isJointMonitor = (currentBase == "JOINT");

    m_comboMonTool->setEnabled(!isJointMonitor);

    for(int i=0; i<6; ++i) {
        double val = 0.0;
        if (isJointMonitor) {
            if(i < status.actualJoints.size()) val = status.actualJoints[i];
        } else {
            if(i < status.monitorPose.size()) val = status.monitorPose[i];
        }
        m_passiveDisplays[i]->setText(QString::number(val, 'f', 2));
    }
}

void PanelRight::onConfigReceived(const HmiSystemConfig &config)
{
    bool blocked = m_comboJogTool->blockSignals(true);
    m_comboJogTool->clear();
    for(const auto &t : config.tools) m_comboJogTool->addItem(t.name, t.id);
    m_comboJogTool->blockSignals(blocked);

    blocked = m_comboJogBase->blockSignals(true);
    m_comboJogBase->clear();
    for(const auto &b : config.bases) m_comboJogBase->addItem(b.name, b.id);
    m_comboJogBase->blockSignals(blocked);

    blocked = m_comboMonTool->blockSignals(true);
    m_comboMonTool->clear();
    for(const auto &t : config.tools) m_comboMonTool->addItem(t.name, t.id);
    m_comboMonTool->blockSignals(blocked);

    blocked = m_comboMonBase->blockSignals(true);
    m_comboMonBase->clear();
    m_comboMonBase->addItem("JOINT");
    for(const auto &b : config.bases) m_comboMonBase->addItem(b.name, b.id);
    m_comboMonBase->blockSignals(blocked);
}

void PanelRight::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);
    mainLayout->addWidget(createActiveJogSection(), 0);
    mainLayout->addStretch(1);
    mainLayout->addWidget(HexaWidgets::createSeparatorH());
    mainLayout->addWidget(createPassiveMonitorSection(), 0);
}

QWidget* PanelRight::createActiveJogSection()
{
    QWidget *container = new QWidget();
    container->setAttribute(Qt::WA_TranslucentBackground);
    container->setStyleSheet("background: transparent; border: none;");

    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(8);

    QGridLayout *ctxGrid = new QGridLayout();
    ctxGrid->setHorizontalSpacing(5);
    ctxGrid->setVerticalSpacing(5);
    m_comboJogTool = HexaWidgets::createComboBox(this);
    m_comboJogTool->addItem("Loading...");
    connect(m_comboJogTool, &QComboBox::currentTextChanged, this, &PanelRight::onContextChangedSlot);
    m_comboJogBase = HexaWidgets::createComboBox(this);
    m_comboJogBase->addItem("Loading...");
    connect(m_comboJogBase, &QComboBox::currentTextChanged, this, &PanelRight::onContextChangedSlot);
    ctxGrid->addWidget(HexaWidgets::createLabelText("TOOL:"), 0, 0);
    ctxGrid->addWidget(m_comboJogTool, 0, 1);
    ctxGrid->addWidget(HexaWidgets::createLabelText("BASE:"), 1, 0);
    ctxGrid->addWidget(m_comboJogBase, 1, 1);
    layout->addLayout(ctxGrid);
    layout->addWidget(HexaWidgets::createSeparatorH());

    m_coordGroup = new QButtonGroup(this);
    QHBoxLayout *tabsLayout = new QHBoxLayout();
    tabsLayout->setSpacing(4);
    QStringList tabNames = {"JOINT", "WORLD", "TOOL"};
    for(int i=0; i<tabNames.size(); ++i) {
        QPushButton *btn = HexaWidgets::createButtonSm(tabNames[i], this, 0, 30);
        btn->setCheckable(true);
        if(i==0) btn->setChecked(true);
        m_coordGroup->addButton(btn, i);
        tabsLayout->addWidget(btn);
    }
    connect(m_coordGroup, &QButtonGroup::idClicked, this, &PanelRight::onTabChanged);
    layout->addLayout(tabsLayout);

    // --- STEP SELECTION (Button instead of ComboBox) ---
    QHBoxLayout *stepLayout = new QHBoxLayout();
    stepLayout->setAlignment(Qt::AlignCenter);
    QLabel *lblInc = HexaWidgets::createLabelSectionTitle("STEP:");

    // Create button with current value
    m_btnStep = HexaWidgets::createButtonSm(m_stepOptions[m_stepIndex], this, 80, 30);
    // Make it look slightly different to distinguish from action buttons
    m_btnStep->setStyleSheet(Hexa::Styles::ButtonBase + "QPushButton { border: 1px solid " + Hexa::Colors::Primary + "; color: white; } QPushButton:hover { background-color: " + Hexa::Colors::StateHover + "; }");

    connect(m_btnStep, &QPushButton::clicked, this, &PanelRight::onStepClicked);

    // Initialize logic
    applyStep(m_stepOptions[m_stepIndex]);

    stepLayout->addStretch();
    stepLayout->addWidget(lblInc);
    stepLayout->addWidget(m_btnStep);
    stepLayout->addStretch();
    layout->addLayout(stepLayout);

    QGridLayout *grid = new QGridLayout();
    grid->setVerticalSpacing(6);
    grid->setHorizontalSpacing(4);
    for(int i=0; i<6; ++i) {
        m_axisLabels.append(HexaWidgets::createLabelAxis("A1"));
        m_axisLabels.last()->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_activeDisplays.append(HexaWidgets::createLabelStatus("0.00"));
        m_activeDisplays.last()->setMinimumWidth(40);
        HexaWidgets::updateStatusLabel(m_activeDisplays.last(), Hexa::State::Active);

        QPushButton *btnMinus = HexaWidgets::createButtonIcon("<", this, 0, 40);
        btnMinus->setProperty("axis", i); btnMinus->setProperty("dir", -1.0);
        connect(btnMinus, &QPushButton::clicked, this, &PanelRight::onJogBtnPressed);
        m_jogButtons.append(btnMinus);

        QPushButton *btnPlus = HexaWidgets::createButtonIcon(">", this, 0, 40);
        btnPlus->setProperty("axis", i); btnPlus->setProperty("dir", 1.0);
        connect(btnPlus, &QPushButton::clicked, this, &PanelRight::onJogBtnPressed);
        m_jogButtons.append(btnPlus);

        grid->addWidget(btnMinus, i, 0);
        grid->addWidget(m_activeDisplays.last(), i, 1);
        grid->addWidget(btnPlus, i, 2);
        grid->addWidget(m_axisLabels.last(), i, 3);
    }
    layout->addLayout(grid);
    layout->addSpacing(5);
    layout->addWidget(HexaWidgets::createSeparatorH());
    m_btnHome = HexaWidgets::createButtonStd("MOVE HOME", this, 0, 40);
    m_btnHome->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_btnHome, &QPushButton::clicked, this, &PanelRight::onHomeClicked);
    layout->addWidget(m_btnHome);
    return container;
}

QWidget* PanelRight::createPassiveMonitorSection()
{
    QWidget *container = new QWidget();
    container->setAttribute(Qt::WA_TranslucentBackground);
    container->setStyleSheet("background: transparent; border: none;");
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(5);
    QPushButton *titleButton = HexaWidgets::createTitleButton("MONITOR SYSTEM", this);
    titleButton->setChecked(true);
    connect(titleButton, &QPushButton::toggled, this, &PanelRight::onToggleMonitor);
    layout->addWidget(titleButton);

    m_monitorContent = new QWidget(this);
    m_monitorContent->setAttribute(Qt::WA_TranslucentBackground);
    QVBoxLayout *contentLayout = new QVBoxLayout(m_monitorContent);
    contentLayout->setContentsMargins(0, 5, 0, 0);
    contentLayout->setSpacing(5);
    QHBoxLayout *ctxLayout = new QHBoxLayout();
    m_comboMonTool = HexaWidgets::createComboBox();
    m_comboMonTool->addItems({"..."});
    connect(m_comboMonTool, &QComboBox::currentTextChanged, this, &PanelRight::onMonitorContextChangedSlot);
    m_comboMonBase = HexaWidgets::createComboBox();
    m_comboMonBase->addItems({"JOINT", "..."});
    connect(m_comboMonBase, &QComboBox::currentTextChanged, this, &PanelRight::onMonitorContextChangedSlot);
    ctxLayout->addWidget(m_comboMonTool, 1);
    ctxLayout->addWidget(m_comboMonBase, 1);
    contentLayout->addLayout(ctxLayout);

    QGridLayout *grid = new QGridLayout();
    grid->setVerticalSpacing(4);
    grid->setHorizontalSpacing(10);
    for(int i=0; i<6; ++i) {
        m_passiveLabels.append(HexaWidgets::createLabelText("X:"));
        m_passiveLabels.last()->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_passiveDisplays.append(HexaWidgets::createLabelData("0.00"));
        int row = i / 2; int col = (i % 2) * 2;
        grid->addWidget(m_passiveLabels.last(), row, col);
        grid->addWidget(m_passiveDisplays.last(), row, col + 1);
    }
    contentLayout->addLayout(grid);
    layout->addWidget(m_monitorContent);
    return container;
}

void PanelRight::onToggleMonitor(bool visible) { if (m_monitorContent) m_monitorContent->setVisible(visible); }

void PanelRight::updateJogLabels(int mode) {
    QStringList active;
    if (mode == 0) { active = {"A1", "A2", "A3", "A4", "A5", "A6"}; }
    else { active = {"X", "Y", "Z", "Rx", "Ry", "Rz"}; }
    for(int i=0; i<6; ++i) { if(i < m_axisLabels.size()) m_axisLabels[i]->setText(active[i]); }
}

void PanelRight::updateMonitorLabels(bool isJoint) {
    QStringList passive;
    if (isJoint) { passive = {"A1:", "A2:", "A3:", "A4:", "A5:", "A6:"}; }
    else { passive = {"X:", "Y:", "Z:", "A:", "B:", "C:"}; }
    for(int i=0; i<6; ++i) { if(i < m_passiveLabels.size()) m_passiveLabels[i]->setText(passive[i]); }
}

void PanelRight::onTabChanged(int id) { m_currentMode = id; updateJogLabels(id); emit coordSystemChanged(id); }

// CLICK HANDLER FOR STEP BUTTON
void PanelRight::onStepClicked() {
    m_stepIndex++;
    if (m_stepIndex >= m_stepOptions.size()) m_stepIndex = 0;

    QString newVal = m_stepOptions[m_stepIndex];
    m_btnStep->setText(newVal);
    applyStep(newVal);
}

void PanelRight::applyStep(const QString &text) {
    QString t = text.toUpper();
    if (t == "CONT") {
        m_isContinuousMode = true;
        setJogButtonsEnabled(false);
        emit stepChanged(0.0);
    } else {
        m_isContinuousMode = false;
        setJogButtonsEnabled(true);
        bool ok;
        double val = t.toDouble(&ok);
        if(ok) {
            m_currentStep = val;
            emit stepChanged(val);
        }
    }
}

void PanelRight::onJogBtnPressed() {
    if (m_isContinuousMode) return;
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if(!btn) return;
    int axis = btn->property("axis").toInt();
    double dir = btn->property("dir").toDouble();
    emit jogRequested(axis, dir * m_currentStep);
}

void PanelRight::setJogButtonsEnabled(bool enabled) {
    for(QPushButton* btn : m_jogButtons) { btn->setEnabled(enabled); }
    // Visual feedback on button
    if (!enabled) m_btnStep->setStyleSheet(Hexa::Styles::ButtonBase + "QPushButton { border: 1px solid " + Hexa::Colors::Alert + "; color: " + Hexa::Colors::Alert + "; }");
    else m_btnStep->setStyleSheet(Hexa::Styles::ButtonBase + "QPushButton { border: 1px solid " + Hexa::Colors::Primary + "; color: white; } QPushButton:hover { background-color: " + Hexa::Colors::StateHover + "; }");
}

void PanelRight::onHomeClicked() { emit homeRequested(); }
void PanelRight::onContextChangedSlot() { emit jogContextChanged(m_comboJogTool->currentText(), m_comboJogBase->currentText()); }
void PanelRight::onMonitorContextChangedSlot() {
    QString base = m_comboMonBase->currentText();
    QString toolName = m_comboMonTool->currentText();

    if (base == "JOINT") {
        updateMonitorLabels(true);
        emit monitorContextChanged("-", base);
    } else {
        updateMonitorLabels(false);
        emit monitorContextChanged(toolName, base);
    }
}
