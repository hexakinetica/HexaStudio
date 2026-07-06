# ==============================================================================
# app_shell — the integration foundation of the new product (boss directive
# 2026-07-03): the composition root (ShellWindow) + THE backend contract
# (BackendClient) + a coherent offline controller (FakeBackend).
# ------------------------------------------------------------------------------
# Additive module: it does NOT modify the HexaStudio executable or HexaMotion.
# This is the ONLY module allowed to depend on the feature modules - it is the
# shell layer above them (see MODULARIZATION § 5), not a peer. Feature modules
# still never see each other or the backend type.
# The production backend is the module-owned hexa_backend (HexaBackend : BackendClient), linked by
# HexaStudioNG; the wiring in ShellWindow does not change.
# The per-module source list lives ONCE in ng_panel_sources.cmake (both targets include it).
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(app_shell LANGUAGES CXX)

# Quick/Quick3D: the viewport3d module (integrated) renders via QQuickView + QtQuick3D.
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Quick Qml Quick3D Quick3DPrivate Quick3DAssetUtils Quick3DAssetUtilsPrivate)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # embeds the shared app resources (theme fonts) + the viewport3d QML scene

include(${CMAKE_CURRENT_SOURCE_DIR}/ng_panel_sources.cmake)

# Common Qt/module dependencies of every target assembling the full panel set.
set(HEXA_NG_PANEL_LIBS
    program_editor    # ProgramBuilder / ProgramEditorModel (+ data_types)
    hexa_contracts    # HMI DTOs (BackendTypes.h) - module-owned, no src/backend reach-through
    hexa_ui_kit       # theme + styled widgets + cyberkeyboard (module-owned)
    robot_model       # viewport3d: URDF -> visual chain (shared foundation lib)
    data_types
    logger
    Qt6::Core
    Qt6::Widgets
    Qt6::Quick
    Qt6::Qml
    Qt6::Quick3D
    Qt6::Quick3DPrivate
    Qt6::Quick3DAssetUtils
    Qt6::Quick3DAssetUtilsPrivate
)

# --- The assembled application, runnable in isolation ------------------------------------------
add_executable(app_shell_bench
    bench/app_shell_bench_main.cpp
    bench/FakeBackend.h
    bench/FakeBackend.cpp
    ${HEXA_NG_PANEL_SOURCES}
)
target_include_directories(app_shell_bench PRIVATE
    ${HEXA_NG_PANEL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/bench
)
# The offline demo drives the viewport with the real bench URDF (same file the viewport3d bench uses).
target_compile_definitions(app_shell_bench PRIVATE
    APP_SHELL_DEMO_URDF="${CMAKE_SOURCE_DIR}/src/HexaArm_Mini_Nema_Assem.SLDASM5/urdf/HexaArm_Mini_Nema_Assem.SLDASM5.urdf"
)
target_compile_features(app_shell_bench PUBLIC cxx_std_20)
target_link_libraries(app_shell_bench PRIVATE
    ${HEXA_NG_PANEL_LIBS}
)

# --- HexaStudioNG: the replacement application (REAL backend via hexa_backend) -------------------
find_package(Qt6 REQUIRED COMPONENTS Network)
add_executable(HexaStudioNG
    main_ng.cpp
    ${HEXA_NG_PANEL_SOURCES}
)
target_include_directories(HexaStudioNG PRIVATE
    ${HEXA_NG_PANEL_INCLUDE_DIRS}
    "${HEXA_SDK_DIR}"
)
target_compile_features(HexaStudioNG PUBLIC cxx_std_20)
target_link_libraries(HexaStudioNG PRIVATE
    ${HEXA_NG_PANEL_LIBS}
    hexa_backend      # module-owned BackendClient implementation (replaces RobotService + adapter)
    Qt6::Network
    rdt_bridge
    network_driver
    StateData
    rdt_protocol
)

# --- HexaStudioDesktop: the single-process edition (controller in-process, loopback RDT link) -----
# Defined ONLY when the HexaStudioDesktop top-level project configures this directory: it sets
# HEXA_DESKTOP_EDITION and provides hexacore_runtime (the shared controller assembly from the
# HexaMotion repository). The networked HexaStudio build never sees this target. The panel set,
# the backend and the whole module graph are byte-identical to HexaStudioNG - only the transport
# (loopback) and the composition root (main_desktop.cpp) differ.
if(HEXA_DESKTOP_EDITION)
    if(NOT TARGET hexacore_runtime)
        message(FATAL_ERROR "HEXA_DESKTOP_EDITION requires the hexacore_runtime target (configure via HexaStudioDesktop/CMakeLists.txt).")
    endif()
    # RC: windres compiles app_icon.rc so the product icon is embedded in the
    # executable (Explorer / taskbar / installer shortcuts). Windows-only need,
    # and this target only exists on the Windows desktop build.
    enable_language(RC)
    # windres does not scan the .rc for file references, so CMake must be told
    # explicitly that the object depends on the .ico - otherwise a regenerated
    # icon silently ships stale (observed 2026-07-07).
    set_source_files_properties(app_icon.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../resources/HexaStudioDesktop.ico")
    add_executable(HexaStudioDesktop
        main_desktop.cpp
        app_icon.rc
        ${HEXA_NG_PANEL_SOURCES}
    )
    target_include_directories(HexaStudioDesktop PRIVATE
        ${HEXA_NG_PANEL_INCLUDE_DIRS}
        "${HEXA_SDK_DIR}"
    )
    target_compile_features(HexaStudioDesktop PUBLIC cxx_std_20)
    target_link_libraries(HexaStudioDesktop PRIVATE
        ${HEXA_NG_PANEL_LIBS}
        hexa_backend        # the same production BackendClient as HexaStudioNG
        Qt6::Network
        rdt_bridge
        network_driver      # LoopbackNetwork lives here
        StateData
        rdt_protocol
        hexacore_runtime    # the same controller stack as HexaCore.exe
    )
endif()
