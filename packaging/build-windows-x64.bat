@echo off
REM Build FileWatcher for Windows x64 (requires CMake + MSVC)
cmake -S "%~dp0.." -B "%~dp0..\build" -A x64
if errorlevel 1 exit /b 1
cmake --build "%~dp0..\build" --config Release
if errorlevel 1 exit /b 1
echo.
echo Output: %~dp0..\build\bin\Release\FileWatcher.dll
echo Rename to FileWatcher64.dll when packing with packaging\MANIFEST.XML
