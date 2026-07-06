@echo off
REM Launch the standalone viewport3d bench (module-owned ViewportPanel + FakeViewportController).
REM Mirrors run_jog_bench.bat: adds the MinGW + Qt runtime to PATH, then runs the built exe.

set "QT_BIN=C:\Qt\6.11.1\mingw_64\bin"
set "MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin"
set "BENCH_EXE=%~dp0..\..\..\build\bin\viewport3d_bench.exe"

set "PATH=%QT_BIN%;%MINGW_BIN%;%PATH%"

if not exist "%BENCH_EXE%" (
    echo viewport3d_bench.exe not found at "%BENCH_EXE%".
    echo Build it first:  cmake --build build --target viewport3d_bench -j 4
    exit /b 1
)

"%BENCH_EXE%" %*
