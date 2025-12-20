#include "PanelLeft.h"
#include "../../styles/HexaTheme.h"
#include "../../styles/HexaWidgets.h"
#include "../../cyberkeyboard.h"
#include "../../backend/RobotService.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QDebug>
#include <QDir>
#include <QStringListModel>
#include <QVariant>
#include <QTabWidget>
#include <QMessageBox>

PanelLeft::PanelLeft(QWidget *parent) : QWidget(parent)
{
    qRegisterMetaType<QVector<ProgramCommand>>("QVector<ProgramCommand>");
    setupUi();
    m_model->setProgramPointer(0);

    connect(RobotService::instance(), &RobotService::programLoaded, this, [this](const QVector<ProgramCommand>& prog){
        m_model->clear();
        for(const auto& cmd : prog) m_model->addCommand(cmd);
        m_displayedProgramName = "SyncedProgram";
        m_navGroup->button(0)->click();
    });

    const auto& cachedProg = RobotService::instance()->getCachedProgram();
    if (!cachedProg.isEmpty()) {
        m_model->clear();
        for(const auto& cmd : cachedProg) m_model->addCommand(cmd);
    }
}

void PanelLeft::updateState(const HmiProgramStatus &progStatus, const HmiMotionStatus &motionStatus)
{
    m_lastMotionStatus = motionStatus;
    m_model->setProgramPointer(progStatus.currentRowIndex);

    if (!progStatus.loadedProgramName.isEmpty()) {
        m_displayedProgramName = progStatus.loadedProgramName;
    }

    bool isSim = motionStatus.isSimulated;
    bool canTeach = !progStatus.isRunning && !motionStatus.isMoving && !isSim;

    if (m_btnTeach) {
        m_btnTeach->setEnabled(canTeach);
        m_btnTeach->setToolTip(isSim ? "Switch to REAL mode to Teach" : "Record Position");
    }
    if (m_btnTouchUp) {
        m_btnTouchUp->setEnabled(canTeach);
        m_btnTouchUp->setToolTip(isSim ? "Switch to REAL mode to Touch Up" : "Update Position");
    }

    if (progStatus.isRunning) {
        m_btnPlay->setText("RUNNING...");
        m_btnPlay->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
        m_btnPlay->setEnabled(false);
    } else {
        m_btnPlay->setText("RUN");
        m_btnPlay->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
        m_btnPlay->setEnabled(true);
    }
}

void PanelLeft::emitProgramChange() {
    emit programChanged(m_model->getProgram());
}

void PanelLeft::onTeachClicked() {
    if (!m_btnTeach->isEnabled()) return;
    QString motionType = m_comboMotionType->currentText();
    QString pointName = "Point " + QString::number(m_model->rowCount() + 1);
    ProgramCommand cmd(CommandType::Motion, motionType, pointName);
    cmd.params["Speed"] = 100;
    cmd.params["Zone"] = "FINE";
    cmd.params["Joints"] = QVariant::fromValue(m_lastMotionStatus.plannedJoints);
    cmd.params["TcpPose"] = QVariant::fromValue(m_lastMotionStatus.plannedTcp);
    addCommand(cmd);

    // Auto-sync
    emitProgramChange();
}

void PanelLeft::onTouchUpClicked() {
    if (!m_btnTouchUp->isEnabled()) return;
    QModelIndex idx = m_programView->currentIndex();
    if (!idx.isValid()) return;
    QVariantMap params = idx.data(ProgramModel::ParamsRole).toMap();
    params["Joints"] = QVariant::fromValue(m_lastMotionStatus.plannedJoints);
    params["TcpPose"] = QVariant::fromValue(m_lastMotionStatus.plannedTcp);
    m_model->setData(idx, params, ProgramModel::ParamsRole);

    // Auto-sync
    emitProgramChange();
}

