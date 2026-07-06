# ==============================================================================
# overlays — standalone, independently-runnable overlays module (settings +
# diagnostics; the HAL runtime is the separate hal_control module).
# ------------------------------------------------------------------------------
# Additive module: it does NOT modify the HexaStudio executable or HexaMotion.
# The module-owned SettingsPanel/DiagnosticsPanel (RobotService-free
# re-implementations; the shipping overlays stay untouched) run against an offline
# FakeOverlaysController, so the overlays build and are checked in isolation.
# Integration into the new product happens later.
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(overlays LANGUAGES CXX)

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
# The shared theme/widgets come from the hexa_ui_kit module (module-owned), not a reach-through into
# src/styles. OverlayWidgets is this module's own building block. NO HexaStudio, NO network.
add_executable(overlays_bench
    bench/overlays_bench_main.cpp
    bench/FakeOverlaysController.h
    bench/FakeOverlaysController.cpp
    SettingsPanel.h
    SettingsPanel.cpp
    DiagnosticsLog.h
    DiagnosticsLog.cpp
    DiagnosticsPanel.h
    DiagnosticsPanel.cpp
    OverlayWidgets.h
    OverlayWidgets.cpp
    ../resources.qrc
)
target_include_directories(overlays_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/bench
)
target_compile_features(overlays_bench PUBLIC cxx_std_20)
target_link_libraries(overlays_bench PRIVATE
    hexa_contracts    # HMI DTOs (BackendTypes.h) - module-owned, no src/backend reach-through
    hexa_ui_kit
    Qt6::Core
    Qt6::Widgets
)
