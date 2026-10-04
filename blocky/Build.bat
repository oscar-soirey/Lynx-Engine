@echo off
rem Build Blocky.dll in build\ (runs GenerateProjectFiles.bat the first time).
cd /d "%~dp0"

if not exist build\CMakeCache.txt call GenerateProjectFiles.bat

cmake --build build -j12
if errorlevel 1 pause