void PanelLeft::setupUi() {
    QHBoxLayout *globalLayout = new QHBoxLayout(this);
    globalLayout->setContentsMargins(0, 0, 0, 0);
    globalLayout->setSpacing(0);
    QWidget *sidebar = new QWidget();
    sidebar->setFixedWidth(60);
    sidebar->setStyleSheet(QString("background-color: %1; border-right: 1px solid rgba(69, 162, 158, 0.2);").arg(Hexa::Colors::Surface));
    QVBoxLayout *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(5, 10, 5, 10);
    sideLayout->setSpacing(10);
    m_navGroup = new QButtonGroup(this);
    auto createNavBtn = [&](const QString &text, int id, QString color = Hexa::Colors::Accent) {
        QPushButton *btn = new QPushButton(text);
        btn->setCheckable(true); btn->setFixedSize(50, 50); btn->setCursor(Qt::PointingHandCursor);
        QString style = QString("QPushButton { background: transparent; border: none; color: %1; font-size: 9px; }QPushButton:checked { background-color: rgba(102, 252, 241, 0.1); color: #FFFFFF; border-left: 2px solid %2; }QPushButton:hover:!checked { background-color: rgba(255,255,255,0.05); }").arg(color, Hexa::Colors::Primary);
        btn->setStyleSheet(style);
        m_navGroup->addButton(btn, id);
        sideLayout->addWidget(btn);
        return btn;
    };
    QPushButton *btnRun = createNavBtn("RUN", 0, Hexa::Colors::Primary);
    createNavBtn("TEACH", 1, Hexa::Colors::Active);
    createNavBtn("CMD", 2);
    createNavBtn("EDIT", 3);
    sideLayout->addSpacing(10);
    QFrame *sep = new QFrame(); sep->setFrameShape(QFrame::HLine); sep->setStyleSheet(Hexa::Styles::Separator);
    sideLayout->addWidget(sep); sideLayout->addSpacing(10);
    createNavBtn("FILE", 4, Hexa::Colors::TextMuted); sideLayout->addStretch(); createNavBtn("UI", 5, Hexa::Colors::TextMuted);
    btnRun->setChecked(true);
    connect(m_navGroup, &QButtonGroup::idClicked, this, &PanelLeft::onNavClicked);
    m_mainStack = new QStackedWidget();
    m_mainStack->addWidget(createProgramView());
    m_mainStack->addWidget(createFullFilePage());
    m_mainStack->addWidget(createGuiSettingsPage());
    globalLayout->addWidget(sidebar); globalLayout->addWidget(m_mainStack);
}

QWidget* PanelLeft::createProgramView() {
    QWidget *container = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(5, 5, 5, 5); layout->setSpacing(5);
    layout->addWidget(HexaWidgets::createLabelSectionTitle("ACTIVE PROGRAM"));
    m_programView = new QListView();
    m_model = new ProgramModel(this);
    m_delegate = new ProgramDelegate(this);
    m_programView->setModel(m_model);
    m_programView->setItemDelegate(m_delegate);
    m_programView->setDragDropMode(QAbstractItemView::InternalMove);
    m_programView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_programView->setSpacing(2);
    m_programView->setStyleSheet(Hexa::Styles::ListView);
    connect(m_programView->selectionModel(), &QItemSelectionModel::currentChanged, this, &PanelLeft::onSelectionChanged);
    layout->addWidget(m_programView, 1);
    m_toolsStack = new QStackedWidget();
    m_toolsStack->setFixedHeight(160);
    m_toolsStack->setStyleSheet("background: transparent; border: none;");
    m_toolsStack->addWidget(createRunTools());
    m_toolsStack->addWidget(createTeachTools());
    m_toolsStack->addWidget(createCmdTools());
    m_toolsStack->addWidget(createEditTools());
    layout->addWidget(m_toolsStack);
    return container;
}

