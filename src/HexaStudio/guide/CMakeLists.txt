# ==============================================================================
# guide — standalone, independently-runnable guided-demo module (investor demos,
# first-contact onboarding).
# ------------------------------------------------------------------------------
# Additive module: it does NOT modify other modules. The module sources are
# compiled into the app targets via app_shell/ng_panel_sources.cmake (same
# pattern as status_bar/jog_control); this file builds only the standalone
# bench: the REAL GuidePanel + GuideRunner + GuideCallout against stand-in
# target widgets — no shell, no backend, no network.
# Requires the repo tree (root CMakeLists provides hexa_ui_kit and the SDK's
# data_types for RDT::Result).
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(guide LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # bench embeds the shared app resources (theme fonts)

if(NOT TARGET hexa_ui_kit)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../hexa_ui_kit ${CMAKE_CURRENT_BINARY_DIR}/hexa_ui_kit_build)
endif()

# --- Standalone GUI bench (runnable in isolation; --selftest for a headless smoke) ------------------
add_executable(guide_bench
    bench/guide_bench_main.cpp
    GuideTypes.h
    GuideScenarios.h
    GuideScenarios.cpp
    GuideRunner.h
    GuideRunner.cpp
    GuideCallout.h
    GuideCallout.cpp
    GuidePanel.h
    GuidePanel.cpp
    ../resources.qrc
)
target_include_directories(guide_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/bench
)
target_compile_features(guide_bench PUBLIC cxx_std_20)
target_link_libraries(guide_bench PRIVATE
    hexa_ui_kit       # theme + styled widgets (module-owned)
    data_types        # RDT::Result (typed guide errors)
    Qt6::Core
    Qt6::Widgets
)
