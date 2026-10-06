# =============================================================================
# Blocky : game project for the Lynx engine
# -----------------------------------------------------------------------------
#   blocky/
#     assets/        game assets
#     src/           game code -> Blocky.dll
#     build/         Blocky.dll (the Lynx editor / runtime look for it here)
#     input.json     key bindings
#
# The engine is found with find_package(Lynx) : configuring the engine once
# registers its build folder in the CMake package registry, so this project
# can be anywhere on the computer. To use another engine build :
#     cmake ... -DLynx_DIR=<engine build folder>
#
# Use the same compiler as the engine (MinGW).
# =============================================================================

cmake_minimum_required(VERSION 3.21)

project(Blocky
	LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Lynx CONFIG)

if(NOT Lynx_FOUND)
	message(FATAL_ERROR
		"Lynx engine not found. Configure the engine once (its CMake registers it "
		"for every project of this computer), or give its build folder : "
		"-DLynx_DIR=C:/path/to/lynx/<build folder>")
endif()

message(STATUS "Lynx engine : ${LYNX_BUILD_DIR}")


file(GLOB_RECURSE BLOCKY_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(Blocky SHARED
	${BLOCKY_SOURCES}
)

target_include_directories(Blocky PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(Blocky PRIVATE
	Lynx::Lynx
	Lynx::HRL
)

# Blocky.dll goes to <project>/build, whatever the CMake build folder is
# (CLion cmake-build-debug...) : the editor and the runtime look there.
# ($<0:> : no Debug/Release sub folder with multi-config generators.)
set_target_properties(Blocky PROPERTIES
	PREFIX ""
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/$<0:>"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/$<0:>"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib"
)

# Plugin MyPlugin (created by the editor)
add_subdirectory(plugins/MyPlugin)
