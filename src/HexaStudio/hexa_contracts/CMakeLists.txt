# ==============================================================================
# hexa_contracts — the DTO layer of the new product (module-owned, header-only).
# ------------------------------------------------------------------------------
# The typed data that crosses every seam of the new product: the HMI status/config DTOs
# (BackendTypes.h: HmiRobotStatus/HmiSystemConfig/HmiHalConfig/HmiTrajectoryData) and the program
# authoring DTO (ProgramData.h: ProgramCommand/CommandType). It is the base of the studio module graph
# (deps point down to it): hexa_backend, the feature modules and app_shell all depend on it; it depends
# only on Qt. Pure Qt DTOs — no RDT wire types, no backend, no widgets.
#
# INTERFACE library: header-only, nothing to compile. Consumers get the include dir + the Qt usage
# requirement. The shipping HexaStudio (with its src/backend + src/panels/left copies) is
# archived to legacy (the copies are token-identical, so compiling both in one binary is ODR-safe).
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(hexa_contracts LANGUAGES CXX)

# Qt6::Gui is required for QVector3D used in BackendTypes.h; Core for the rest.
find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_library(hexa_contracts INTERFACE)

target_include_directories(hexa_contracts INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_features(hexa_contracts INTERFACE cxx_std_20)
target_link_libraries(hexa_contracts INTERFACE Qt6::Core Qt6::Gui)

message(STATUS "Configured hexa_contracts module (HMI DTOs: BackendTypes + ProgramData).")
