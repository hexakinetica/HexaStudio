cmake_minimum_required(VERSION 3.16)
project(HexaVRC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Название исполняемого файла: HexaVRC
add_executable(HexaVRC
    main.cpp
    HmiBridge.h
    HmiBridge.cpp
    ../Shared/RdtProtocol.h
    MockJsonHelpers.h
    ConfigManager.h
    ConfigManager.cpp
    MockMath.h
    MockController.h
    MockController.cpp
)

# Путь к общим файлам
target_include_directories(HexaVRC PRIVATE ../Shared)

if(WIN32)
    target_link_libraries(HexaVRC PRIVATE ws2_32)
endif()