QWidget* PanelLeft::createRunTools() {
    QWidget *p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QHBoxLayout *l = new QHBoxLayout(p); l->setSpacing(5); l->setContentsMargins(0,0,0,0);
    m_btnPlay = new QPushButton("RUN");
    m_btnPlay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_btnPlay->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(m_btnPlay, &QPushButton::clicked, this, [this](){ emit playRequested(m_model->getProgram()); });
    QPushButton *btnPause = new QPushButton("PAUSE");
    btnPause->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    btnPause->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(btnPause, &QPushButton::clicked, this, &PanelLeft::pauseRequested);
    QPushButton *btnStop = new QPushButton("STOP");
    btnStop->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    btnStop->setStyleSheet(Hexa::Styles::ButtonAlert);
    connect(btnStop, &QPushButton::clicked, this, &PanelLeft::stopRequested);
    l->addWidget(m_btnPlay); l->addWidget(btnPause); l->addWidget(btnStop);
    return p;
}

QWidget* PanelLeft::createTeachTools() {
    QWidget *p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QGridLayout *l = new QGridLayout(p); l->setSpacing(5); l->setContentsMargins(0, 5, 0, 0);
    m_comboMotionType = HexaWidgets::createComboBox(); m_comboMotionType->addItems({"PTP", "LIN", "CIRC"});
    m_comboMotionType->setFixedHeight(40); m_comboMotionType->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    l->addWidget(m_comboMotionType, 0, 0);
    QPushButton *btnSetPP = new QPushButton("SET PP"); btnSetPP->setFixedHeight(40);
    btnSetPP->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    btnSetPP->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(btnSetPP, &QPushButton::clicked, this, &PanelLeft::onSetPPClicked);
    l->addWidget(btnSetPP, 0, 1);
    m_btnTeach = new QPushButton("+ TEACH"); m_btnTeach->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_btnTeach->setMinimumHeight(40); m_btnTeach->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(m_btnTeach, &QPushButton::clicked, this, &PanelLeft::onTeachClicked);
    l->addWidget(m_btnTeach, 1, 0);
    m_btnTouchUp = new QPushButton("TOUCH UP"); m_btnTouchUp->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_btnTouchUp->setMinimumHeight(40); m_btnTouchUp->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(m_btnTouchUp, &QPushButton::clicked, this, &PanelLeft::onTouchUpClicked);
    l->addWidget(m_btnTouchUp, 1, 1);
    QHBoxLayout *row3 = new QHBoxLayout(); row3->setSpacing(5);
    QPushButton *btnCopy = new QPushButton("COPY"); btnCopy->setFixedHeight(40); btnCopy->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(btnCopy, &QPushButton::clicked, this, &PanelLeft::onCopyClicked);
    row3->addWidget(btnCopy);
    QPushButton *btnPaste = new QPushButton("PASTE"); btnPaste->setFixedHeight(40); btnPaste->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(btnPaste, &QPushButton::clicked, this, &PanelLeft::onPasteClicked);
    row3->addWidget(btnPaste);
    QPushButton *btnDel = new QPushButton("DEL"); btnDel->setFixedHeight(40); btnDel->setStyleSheet(Hexa::Styles::ButtonAlert);
    connect(btnDel, &QPushButton::clicked, this, &PanelLeft::onDeleteClicked);
    row3->addWidget(btnDel);
    l->addLayout(row3, 2, 0, 1, 2);
    return p;
}

QWidget* PanelLeft::createCmdTools() {
    QWidget *p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QGridLayout *grid = new QGridLayout(p); grid->setContentsMargins(0, 5, 0, 0); grid->setSpacing(5);
    QStringList cmds = {"WAIT TIME", "SET DO", "WAIT DI", "PULSE DO", "COMMENT", "MSG NOTIFY", "LOOP START", "LOOP END", "IF / ELSE", "CALL SUB", "HALT PROG", "GRIPPER"};
    int row = 0, col = 0;
    for(const auto &txt : cmds) {
        QPushButton *btn = new QPushButton(txt); btn->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
        btn->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
        connect(btn, &QPushButton::clicked, this, &PanelLeft::onInsertCmdClicked);
        grid->addWidget(btn, row, col);
        col++; if(col > 3) { col = 0; row++; }
    }
    return p;
}

