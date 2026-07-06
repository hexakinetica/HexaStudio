@echo off
REM ==============================================================================
REM Launch the standalone jog-panel bench (module-owned JogPanel + FakeJogController).
REM No controller, no network, no HexaStudio required.
REM Pass --selftest for a headless smoke run (exits 0), --screenshot <file.png> for a
REM one-shot design render. Any other args pass through.
REM ==============================================================================
setlocal

set "QT_DIR=C:\Qt\6.11.1\mingw_64"
set "MINGW_DIR=C:\Qt\Tools\mingw1310_64\bin"
set "BENCH=%~dp0..\..\..\build\bin\jog_control_bench.exe"

if not exist "%BENCH%" (
    echo [run_jog_bench] Executable not found:
    echo     %BENCH%
    echo Build it first from the repo root:
    echo     cmake --build build --target jog_control_bench
    exit /b 1
)

REM Qt runtime DLLs + MinGW runtime + Qt platform plugins (qwindows.dll).
set "PATH=%QT_DIR%\bin;%MINGW_DIR%;%PATH%"
set "QT_PLUGIN_PATH=%QT_DIR%\plugins"

echo [run_jog_bench] Launching %BENCH% %*
"%BENCH%" %*
set "RC=%ERRORLEVEL%"
endlocal & exit /b %RC%
