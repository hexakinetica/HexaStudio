# ==============================================================================
# HexaStudio - operator HMI / GUI (HexaStudioNG) application repository.
# ------------------------------------------------------------------------------
# Independent, production-ready repository. It links directly against the single
# HexaMotion-SDK foundation repository - there is NO local copy of the SDK.
#
# HexaMotion-SDK is resolved as:
#   1) a git submodule at external/HexaMotion-SDK (published layout), or
#   2) the sibling HexaMotion-SDK repository in the HexaKinetica dev workspace.
#
# Requires Qt6 (Widgets/Quick/Quick3D). Configure with the Qt toolchain on
# CMAKE_PREFIX_PATH, e.g. -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64.
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(HexaStudio LANGUAGES CXX)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-unused-result)
endif()

# --- Resolve HexaMotion-SDK (submodule first, sibling fallback) -------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/HexaMotion-SDK/CMakeLists.txt")
    get_filename_component(HEXA_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/HexaMotion-SDK" ABSOLUTE)
else()
    get_filename_component(HEXA_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../HexaMotion-SDK" ABSOLUTE)
endif()
if(NOT EXISTS "${HEXA_SDK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "HexaMotion-SDK not found at '${HEXA_SDK_DIR}'. Add it as a submodule (external/HexaMotion-SDK) or place it beside this repository.")
endif()
set(HEXA_SDK_DIR "${HEXA_SDK_DIR}" CACHE PATH "Path to the HexaMotion-SDK foundation repository" FORCE)
message(STATUS "HexaStudio: using HexaMotion-SDK at ${HEXA_SDK_DIR}")

# Build the SDK umbrella (foundation targets + vendored third_party + GoogleTest).
add_subdirectory("${HEXA_SDK_DIR}" "${CMAKE_BINARY_DIR}/HexaMotion-SDK")

# --- HexaStudioNG module graph ----------------------------------------------------------------------
add_subdirectory(src/HexaStudio/hexa_contracts)
add_subdirectory(src/HexaStudio/hexa_backend)
add_subdirectory(src/HexaStudio/hexa_ui_kit)
add_subdirectory(src/HexaStudio/program_editor)
add_subdirectory(src/HexaStudio/jog_control)
add_subdirectory(src/HexaStudio/status_bar)
add_subdirectory(src/HexaStudio/overlays)
add_subdirectory(src/HexaStudio/hal_control)
add_subdirectory(src/HexaStudio/guide)
add_subdirectory(src/HexaStudio/viewport3d)
add_subdirectory(src/HexaStudio/app_shell)

message(STATUS "Configured HexaStudio (HexaStudioNG HMI) against HexaMotion-SDK.")
