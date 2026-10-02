@echo off
setlocal

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake was not found in PATH.
    echo Install Visual Studio 2022 with "Desktop development with C++" and CMake tools.
    exit /b 1
)

cmake -S . -B build -A x64
if errorlevel 1 exit /b %errorlevel%

cmake --build build --config Release --parallel
if errorlevel 1 exit /b %errorlevel%

ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 exit /b %errorlevel%

echo.
echo Build succeeded:
echo   build\Release\FolderMTimeFix.exe
exit /b 0
