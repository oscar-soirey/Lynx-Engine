@echo off
rem Configure Blocky in build\ (MinGW, like the engine).
rem The engine is found through the CMake package registry ; to use another
rem engine build : set LYNX_DIR=C:\path\to\lynx\build before running this file.
cd /d "%~dp0"

set LYNX_ARG=
if defined LYNX_DIR set LYNX_ARG=-DLynx_DIR="%LYNX_DIR%"

cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug %LYNX_ARG%
if errorlevel 1 pause