QWidget* PanelLeft::createEditTools() {
    QWidget *p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QVBoxLayout *l = new QVBoxLayout(p); l->setContentsMargins(0,0,0,0);
    QLabel *lblType = new QLabel("EDIT PROPERTIES"); lblType->setAlignment(Qt::AlignCenter); lblType->setStyleSheet(Hexa::Styles::LabelHeaderSimple);
    l->addWidget(lblType);
    m_editStack = new QStackedWidget();
    m_editStack->addWidget(createEditMotion());
    m_editStack->addWidget(createEditLogic());
    m_editStack->addWidget(createEditIO());
    m_editStack->addWidget(createEditEmpty());
    l->addWidget(m_editStack);
    return p;
}

QWidget* PanelLeft::createEditMotion() {
    QWidget *p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QGridLayout *l = new QGridLayout(p); l->setContentsMargins(0,0,0,0);
    l->addWidget(HexaWidgets::createLabelText("VELOCITY:"), 0, 0);
    m_btnVelocityVal = new QPushButton("100%"); m_btnVelocityVal->setFixedHeight(40); m_btnVelocityVal->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(m_btnVelocityVal, &QPushButton::clicked, this, &PanelLeft::onEditVelocityClicked);
    l->addWidget(m_btnVelocityVal, 0, 1);
    l->addWidget(HexaWidgets::createLabelText("ZONE:"), 1, 0);
    QComboBox *cbZone = HexaWidgets::createComboBox(); cbZone->addItems({"FINE", "APPROX 1mm", "APPROX 10mm"}); cbZone->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    l->addWidget(cbZone, 1, 1);
    QPushButton *btnApply = new QPushButton("APPLY"); btnApply->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    l->addWidget(btnApply, 0, 2, 2, 1);
    return p;
}

QWidget* PanelLeft::createEditLogic() {
    QWidget *p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QGridLayout *l = new QGridLayout(p); l->setContentsMargins(0,0,0,0);
    l->addWidget(HexaWidgets::createLabelText("BLOCK:"), 0, 0);
    m_comboLogicType = HexaWidgets::createComboBox(); m_comboLogicType->addItems({"IF", "ELSE", "END_IF"});
    m_comboLogicType->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(m_comboLogicType, &QComboBox::currentTextChanged, this, &PanelLeft::onEditLogicTypeChanged);
    l->addWidget(m_comboLogicType, 0, 1);
    l->addWidget(HexaWidgets::createLabelText("COND:"), 1, 0);
    m_btnConditionVal = new QPushButton("TRUE"); m_btnConditionVal->setFixedHeight(40); m_btnConditionVal->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(m_btnConditionVal, &QPushButton::clicked, this, &PanelLeft::onEditConditionClicked);
    l->addWidget(m_btnConditionVal, 1, 1);
    return p;
}

QWidget* PanelLeft::createEditIO() {
    QWidget *p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QHBoxLayout *l = new QHBoxLayout(p);
    l->addWidget(HexaWidgets::createLabelText("SIGNAL:"));
    QComboBox *cbIO = HexaWidgets::createComboBox(); cbIO->addItems({"DO[1] - GRIPPER", "DO[2] - VACUUM"}); cbIO->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    l->addWidget(cbIO);
    return p;
}

QWidget* PanelLeft::createEditEmpty() {
    QWidget *p = new QWidget(); p->setStyleSheet(Hexa::Styles::PanelTransparent);
    QVBoxLayout *l = new QVBoxLayout(p); l->setAlignment(Qt::AlignCenter);
    QLabel *lbl = new QLabel("NO PROPERTIES"); lbl->setStyleSheet(Hexa::Styles::LabelHeaderSimple);
    l->addWidget(lbl);
    return p;
}

