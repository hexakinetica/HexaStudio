# ==============================================================================
# hexa_ui_kit — shared UI foundation of the new product (module-owned).
# ------------------------------------------------------------------------------
# The theme, styled widgets and on-screen keyboard used by every feature module and by the shell.
# It is a leaf module (Qt Widgets only; no backend, no DTOs), so it has no dependency on
# hexa_contracts/hexa_backend. HexaStudioNG links it instead of reaching through into src/styles and
# src/cyberkeyboard; the shipping HexaStudio keeps its own copies until src/ is archived to legacy.
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(hexa_ui_kit LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)

add_library(hexa_ui_kit STATIC
    HexaTheme.h
    HexaWidgets.h
    HexaWidgets.cpp
    cyberkeyboard.h
    cyberkeyboard.cpp
)

target_include_directories(hexa_ui_kit PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_features(hexa_ui_kit PUBLIC cxx_std_20)
target_link_libraries(hexa_ui_kit PUBLIC Qt6::Widgets)

message(STATUS "Configured hexa_ui_kit module (theme + widgets + cyberkeyboard).")
