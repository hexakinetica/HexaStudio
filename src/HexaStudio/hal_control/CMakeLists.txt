# ==============================================================================
# hal_control — standalone, independently-runnable HAL runtime module.
# ------------------------------------------------------------------------------
# Additive module: it does NOT modify the HexaStudio executable or HexaMotion.
# The module-owned HalPanel (a RobotService-free re-implementation of the
# shipping HalOverlay; the shipping overlay stays untouched) runs against an
# offline FakeHalController, so the HAL commissioning panel builds, runs and is
# checked in isolation from the controller/network — valuable on its own for
# MKS bring-up. Split out of the overlays module by boss decision (2026-07-03):
# HAL is a direct-hardware domain with the largest intent contract, not a thin
# overlay. Integration into the new product happens later.
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(hal_control LANGUAGES CXX)

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
# src/styles. OverlayWidgets is reused from the overlays module (hexa_ui_kit candidate). NO network.
add_executable(hal_control_bench
    bench/hal_control_bench_main.cpp
    bench/FakeHalController.h
    bench/FakeHalController.cpp
    HalPanel.h
    HalPanel.cpp
    ../overlays/OverlayWidgets.h
    ../overlays/OverlayWidgets.cpp
    ../resources.qrc
)
target_include_directories(hal_control_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/bench
    ${CMAKE_CURRENT_SOURCE_DIR}/../overlays
)
target_compile_features(hal_control_bench PUBLIC cxx_std_20)
target_link_libraries(hal_control_bench PRIVATE
    hexa_contracts    # HMI DTOs (BackendTypes.h) - module-owned, no src/backend reach-through
    hexa_ui_kit
    Qt6::Core
    Qt6::Widgets
)
