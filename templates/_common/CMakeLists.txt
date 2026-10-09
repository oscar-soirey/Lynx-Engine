# =============================================================================
# {{PROJECT_TITLE}} : game project for the Lynx engine
# -----------------------------------------------------------------------------
#   assets/        game assets (world.xml, voxels.json, sprites, JS classes...)
#   src/           game code -> {{PROJECT_NAME}}.dll
#   build/         {{PROJECT_NAME}}.dll (the Lynx editor / runtime look for it here)
#   input.json     key bindings
#
# The engine is found with find_package(Lynx), the project can be anywhere :
#   1. builds started by the Lynx editor : the engine of that editor (Lynx_DIR) ;
#   2. CMake package registry : versions installed by the Lynx launcher, and
#      engine build folders (configuring the engine sources registers them) ;
#   3. otherwise, the most recent version in %LOCALAPPDATA%/Lynx/versions.
# To choose an engine :
#     cmake ... -DLynx_DIR=<version>/sdk/cmake   (or <engine build folder>)
#
# Use the same compiler as the engine (MinGW).
# =============================================================================

cmake_minimum_required(VERSION 3.21)

project({{PROJECT_NAME}}
	LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Versions installed by the Lynx launcher, most recent first (last resort)
set(LYNX_INSTALLED_VERSIONS "")
if(DEFINED ENV{LOCALAPPDATA})
	file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" LYNX_LOCALAPPDATA)
	file(GLOB LYNX_INSTALLED_VERSIONS LIST_DIRECTORIES true
		"${LYNX_LOCALAPPDATA}/Lynx/versions/*/sdk/cmake")
	list(SORT LYNX_INSTALLED_VERSIONS COMPARE NATURAL ORDER DESCENDING)
endif()

find_package(Lynx CONFIG PATHS ${LYNX_INSTALLED_VERSIONS})

if(NOT Lynx_FOUND)
	message(FATAL_ERROR
		"Lynx engine not found. Install a version of the engine with the Lynx "
		"launcher (Engine tab), or give its folder : "
		"-DLynx_DIR=%LOCALAPPDATA%/Lynx/versions/<version>/sdk/cmake "
		"(engine sources : -DLynx_DIR=<engine build folder>). "
		"Versions published before the SDK cannot compile C++ games : install a newer one.")
endif()

message(STATUS "Lynx engine : ${LYNX_BUILD_DIR}")

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library({{PROJECT_NAME}} SHARED
	${GAME_SOURCES}
)

target_include_directories({{PROJECT_NAME}} PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries({{PROJECT_NAME}} PRIVATE
	Lynx::Lynx
	Lynx::HRL
)

# {{PROJECT_NAME}}.dll goes to <project>/build, whatever the CMake build folder is
# (CLion cmake-build-debug...) : the editor and the runtime look there.
# ($<0:> : no Debug/Release sub folder with multi-config generators.)
set_target_properties({{PROJECT_NAME}} PROPERTIES
	PREFIX ""
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/$<0:>"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/$<0:>"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib"
)
