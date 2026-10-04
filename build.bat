@echo off
cd build
cmake -S .. -B .
cmake --build . -j12
ImGuiEditor.exe