cmake_minimum_required(VERSION 3.16)
project(HexaStudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets 3DCore 3DRender 3DExtras 3DInput Network)

# Подключаем локальные пути и общую папку Shared (на уровень выше)
include_directories(
    src/styles
    src/panels/top
    src/panels/right
    src/panels/left
    src/panels/center
    src/backend
    src
    ../Shared
)

add_executable(HexaStudio
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    
    # Styles
    src/styles/HexaTheme.h
    src/styles/HexaWidgets.h
    src/styles/HexaWidgets.cpp

    # Panels
    src/panels/top/PanelTop.h
    src/panels/top/PanelTop.cpp
    src/panels/right/PanelRight.h
    src/panels/right/PanelRight.cpp
    src/panels/left/PanelLeft.h
    src/panels/left/PanelLeft.cpp
    src/panels/left/ProgramData.h
    src/panels/left/ProgramModel.h
    src/panels/left/ProgramModel.cpp
    src/panels/left/ProgramDelegate.h
    src/panels/left/ProgramDelegate.cpp
    src/panels/center/PanelView3D.h
    src/panels/center/PanelView3D.cpp

    # Backend
    src/backend/BackendTypes.h
    src/backend/RobotService.h
    src/backend/RobotService.cpp

    # Helpers
    src/cyberkeyboard.cpp src/cyberkeyboard.h
    src/settingsoverlay.cpp src/settingsoverlay.h

    resources.qrc
    
    # Doxygen Docs
    docs/architecture/ComponentSpecs.dox
    docs/architecture/ProtocolSpecification.dox
    docs/architecture/SystemOverview.dox
    docs/architecture/DataSynchronization.dox
)

target_link_libraries(HexaStudio PRIVATE
    Qt6::Widgets
    Qt6::3DCore
    Qt6::3DRender
    Qt6::3DExtras
    Qt6::3DInput
    Qt6::Network
)
