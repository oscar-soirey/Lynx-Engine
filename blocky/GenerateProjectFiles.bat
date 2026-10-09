@echo off
rem Configure Blocky in build\ (MinGW, like the engine).
rem The engine is found by find_package(Lynx) (see CMakeLists.txt) : the engine of
rem the editor that runs this file, else the CMake package registry (versions installed
rem by the launcher), else the newest installed version. To choose one :
rem set LYNX_DIR=%LOCALAPPDATA%\Lynx\versions\<version>\sdk\cmake before running this file.
cd /d "%~dp0"

set LYNX_ARG=
if defined LYNX_DIR set LYNX_ARG=-DLynx_DIR="%LYNX_DIR%"

cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug %LYNX_ARG%
if errorlevel 1 pause
