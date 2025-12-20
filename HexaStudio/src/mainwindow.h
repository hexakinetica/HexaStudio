#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QFrame>
#include <QSplitter>

#include "panels/top/PanelTop.h"
#include "panels/right/PanelRight.h"
#include "panels/left/PanelLeft.h"
#include "panels/center/PanelView3D.h"
#include "styles/HexaWidgets.h"
#include "settingsoverlay.h"
#include "backend/RobotService.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void showSettingsOverlay();
    void hideSettingsOverlay();

private slots:
    // New unified state handler
    void onRobotStateChanged(const HmiRobotStatus &status);

    // Handler for settings (Slow Path)
    void onSettingsReceived(const HmiSystemConfig &config);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void setupConnections();

    // Panels
    QFrame     *m_topFrame;
    PanelTop   *m_panelTop;

    QFrame     *m_rightFrame;
    PanelRight *m_panelRight;

    QFrame     *m_leftFrame;
    PanelLeft  *m_panelLeft;

    // Center Panel
    RDT::PanelView3D *m_view3D;
    QFrame *m_viewToolbar;

    QSplitter   *m_mainSplitter;

    QPushButton *m_btnFloatingKbd;
    SettingsOverlay *m_settingsOverlay;

    RobotService *m_robotService;
};

#endif // MAINWINDOW_H
