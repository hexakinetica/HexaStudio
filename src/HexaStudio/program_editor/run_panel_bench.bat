@echo off
REM ==============================================================================
REM Launch the standalone ProgramEditorPanel bench (builder-based PanelLeft +
REM FakeController). No controller, no network, no HexaStudio required.
REM Pass --selftest for a headless smoke run (exits 0). Any other args pass through.
REM ==============================================================================
setlocal

set "QT_DIR=C:\Qt\6.11.1\mingw_64"
set "MINGW_DIR=C:\Qt\Tools\mingw1310_64\bin"
set "BENCH=%~dp0..\..\..\build\bin\program_editor_panel_bench.exe"

if not exist "%BENCH%" (
    echo [run_panel_bench] Executable not found:
    echo     %BENCH%
    echo Build it first from the repo root:
    echo     cmake --build build --target program_editor_panel_bench
    exit /b 1
)

REM Qt runtime DLLs + MinGW runtime + Qt platform plugins (qwindows.dll).
set "PATH=%QT_DIR%\bin;%MINGW_DIR%;%PATH%"
set "QT_PLUGIN_PATH=%QT_DIR%\plugins"

echo [run_panel_bench] Launching %BENCH% %*
"%BENCH%" %*
set "RC=%ERRORLEVEL%"
endlocal & exit /b %RC%
