@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%build-windows"
set "INSTALL_DIR=C:\Program Files (x86)\Common Files\VST3"

:: ── Find vcvars64.bat ─────────────────────────────────────────────────────────
set "VCVARS="

if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)

if "!VCVARS!"=="" (
    echo ERROR: Could not find vcvars64.bat.
    echo Make sure Visual Studio Build Tools 2022 is installed with the C++ workload.
    pause
    exit /b 1
)

echo Found VS environment: !VCVARS!
call "!VCVARS!"

:: ── Check cmake ───────────────────────────────────────────────────────────────
where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: cmake not found on PATH.
    echo Install CMake from https://cmake.org and tick "Add to PATH".
    pause
    exit /b 1
)

cmake --version

:: ── Configure ─────────────────────────────────────────────────────────────────
echo.
echo Configuring...
cmake -B "%BUILD_DIR%" -S "%SCRIPT_DIR%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo ERROR: CMake configure failed.
    pause
    exit /b 1
)

:: ── Build ─────────────────────────────────────────────────────────────────────
echo.
echo Building...
cmake --build "%BUILD_DIR%" --config Release --parallel

if errorlevel 1 (
    echo ERROR: Build failed.
    pause
    exit /b 1
)

:: ── Install VST3 ──────────────────────────────────────────────────────────────
echo.
echo Build succeeded!

set "VST3_SRC=%BUILD_DIR%\bin\Release\Octofilter.vst3"
if not exist "%VST3_SRC%" (
    set "VST3_SRC=%BUILD_DIR%\bin\Octofilter.vst3"
)
if not exist "%VST3_SRC%" (
    echo Could not find built VST3. Listing bin folder:
    dir "%BUILD_DIR%\bin" /s /b 2>nul
    pause
    exit /b 1
)

echo Copying VST3 to: %INSTALL_DIR%
xcopy /E /I /Y "%VST3_SRC%" "%INSTALL_DIR%\Octofilter.vst3\"

if errorlevel 1 (
    echo Copy failed - make sure you are running as Administrator.
) else (
    echo Done! Rescan plugins in Ableton Live to load Octofilter.
)

pause
