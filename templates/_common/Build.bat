@echo off
rem Build {{PROJECT_NAME}}.dll in build\ (runs GenerateProjectFiles.bat the first time).
cd /d "%~dp0"

if not exist build\CMakeCache.txt call GenerateProjectFiles.bat

cmake --build build
if errorlevel 1 pause