QWidget* PanelLeft::createFullFilePage() {
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(5, 5, 5, 5); layout->setSpacing(10);

    m_fileTabs = new QTabWidget();
    m_fileTabs->setStyleSheet("QTabWidget::pane { border: 1px solid " + Hexa::Colors::Border + "; background: transparent; } QTabBar::tab { background: " + Hexa::Colors::Surface + "; color: " + Hexa::Colors::TextMuted + "; padding: 8px; } QTabBar::tab:selected { background: " + Hexa::Colors::Primary + "; color: black; }");

    // Local Tab
    QWidget *tabLocal = new QWidget();
    QVBoxLayout *localLayout = new QVBoxLayout(tabLocal);
    m_fileListLocal = new QListView();
    QStringListModel *fileModel = new QStringListModel(this);
    QDir dir(QDir::currentPath()); QStringList filters; filters << "*.json";
    fileModel->setStringList(dir.entryList(filters, QDir::Files));
    m_fileListLocal->setModel(fileModel);
    m_fileListLocal->setStyleSheet(Hexa::Styles::ListView);
    localLayout->addWidget(m_fileListLocal);

    QHBoxLayout *localBtns = new QHBoxLayout();
    QPushButton *btnLoad = new QPushButton("LOAD LOCAL");
    btnLoad->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract); btnLoad->setFixedHeight(40);
    connect(btnLoad, &QPushButton::clicked, this, &PanelLeft::onLoadClicked);

    QPushButton *btnSave = new QPushButton("SAVE LOCAL");
    btnSave->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract); btnSave->setFixedHeight(40);
    connect(btnSave, &QPushButton::clicked, this, &PanelLeft::onSaveClicked);

    QPushButton *btnDelete = new QPushButton("DELETE");
    btnDelete->setStyleSheet(Hexa::Styles::ButtonAlert); btnDelete->setFixedHeight(40);
    connect(btnDelete, &QPushButton::clicked, this, &PanelLeft::onDeleteFileClicked);

    localBtns->addWidget(btnLoad); localBtns->addWidget(btnSave); localBtns->addWidget(btnDelete);
    localLayout->addLayout(localBtns);

    // Remote Tab
    QWidget *tabRemote = new QWidget();
    QVBoxLayout *remoteLayout = new QVBoxLayout(tabRemote);
    QLabel *lblRemote = new QLabel("CONTROLLER STORAGE");
    lblRemote->setStyleSheet(Hexa::Styles::LabelHeaderSimple);
    lblRemote->setAlignment(Qt::AlignCenter);

    QPushButton *btnSync = new QPushButton("LOAD FROM CONTROLLER");
    btnSync->setFixedHeight(60);
    btnSync->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(btnSync, &QPushButton::clicked, this, &PanelLeft::onSyncFromControllerClicked);

    remoteLayout->addStretch();
    remoteLayout->addWidget(lblRemote);
    remoteLayout->addWidget(btnSync);
    remoteLayout->addStretch();

    m_fileTabs->addTab(tabLocal, "LOCAL");
    m_fileTabs->addTab(tabRemote, "REMOTE");

    layout->addWidget(m_fileTabs);
    return page;
}

QWidget* PanelLeft::createGuiSettingsPage() {
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10); layout->setSpacing(15); layout->setAlignment(Qt::AlignTop);
    layout->addWidget(HexaWidgets::createLabelSectionTitle("INTERFACE SETTINGS"));
    layout->addWidget(HexaWidgets::createSeparatorH());

    QHBoxLayout *rowToolbar = new QHBoxLayout();
    rowToolbar->addWidget(HexaWidgets::createLabelText("VIEW CONTROL TOOLBAR")); rowToolbar->addStretch();
    HexaToggle *toggleToolbar = new HexaToggle(); toggleToolbar->setChecked(true);
    connect(toggleToolbar, &QAbstractButton::toggled, this, &PanelLeft::onUiSettingToggled);
    rowToolbar->addWidget(toggleToolbar);
    layout->addLayout(rowToolbar);

    QHBoxLayout *rowTraj = new QHBoxLayout();
    rowTraj->addWidget(HexaWidgets::createLabelText("SHOW TRAJECTORY")); rowTraj->addStretch();
    HexaToggle *toggleTraj = new HexaToggle(); toggleTraj->setChecked(true);
    connect(toggleTraj, &QAbstractButton::toggled, this, &PanelLeft::viewTrajectoryToggled);
    rowTraj->addWidget(toggleTraj);
    layout->addLayout(rowTraj);

    layout->addStretch();
    return page;
}

