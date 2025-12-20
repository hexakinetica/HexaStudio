#include "mainwindow.h"
#include "cyberkeyboard.h"
#include "styles/HexaTheme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QDebug>
#include <QtMath>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("HEXA KINETIC CONTROL");
    resize(1280, 720);
    setMinimumSize(800, 600);
    setStyleSheet(QString("background-color: %1;").arg(Hexa::Colors::Background));

    m_robotService = RobotService::instance();

    setupUi();
    setupConnections();

    onRobotStateChanged(m_robotService->getStatus());

    if (!m_robotService->getCachedTrajectory().isEmpty()) {
        m_view3D->updateTrajectoryPath(m_robotService->getCachedTrajectory());
    }

    const auto& cfg = m_robotService->getConfig();
    if (!cfg.tools.isEmpty()) {
        m_panelRight->onConfigReceived(cfg);
    }
}

MainWindow::~MainWindow() {}

void MainWindow::setupConnections()
{
    connect(m_robotService, &RobotService::stateChanged, this, &MainWindow::onRobotStateChanged);
    connect(m_robotService, &RobotService::settingsReceived, this, &MainWindow::onSettingsReceived);

    connect(m_robotService, &RobotService::trajectoryReceived, m_view3D, &RDT::PanelView3D::updateTrajectoryPath);

    connect(m_panelTop, &PanelTop::modeChanged, m_robotService, &RobotService::setMode);
    connect(m_panelTop, &PanelTop::speedChanged, m_robotService, &RobotService::setSpeedOverride);
    connect(m_panelTop, &PanelTop::settingsRequested, m_robotService, &RobotService::requestSettings);

    connect(m_panelTop, &PanelTop::eStopRequested, this, [this](){
        bool isCurrentlyEStop = m_robotService->getStatus().top.isEStop;
        m_robotService->setEStop(!isCurrentlyEStop);
    });

    connect(m_panelRight, &PanelRight::jogRequested, m_robotService, &RobotService::jogJointIncremental);
    connect(m_panelRight, &PanelRight::homeRequested, m_robotService, &RobotService::moveHome);

    connect(m_panelRight, &PanelRight::monitorContextChanged, this, [this](QString t, QString b){
        m_robotService->setMonitorContext(0, 0);
    });

    connect(m_robotService, &RobotService::settingsReceived, m_panelRight, &PanelRight::onConfigReceived);

    connect(m_panelLeft, &PanelLeft::playRequested, m_robotService, &RobotService::startProgram);
    connect(m_panelLeft, &PanelLeft::stopRequested, m_robotService, &RobotService::stopProgram);
    connect(m_panelLeft, &PanelLeft::pauseRequested, m_robotService, &RobotService::pauseProgram);

    // NEW: Sync program on edits
    connect(m_panelLeft, &PanelLeft::programChanged, m_robotService, &RobotService::uploadProgram);

    connect(m_panelLeft, &PanelLeft::viewTrajectoryToggled, m_view3D, &RDT::PanelView3D::setTrajectoryVisible);
}

void MainWindow::onRobotStateChanged(const HmiRobotStatus &status)
{
    m_panelTop->updateState(status.top, status.motion.isMoving);
    m_panelRight->updateState(status.motion);
    m_panelLeft->updateState(status.prog, status.motion);
    m_view3D->updateState(status.motion);
}

void MainWindow::onSettingsReceived(const HmiSystemConfig &config)
{
    Q_UNUSED(config);
    if (m_settingsOverlay) {
        showSettingsOverlay();
    }
}

void MainWindow::showSettingsOverlay() {
    if (m_view3D) m_view3D->setVisible(false);
    if (m_settingsOverlay) {
        m_settingsOverlay->resize(size());
        m_settingsOverlay->raise();
        m_settingsOverlay->show();
    }
}

void MainWindow::hideSettingsOverlay() {
    if (m_settingsOverlay) m_settingsOverlay->hide();
    if (m_view3D) {
        m_view3D->setVisible(true);
        QResizeEvent re(m_view3D->size(), m_view3D->size());
        QApplication::sendEvent(m_view3D, &re);
    }
}

void MainWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(5);
    mainLayout->setContentsMargins(5, 5, 5, 5);

    m_topFrame = HexaWidgets::createMainPanel();
    m_topFrame->setFixedHeight(80);
    QVBoxLayout *topLayout = new QVBoxLayout(m_topFrame);
    topLayout->setContentsMargins(0, 0, 0, 0);
    m_panelTop = new PanelTop(m_topFrame);
    topLayout->addWidget(m_panelTop);

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->setHandleWidth(4);
    m_mainSplitter->setStyleSheet(Hexa::Styles::Splitter);
    m_mainSplitter->setChildrenCollapsible(false);

    m_leftFrame = HexaWidgets::createMainPanel();
    m_leftFrame->setMinimumWidth(200);
    QVBoxLayout *leftLayout = new QVBoxLayout(m_leftFrame);
    leftLayout->setContentsMargins(0,0,0,0);
    m_panelLeft = new PanelLeft(m_leftFrame);
    leftLayout->addWidget(m_panelLeft);
    m_mainSplitter->addWidget(m_leftFrame);

    QFrame *centerFrame = HexaWidgets::createMainPanel();
    centerFrame->setMinimumWidth(200);
    QVBoxLayout *centerLayout = new QVBoxLayout(centerFrame);
    centerLayout->setContentsMargins(0,0,0,0);
    centerLayout->setSpacing(0);
    m_view3D = new RDT::PanelView3D(centerFrame);
    centerLayout->addWidget(m_view3D, 1);

    m_viewToolbar = new QFrame(centerFrame);
    m_viewToolbar->setFixedHeight(40);
    m_viewToolbar->setStyleSheet("background-color: " + Hexa::Colors::Surface + "; border-top: 1px solid " + Hexa::Colors::Border + ";");
    QHBoxLayout *toolbarLayout = new QHBoxLayout(m_viewToolbar);
    toolbarLayout->setContentsMargins(10, 0, 10, 0);
    toolbarLayout->setSpacing(10);
    toolbarLayout->setAlignment(Qt::AlignLeft);

    QPushButton *btnGhost = HexaWidgets::createButtonSm("GHOST", m_viewToolbar, 80, 28);
    btnGhost->setCheckable(true);
    btnGhost->setChecked(true);
    connect(btnGhost, &QPushButton::toggled, m_view3D, &RDT::PanelView3D::setGhostVisible);
    toolbarLayout->addWidget(btnGhost);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(HexaWidgets::createLabelText("VIEW:"));
    QPushButton *btnIso = HexaWidgets::createButtonSm("ISO", m_viewToolbar, 60, 28);
    QPushButton *btnTop = HexaWidgets::createButtonSm("TOP", m_viewToolbar, 60, 28);
    QPushButton *btnFront = HexaWidgets::createButtonSm("FRONT", m_viewToolbar, 60, 28);
    connect(btnIso, &QPushButton::clicked, m_view3D, &RDT::PanelView3D::setViewIso);
    connect(btnTop, &QPushButton::clicked, m_view3D, &RDT::PanelView3D::setViewTop);
    connect(btnFront, &QPushButton::clicked, m_view3D, &RDT::PanelView3D::setViewFront);
    toolbarLayout->addWidget(btnIso);
    toolbarLayout->addWidget(btnTop);
    toolbarLayout->addWidget(btnFront);
    centerLayout->addWidget(m_viewToolbar, 0);

    connect(m_panelLeft, &PanelLeft::viewToolbarToggled, m_viewToolbar, &QWidget::setVisible);
    m_mainSplitter->addWidget(centerFrame);

    m_rightFrame = HexaWidgets::createMainPanel();
    m_rightFrame->setMinimumWidth(200);
    QVBoxLayout *rightLayout = new QVBoxLayout(m_rightFrame);
    rightLayout->setContentsMargins(0,0,0,0);
    m_panelRight = new PanelRight(m_rightFrame);
    rightLayout->addWidget(m_panelRight);
    m_mainSplitter->addWidget(m_rightFrame);

    m_mainSplitter->setSizes(QList<int>({300, 680, 300}));
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setStretchFactor(2, 0);

    mainLayout->addWidget(m_topFrame);
    mainLayout->addWidget(m_mainSplitter, 1);

    m_btnFloatingKbd = new QPushButton(this);
    m_btnFloatingKbd->setText("KEYBOARD");
    m_btnFloatingKbd->setFixedSize(100, 40);
    m_btnFloatingKbd->setCursor(Qt::PointingHandCursor);
    m_btnFloatingKbd->setStyleSheet(Hexa::Styles::ButtonRounded + Hexa::Styles::ButtonInteract);
    connect(m_btnFloatingKbd, &QPushButton::clicked, [this](){ CyberKeyboard::getString(this, "", "TYPE HERE..."); });

    m_settingsOverlay = new SettingsOverlay(this);
    connect(m_settingsOverlay, &SettingsOverlay::closeRequested, this, &MainWindow::hideSettingsOverlay);
    connect(m_settingsOverlay, &SettingsOverlay::applyRequested, m_robotService, &RobotService::applySettings);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_btnFloatingKbd) {
        int x = width() - m_btnFloatingKbd->width() - 20;
        int y = height() - m_btnFloatingKbd->height() - 20;
        m_btnFloatingKbd->move(x, y);
        m_btnFloatingKbd->raise();
    }
    if (m_settingsOverlay) m_settingsOverlay->resize(size());
}
