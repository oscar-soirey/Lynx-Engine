@echo off
cd /d "%~dp0build" || (
    echo Impossible d'acceder au dossier build.
    pause
    exit /b 1
)

cmake -S .. -B .
if errorlevel 1 (
    echo.
    echo Erreur pendant la configuration CMake.
    pause
    exit /b 1
)

cmake --build . -j12
if errorlevel 1 (
    echo.
    echo Erreur pendant la compilation.
    pause
    exit /b 1
)

start "" "LynxEditor.exe"