void PanelLeft::addCommand(const ProgramCommand &cmd) {
    int row = m_programView->currentIndex().row();
    if (row < 0) row = m_model->rowCount(); else row++;
    m_model->insertCommand(row, cmd);
    m_programView->setCurrentIndex(m_model->index(row));
}

void PanelLeft::onSelectionChanged(const QModelIndex &current, const QModelIndex &previous) {
    Q_UNUSED(previous);
    if (!current.isValid()) { m_editStack->setCurrentIndex(3); return; }
    CommandType type = current.data(ProgramModel::TypeRole).value<CommandType>();
    QVariantMap params = current.data(ProgramModel::ParamsRole).toMap();
    if (type == CommandType::Motion) {
        m_editStack->setCurrentIndex(0);
        if (params.contains("Speed")) m_btnVelocityVal->setText(QString::number(params["Speed"].toInt()) + "%");
    } else if (type == CommandType::Logic) {
        if (current.data(ProgramModel::CodeRole).toString() == "IF") {
            m_editStack->setCurrentIndex(1);
            QString subtype = params.value("Subtype", "IF").toString();
            m_comboLogicType->blockSignals(true); m_comboLogicType->setCurrentText(subtype); m_comboLogicType->blockSignals(false);
            if(params.contains("Condition")) m_btnConditionVal->setText(params["Condition"].toString());
            else m_btnConditionVal->setText("---");
            m_btnConditionVal->setEnabled(subtype == "IF");
        } else { m_editStack->setCurrentIndex(3); }
    } else if (type == CommandType::IO) { m_editStack->setCurrentIndex(2); }
    else { m_editStack->setCurrentIndex(3); }
}

void PanelLeft::onEditLogicTypeChanged(const QString &type) {
    QModelIndex idx = m_programView->currentIndex();
    if (idx.isValid()) {
        QVariantMap params = idx.data(ProgramModel::ParamsRole).toMap();
        params["Subtype"] = type; m_model->setData(idx, params, ProgramModel::ParamsRole);
        m_btnConditionVal->setEnabled(type == "IF");
        emitProgramChange();
    }
}

void PanelLeft::onInsertCmdClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    QString txt = btn->text().replace("\n", " ");
    CommandType type = CommandType::Logic; QString code = "CMD"; QVariantMap params;
    if (txt.contains("WAIT")) { code = "WAIT"; type = CommandType::Logic; params["Time"] = 1.0; }
    else if (txt.contains("IF")) { code = "IF"; type = CommandType::Logic; params["Subtype"] = "IF"; params["Condition"] = "TRUE"; }
    else if (txt.contains("DO") || txt.contains("DI")) { code = "IO"; type = CommandType::IO; }
    else if (txt.contains("COMMENT")) { code = "#"; type = CommandType::Comment; }
    ProgramCommand cmd(type, code, txt, params);
    addCommand(cmd);
    m_navGroup->button(0)->click();

    // Auto-sync
    emitProgramChange();
}

