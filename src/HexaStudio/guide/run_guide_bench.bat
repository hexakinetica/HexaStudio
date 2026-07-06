@echo off
REM ==============================================================================
REM Launch the standalone guide bench (REAL GuidePanel + GuideRunner + GuideCallout
REM against stand-in target widgets). No controller, no network, no HexaStudio.
REM Pass --selftest for a headless smoke run (exits 0 when TOUR finishes and every
REM virtual click landed). Any other args pass through.
REM ==============================================================================
setlocal

set "QT_DIR=C:\Qt\6.11.1\mingw_64"
set "MINGW_DIR=C:\Qt\Tools\mingw1310_64\bin"
set "BENCH=%~dp0..\..\..\build\bin\guide_bench.exe"

if not exist "%BENCH%" (
    echo [run_guide_bench] Executable not found:
    echo     %BENCH%
    echo Build it first from the repo root:
    echo     cmake --build build --target guide_bench
    exit /b 1
)

REM Qt runtime DLLs + MinGW runtime + Qt platform plugins (qwindows.dll).
set "PATH=%QT_DIR%\bin;%MINGW_DIR%;%PATH%"
set "QT_PLUGIN_PATH=%QT_DIR%\plugins"

echo [run_guide_bench] Launching %BENCH% %*
"%BENCH%" %*
set "RC=%ERRORLEVEL%"
endlocal & exit /b %RC%
