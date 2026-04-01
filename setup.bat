@echo off
setlocal EnableDelayedExpansion
title Nova-Sight Setup

echo.
echo  ============================================================
echo   NOVA-SIGHT ^| Automated Setup
echo  ============================================================
echo.

:: ── 1. Find or install vcpkg ─────────────────────────────────────────────────
set VCPKG_ROOT=

for %%P in (
    "C:\vcpkg"
    "C:\dev\vcpkg"
    "C:\tools\vcpkg"
    "%USERPROFILE%\vcpkg"
    "%USERPROFILE%\source\vcpkg"
    "%LOCALAPPDATA%\vcpkg"
) do (
    if exist "%%~P\vcpkg.exe" (
        set VCPKG_ROOT=%%~P
        goto :vcpkg_found
    )
)

echo  [!] vcpkg not found in common locations.
echo      Installing vcpkg to C:\vcpkg ...
echo.
git --version >nul 2>&1 || (
    echo  [ERROR] Git is not installed or not in PATH.
    echo          Download it from: https://git-scm.com/download/win
    pause & exit /b 1
)
git clone https://github.com/microsoft/vcpkg C:\vcpkg
if errorlevel 1 ( echo  [ERROR] Failed to clone vcpkg. & pause & exit /b 1 )
call C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
if errorlevel 1 ( echo  [ERROR] vcpkg bootstrap failed. & pause & exit /b 1 )
set VCPKG_ROOT=C:\vcpkg

:vcpkg_found
echo  [OK] vcpkg found at: %VCPKG_ROOT%

:: ── 2. Verify CMake ───────────────────────────────────────────────────────────
cmake --version >nul 2>&1 || (
    echo.
    echo  [ERROR] CMake is not installed or not in PATH.
    echo          Download it from: https://cmake.org/download/
    pause & exit /b 1
)
echo  [OK] CMake found.

:: ── 3. Configure ─────────────────────────────────────────────────────────────
echo.
echo  Configuring build...
cmake -B build -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo.
    echo  [ERROR] CMake configuration failed.
    echo          Make sure Visual Studio 2022 with C++ Desktop workload is installed.
    echo          https://visualstudio.microsoft.com/downloads/
    pause & exit /b 1
)
echo  [OK] Configuration complete.

:: ── 4. Build ─────────────────────────────────────────────────────────────────
echo.
echo  Building (this may take a few minutes on first run while vcpkg
echo  downloads and compiles dependencies)...
echo.
cmake --build build --config Release
if errorlevel 1 (
    echo.
    echo  [ERROR] Build failed. See output above for details.
    pause & exit /b 1
)
echo.
echo  [OK] Build complete.

:: ── 5. Download Tesseract language data ──────────────────────────────────────
set TESSDATA_DIR=build\Release\tessdata
if not exist "%TESSDATA_DIR%\eng.traineddata" (
    echo.
    echo  Downloading Tesseract language data (eng.traineddata)...
    if not exist "%TESSDATA_DIR%" mkdir "%TESSDATA_DIR%"
    curl -L -o "%TESSDATA_DIR%\eng.traineddata" ^
        "https://github.com/tesseract-ocr/tessdata/raw/main/eng.traineddata"
    if errorlevel 1 (
        echo  [WARN] Could not download eng.traineddata automatically.
        echo         Download it manually from:
        echo         https://github.com/tesseract-ocr/tessdata/raw/main/eng.traineddata
        echo         and place it in: %TESSDATA_DIR%\
    ) else (
        echo  [OK] eng.traineddata downloaded.
    )
) else (
    echo  [OK] Tesseract language data already present.
)

:: ── 6. Open games.json if API key is empty ────────────────────────────────────
findstr /C:"\"groq_api_key\": \"\"" data\games.json >nul 2>&1
if not errorlevel 1 (
    echo.
    echo  ============================================================
    echo   ACTION NEEDED: Set your Groq API key
    echo  ============================================================
    echo.
    echo  Opening data\games.json ...
    echo  Replace the empty  "groq_api_key": ""  with your key.
    echo  Get a free key at: https://console.groq.com
    echo.
    notepad data\games.json
)

:: ── 7. Done ───────────────────────────────────────────────────────────────────
echo.
echo  ============================================================
echo   All done! Run Nova-Sight with:
echo     build\Release\nova-sight.exe
echo  ============================================================
echo.
pause