void PanelLeft::onDeleteClicked() {
    QModelIndex idx = m_programView->currentIndex();
    if (idx.isValid()) {
        m_model->removeCommand(idx.row());
        emit deleteRequested();

        // FIX: Auto-upload on delete to sync controller state immediately
        emitProgramChange();
    }
}
void PanelLeft::onCopyClicked() { QModelIndex idx = m_programView->currentIndex(); if (idx.isValid()) { m_copyBuffer = m_model->getCommand(idx.row()); m_hasCopy = true; } }
void PanelLeft::onPasteClicked() {
    if (m_hasCopy) {
        ProgramCommand newCmd = m_copyBuffer;
        newCmd.uuid = QUuid::createUuid();
        addCommand(newCmd);
        emitProgramChange();
    }
}
void PanelLeft::onSetPPClicked() { QModelIndex idx = m_programView->currentIndex(); if (idx.isValid()) m_model->setProgramPointer(idx.row()); }

void PanelLeft::onEditVelocityClicked() {
    bool ok; QString text = CyberKeyboard::getString(this, "100", "ENTER VELOCITY %", &ok);
    if (ok) {
        bool isNum; int val = text.toInt(&isNum);
        if (isNum) {
            QModelIndex idx = m_programView->currentIndex();
            if (idx.isValid()) {
                QVariantMap params = idx.data(ProgramModel::ParamsRole).toMap();
                params["Speed"] = val; m_model->setData(idx, params, ProgramModel::ParamsRole);
                m_btnVelocityVal->setText(QString::number(val) + "%");
                emitProgramChange();
            }
        }
    }
}

void PanelLeft::onEditConditionClicked() {
    bool ok; QString text = CyberKeyboard::getString(this, m_btnConditionVal->text(), "ENTER CONDITION", &ok);
    if (ok) {
        QModelIndex idx = m_programView->currentIndex();
        if (idx.isValid()) {
            QVariantMap params = idx.data(ProgramModel::ParamsRole).toMap();
            params["Condition"] = text; m_model->setData(idx, params, ProgramModel::ParamsRole);
            m_btnConditionVal->setText(text);
            emitProgramChange();
        }
    }
}

void PanelLeft::onSaveClicked() {
    bool ok; QString name = CyberKeyboard::getString(this, "program.json", "ENTER FILE NAME", &ok);
    if (ok && !name.isEmpty()) {
        if (m_model->saveToFile(name)) {
            QDir dir(QDir::currentPath()); QStringList filters; filters << "*.json";
            static_cast<QStringListModel*>(m_fileListLocal->model())->setStringList(dir.entryList(filters, QDir::Files));
        }
    }
}

void PanelLeft::onLoadClicked() {
    QModelIndex idx = m_fileListLocal->currentIndex();
    if (idx.isValid()) {
        QString name = idx.data().toString();
        if (m_model->loadFromFile(name)) {
            m_mainStack->setCurrentIndex(0);
            RobotService::instance()->uploadProgram(m_model->getProgram());
        }
    }
}

void PanelLeft::onDeleteFileClicked() {
    QModelIndex idx = m_fileListLocal->currentIndex();
    if (!idx.isValid()) return;

    QString fileName = idx.data().toString();
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Confirm Delete",
                                                              "Are you sure you want to delete " + fileName + "?", QMessageBox::Yes|QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        QFile::remove(fileName);
        QDir dir(QDir::currentPath()); QStringList filters; filters << "*.json";
        static_cast<QStringListModel*>(m_fileListLocal->model())->setStringList(dir.entryList(filters, QDir::Files));
    }
}

void PanelLeft::onSyncFromControllerClicked() {
    RobotService::instance()->connectToController("127.0.0.1", 30002);
}

void PanelLeft::onUiSettingToggled(bool checked) { emit viewToolbarToggled(checked); }
void PanelLeft::onNavClicked(int id) {
    if (id == 4) m_mainStack->setCurrentIndex(1);
    else if (id == 5) m_mainStack->setCurrentIndex(2);
    else { m_mainStack->setCurrentIndex(0); m_toolsStack->setCurrentIndex(id); if(id == 3) onSelectionChanged(m_programView->currentIndex(), QModelIndex()); }
}
