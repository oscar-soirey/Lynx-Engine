@echo off
mkdir build 2>nul
cd build

cmake .. -G "MinGW Makefiles" ^
    -DCMAKE_TOOLCHAIN_FILE=S:/Programmation/Libraries/vcpkg/scripts/buildsystems/vcpkg.cmake ^
    -DVCPKG_TARGET_TRIPLET=x64-mingw-static

pause