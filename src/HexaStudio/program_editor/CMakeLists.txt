# ==============================================================================
# program_editor — standalone, independently-testable HMI program authoring module.
# ------------------------------------------------------------------------------
# Additive module: it does NOT modify the HexaStudio executable or HexaMotion.
# Phase 1 contains ProgramBuilder (authoring core) + its unit tests. The model
# adapter, the standalone bench and FakeController are added in later phases.
# Integration into HexaStudio/HexaMotion happens only after this module is proven.
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(program_editor LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
set(CMAKE_AUTOMOC ON)

add_library(program_editor STATIC
    ProgramBuilder.h
    ProgramBuilder.cpp
    ProgramEditorModel.h
    ProgramEditorModel.cpp
)

target_include_directories(program_editor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(program_editor PUBLIC
    hexa_contracts    # ProgramData.h (ProgramCommand/CommandType) - module-owned contract layer
    Qt6::Core
    data_types        # RDT::Result (header-only style lib; no network/StateData/rdt_bridge)
)

target_compile_features(program_editor PUBLIC cxx_std_20)

# --- Unit tests (pure logic; no widgets, no RobotService) ---------------------
if(TARGET gtest_main)
    add_executable(program_builder_tests test/ProgramBuilderTests.cpp)
    target_link_libraries(program_builder_tests PRIVATE
        program_editor
        gtest
        gtest_main
        Qt6::Core
    )
    add_test(NAME program_builder_tests COMMAND program_builder_tests)

    add_executable(program_editor_model_tests test/ProgramEditorModelTests.cpp)
    target_link_libraries(program_editor_model_tests PRIVATE
        program_editor
        gtest
        gtest_main
        Qt6::Core
    )
    add_test(NAME program_editor_model_tests COMMAND program_editor_model_tests)

    add_executable(fake_controller_tests
        bench/FakeController.h
        bench/FakeController.cpp
        test/FakeControllerTests.cpp
    )
    target_include_directories(fake_controller_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bench)
    target_link_libraries(fake_controller_tests PRIVATE
        program_editor
        gtest
        gtest_main
        Qt6::Core
    )
    add_test(NAME fake_controller_tests COMMAND fake_controller_tests)

    # Pipeline integration test: studio authoring (ProgramBuilder) -> studio/wire mapping
    # (hexa_backend ProgramMapper) -> controller execution (HexaMotion ProgramSequencer). The
    # sequencer source is compiled directly (as HexaCore does) so the test needs no controller lib.
    # hexa_backend is registered before this module, so its target already exists here; linking it
    # pulls only the ProgramMapper object (HexaBackend itself is not referenced).
    # This is a CROSS-REPO integration test: it compiles HexaMotion's ProgramSequencer.cpp directly.
    # In this standalone HexaStudio repository the HexaMotion sources are absent, so the test is only
    # built when they are present (i.e. in the combined mono-repo). HexaStudio itself never depends on
    # HexaMotion - only this end-to-end pipeline test does.
    set(SEQUENCER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../HexaMotion/modules/program_sequencer/src)
    if(EXISTS "${SEQUENCER_DIR}/ProgramSequencer.cpp")
        add_executable(pipeline_e2e_tests
            test/PipelineE2ETests.cpp
            ${SEQUENCER_DIR}/ProgramSequencer.cpp
        )
        target_include_directories(pipeline_e2e_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../hexa_backend
            ${SEQUENCER_DIR}
        )
        target_link_libraries(pipeline_e2e_tests PRIVATE
            program_editor      # ProgramBuilder (studio authoring)
            hexa_backend        # program_mapper::toRdt / toHmi (studio <-> wire)
            rdt_protocol        # RDT::NetProtocol program/step/condition
            data_types          # AxisSet / units / RDT::Result
            gtest
            gtest_main
            Qt6::Core
        )
        add_test(NAME pipeline_e2e_tests COMMAND pipeline_e2e_tests)
    else()
        message(STATUS "program_editor: pipeline_e2e_tests skipped - HexaMotion program_sequencer source not present (cross-repo integration test; build it in the mono-repo).")
    endif()
endif()

# --- Standalone bench for the builder-based ProgramEditorPanel (decoupled PanelLeft replacement) -----
# Hosts the real panel + FakeController, wired the way the shell will at integration. Reuses the clean
# (RobotService-free) ProgramDelegate / ProgramModel directly; theme/widgets/keyboard come from the
# hexa_ui_kit module (module-owned), not a reach-through into src/styles.
if(NOT TARGET hexa_contracts)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../hexa_contracts ${CMAKE_CURRENT_BINARY_DIR}/hexa_contracts_build)
endif()
if(NOT TARGET hexa_ui_kit)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../hexa_ui_kit ${CMAKE_CURRENT_BINARY_DIR}/hexa_ui_kit_build)
endif()
add_executable(program_editor_panel_bench
    bench/panel_bench_main.cpp
    bench/FakeController.h
    bench/FakeController.cpp
    ProgramEditorPanel.h
    ProgramEditorPanel.cpp
    ProgramIssueDelegate.h
    ProgramIssueDelegate.cpp
    ProgramDelegate.h
    ProgramDelegate.cpp
    ProgramModel.h
    ProgramModel.cpp
)
target_include_directories(program_editor_panel_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/bench
)
target_link_libraries(program_editor_panel_bench PRIVATE
    program_editor
    hexa_contracts    # HMI DTOs (BackendTypes.h) - module-owned, no src/backend reach-through
    hexa_ui_kit
    Qt6::Core
    Qt6::Widgets
)
