# ==============================================================================
# viewport3d — standalone, independently-runnable 3D viewport module.
# ------------------------------------------------------------------------------
# Additive module: it does NOT modify the HexaStudio executable or HexaMotion.
# It hosts the module-owned ViewportPanel (a clean re-implementation of the
# shipping PanelView3D) against an offline FakeViewportController, so the viewport
# builds, runs and is design-reviewed in isolation from the controller/network.
# Integration into the new product (ShellWindow / HexaStudioNG) happens later.
#
# Unlike the lighter panel modules, this one needs the robot_model foundation lib
# (URDF -> visual chain) and its Orocos-KDL chain, so the bench is built WITHIN the
# tree (robot_model/data_types/logger are added by the root before this block).
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(viewport3d LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Quick Qml Quick3D Quick3DPrivate Quick3DAssetUtils Quick3DAssetUtilsPrivate)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # embeds the module-owned QML scene + the shared app resources (theme fonts)

# Shared UI-kit / DTO modules (self-contained). Build them standalone if the tree has not provided them.
if(NOT TARGET hexa_contracts)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../hexa_contracts ${CMAKE_CURRENT_BINARY_DIR}/hexa_contracts_build)
endif()
if(NOT TARGET hexa_ui_kit)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../hexa_ui_kit ${CMAKE_CURRENT_BINARY_DIR}/hexa_ui_kit_build)
endif()

# --- Standalone GUI bench (runnable in isolation) ---------------------------------------------------
# Compiles the module-owned ViewportPanel + the cinematic scene; robot geometry comes from the shared
# robot_model lib (reach-through link, NOT a copy). NO HexaStudio, NO network.
add_executable(viewport3d_bench
    bench/viewport3d_bench_main.cpp
    bench/FakeViewportController.h
    bench/FakeViewportController.cpp
    ViewportPanel.h
    ViewportPanel.cpp
    UrdfVisualModel.h
    UrdfVisualModel.cpp
    viewport3d.qrc
    ../resources.qrc
)
target_include_directories(viewport3d_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/bench
)
# Default robot model for the bench = the PRODUCTION robot (hexacore_config.json urdfPath).
# --urdf overrides it; --rx overrides the root tilt (Medium is Z-up -> 0, the Mini needs --rx 90).
target_compile_definitions(viewport3d_bench PRIVATE
    VIEWPORT3D_DEFAULT_URDF="${HEXA_SDK_DIR}/robots/HexaArmMedium_Light_GLB/urdf/HexaArmMedium_Light.SLDASM.urdf"
)
target_compile_features(viewport3d_bench PUBLIC cxx_std_20)
target_link_libraries(viewport3d_bench PRIVATE
    hexa_contracts        # HMI DTOs (BackendTypes.h): HmiMotionStatus / HmiTrajectoryData
    hexa_ui_kit           # theme + fonts for the bench host window
    robot_model           # KinematicModel: URDF -> ordered visual chain (+ RobotModelTypes.h)
    data_types
    logger
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::Qml
    Qt6::Quick3D
    Qt6::Quick3DPrivate
    Qt6::Quick3DAssetUtils          # RuntimeLoader: auto-upgrade a link to a glTF/GLB/OBJ sibling
    Qt6::Quick3DAssetUtilsPrivate
)

# --- Unit tests for the pure loader ------------------------------------------------------------------
# Protects the exact logic that has produced real defects (terminal-flange composition, authored-colour
# propagation, mesh path resolution) without a GPU. Deliberately pinned to the bundled Mini URDF as a
# stable fixture (its 30 mm flange and authored colours are the asserted values), independent of which
# robot is active in production.
if(TARGET gtest_main)
    add_executable(viewport3d_tests
        tests/gtest_urdf_visual_model.cpp
        UrdfVisualModel.h
        UrdfVisualModel.cpp
    )
    target_include_directories(viewport3d_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(viewport3d_tests PRIVATE cxx_std_20)
    target_compile_definitions(viewport3d_tests PRIVATE
        VIEWPORT3D_TEST_URDF="${HEXA_SDK_DIR}/robots/HexaArm_Mini_Nema_Assem.SLDASM5/urdf/HexaArm_Mini_Nema_Assem.SLDASM5.urdf"
    )
    target_link_libraries(viewport3d_tests PRIVATE
        robot_model
        data_types
        logger
        Qt6::Core
        Qt6::Gui
        gtest_main
    )
endif()
