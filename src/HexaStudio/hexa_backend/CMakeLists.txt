# ==============================================================================
# hexa_backend — the new-product backend (module-owned BackendClient implementation).
# ------------------------------------------------------------------------------
# From-scratch Studio backend (boss decision 2026-07-03, REQ_studio_modularization_backend.md §8;
# detail REQ_hexa_backend.md). It owns the controller connection (RDT::ClientState + RdtClient on a
# worker thread) and implements the BackendClient contract directly, reproducing the E2E-validated
# RobotService data path and absorbing the RobotServiceAdapter pieces.
#
# The swap is DONE: HexaStudioNG links this lib as its only backend (REQ_hexa_backend.md §8) and
# the contract DTOs resolve against hexa_contracts (module-owned) - no src/ reach-through.
# A static library may carry the (external) BackendClient metaobject symbol unresolved — it is
# provided by app_shell's moc when HexaStudioNG links this lib.
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(hexa_backend LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)
set(CMAKE_AUTOMOC ON)

add_library(hexa_backend STATIC
    HexaBackend.h
    HexaBackend.cpp
    ProgramMapper.h
    ProgramMapper.cpp
)

target_include_directories(hexa_backend PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
    # The BackendClient contract lives in app_shell; the HMI DTOs come from hexa_contracts (linked
    # below) - no reach-through into the shipping src/ copies.
    "${CMAKE_CURRENT_SOURCE_DIR}/../app_shell"
    "${HEXA_SDK_DIR}"
)

target_compile_features(hexa_backend PUBLIC cxx_std_20)

# Foundation libs — the exact link set HexaStudioNG already uses for RobotService
# (app_shell/CMakeLists.txt). The shared RDT client, protocol, state and math live here.
target_link_libraries(hexa_backend PUBLIC
    hexa_contracts    # HMI DTOs (BackendTypes.h / ProgramData.h) - module-owned contract layer
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    rdt_bridge
    network_driver
    StateData
    rdt_protocol
    data_types
    logger
    robot_model
)

# --- Unit tests: program-pipeline round-trips (audit A2) ---------------------------------------------
# Pin the exact defect class the shipping RobotService had (fields dropped on the wire: GOTO/IF/WaitDI
# linkage, Tool/Base, blending zone) plus the local JSON envelope round-trip. Mapper only - no
# BackendClient/moc dependency.
if(TARGET gtest_main)
    add_executable(hexa_backend_tests
        tests/gtest_program_mapper.cpp
        ProgramMapper.h
        ProgramMapper.cpp
    )
    target_include_directories(hexa_backend_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(hexa_backend_tests PRIVATE cxx_std_20)
    target_link_libraries(hexa_backend_tests PRIVATE
        hexa_contracts
        rdt_protocol
        data_types
        logger
        Qt6::Core
        Qt6::Gui
        gtest_main
    )
endif()

message(STATUS "Configured hexa_backend module (module-owned BackendClient implementation).")
