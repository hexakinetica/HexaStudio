# ==============================================================================
# status_bar — standalone, independently-runnable top status bar module.
# ------------------------------------------------------------------------------
# Additive module: it does NOT modify the HexaStudio executable or HexaMotion.
# The module-owned StatusBarPanel (a RobotService-free re-implementation of the
# shipping PanelTop; PanelTop stays untouched) runs against an offline
# FakeTopController, so the status bar builds, runs and is checked in isolation
# from the controller/network. Integration into the new product happens later.
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(status_bar LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # bench embeds the shared app resources (theme fonts)

# Shared UI-kit module (theme + styled widgets); build it standalone when the tree has not provided it.
if(NOT TARGET hexa_contracts)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../hexa_contracts ${CMAKE_CURRENT_BINARY_DIR}/hexa_contracts_build)
endif()
if(NOT TARGET hexa_ui_kit)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../hexa_ui_kit ${CMAKE_CURRENT_BINARY_DIR}/hexa_ui_kit_build)
endif()

# --- Standalone GUI bench (runnable in isolation) ---------------------------------------------------
# Compiles the module-owned StatusBarPanel; the shared theme/widgets now come from the hexa_ui_kit
# module (module-owned), not a reach-through into src/styles. NO HexaStudio, NO network.
add_executable(status_bar_bench
    bench/status_bar_bench_main.cpp
    bench/FakeTopController.h
    bench/FakeTopController.cpp
    StatusBarPanel.h
    StatusBarPanel.cpp
    ConfirmDialog.h
    ConfirmDialog.cpp
    ../resources.qrc
)
target_include_directories(status_bar_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/bench
)
target_compile_features(status_bar_bench PUBLIC cxx_std_20)
target_link_libraries(status_bar_bench PRIVATE
    hexa_contracts    # HMI DTOs (BackendTypes.h) - module-owned, no src/backend reach-through
    hexa_ui_kit
    Qt6::Core
    Qt6::Widgets
)
