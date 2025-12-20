#include "mainwindow.h"
#include <QApplication>
#include <QFontDatabase>
#include <QDebug>
#include "backend/BackendTypes.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 1. Register Meta Types for Signal/Slots
    qRegisterMetaType<HmiRobotStatus>("HmiRobotStatus");
    qRegisterMetaType<HmiSystemConfig>("HmiSystemConfig");

    // 2. Register types used inside QVariant (Critical for ProgramModel)
    qRegisterMetaType<QVector<double>>("QVector<double>");

    // 3. Load Fonts
    if (QFontDatabase::addApplicationFont(":/resources/Michroma-Regular.ttf") == -1)
        qWarning() << "Failed to load Michroma font";
    if (QFontDatabase::addApplicationFont(":/resources/IBMPlexMono-Regular.ttf") == -1)
        qWarning() << "Failed to load IBM Plex Mono font";

    qDebug() << "Application Starting...";

    MainWindow w;
    w.show();

    return a.exec();
}
