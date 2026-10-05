cmake_minimum_required(VERSION 3.21)
project(lynx
	LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CXXE_COMPILER cxxe)

include_directories(src)
include_directories(third-party)

find_package(OpenAL CONFIG REQUIRED)

file(GLOB PHYSFS_SOURCE
	"${CMAKE_SOURCE_DIR}/third-party/physfs/*.c"
	"${CMAKE_SOURCE_DIR}/third-party/physfs/win/*.c"
)

add_subdirectory(third-party/quickjs-ng)

set(HRL_LIB  "${CMAKE_CURRENT_SOURCE_DIR}/third-party/hrl/lib/libhrldll.dll.a")
set(GLFW_LIB "${CMAKE_CURRENT_SOURCE_DIR}/third-party/glfw/lib/libglfw3.a")


## Engine (lynx.dll)

add_library(Lynx SHARED
		#Dependencies
		third-party/inih/ini.c
		third-party/xml/tinyxml2.cpp
		${PHYSFS_SOURCE}

		src/core/Engine.cpp
		src/core/Filesystem.cpp
		src/core/Level.cpp
		src/core/RessourceManager.cpp
		src/core/Voxels.cpp

		src/core/data/IniParser.cpp
		src/core/Private/SystemModule.cpp

		src/gameplay/Object.cpp
		src/gameplay/Actor.cpp
		src/gameplay/Input.cpp
		#src/gameplay/PlayerController.cpp
		src/gameplay/Private/InputManager.cpp
		src/gameplay/CameraShake.cpp
		src/gameplay/AnimationSystem.cpp
		src/gameplay/AIBehavior.cpp
		src/gameplay/Components.cpp
		src/gameplay/SpriteComponents.cpp
		src/gameplay/CameraComponent.cpp
		src/gameplay/BoxColliderComponent.cpp
		src/gameplay/SoundSourceComponent.cpp
		src/gameplay/LightComponent.cpp

		src/audio/AudioDecoder.cpp
		src/audio/Audio2D.cpp
		src/audio/AudioCommon.cpp
		src/audio/AudioListener.cpp
		src/audio/AudioSource.cpp

		src/widgets/WidgetScene.cpp
		src/widgets/WidgetContainers.cpp

		src/scripting/ScriptSystem.cpp
		src/scripting/ComponentBindings.cpp
)

target_compile_definitions(Lynx PRIVATE
	LYNX_BUILD_DLL
)

target_compile_definitions(Lynx
		PRIVATE
		LYNX_VERSION_YEAR=2026
		LYNX_VERSION_MAJOR=1
		LYNX_VERSION_MINOR=0
)

target_link_libraries(Lynx PRIVATE
		${HRL_LIB}
		${GLFW_LIB}
		OpenAL::OpenAL
		qjs
)

# What a game needs to compile against the engine : the engine headers
# (<Lynx.h>, <core/...>) and the third-party headers they use (<hrl/hrl.h>...).
target_include_directories(Lynx INTERFACE
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third-party>"
)

target_compile_features(Lynx INTERFACE cxx_std_20)


## Lynx Editor (engine executable : opens a game project, see src/editor/README.md)

file(GLOB ImGUI_SOURCE
		"${CMAKE_SOURCE_DIR}/third-party/imgui/*.cpp"
)

add_executable(LynxEditor src/icon.rc
		${ImGUI_SOURCE}
		third-party/imgui_textedit/TextEditor.cpp
		third-party/glad/glad.c

		src/host/GameProject.cpp

		src/editor/EditorMain.cpp
		src/editor/ProjectBrowser.cpp
		src/editor/InputSettingsEditor.cpp
		src/editor/GameBuild.cpp
		src/editor/StartupBuild.cpp

		src/editor/commands/CommandRegistry.cpp
		src/editor/commands/CommandServer.cpp
		src/editor/commands/CommandUtils.cpp
		src/editor/commands/CommandsWindow.cpp
		src/editor/commands/ScriptRunner.cpp
)

target_link_libraries(LynxEditor PRIVATE
		${HRL_LIB}
		${GLFW_LIB}
		Lynx
		opengl32
		ws2_32      # command server (Python scripts / MCP)
		winhttp     # local Ollama chat API
		shell32
)

# Python tools next to the editor : lynx_editor module + MCP server (see python/README.md)
add_custom_command(TARGET LynxEditor POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_CURRENT_SOURCE_DIR}/python"
				"$<TARGET_FILE_DIR:LynxEditor>/python"
		COMMENT "Copying the Python tools (lynx_editor, lynx_mcp.py)"
)



## Lynx Runtime (engine executable : plays a project without the editor)

add_executable(LynxRuntime src/icon.rc
		third-party/glad/glad.c

		src/host/GameProject.cpp
		src/runtime/RuntimeMain.cpp
)

target_link_libraries(LynxRuntime PRIVATE
		${HRL_LIB}
		${GLFW_LIB}
		Lynx
		opengl32
)


## Lynx Launcher (installs the dependencies : Python, CMake, MinGW, Git, Ollama + models, see Launcher/README.md)
# Independent from lynx.dll : it must start on a computer where nothing is installed yet.

add_executable(LynxLauncher src/icon.rc
		${ImGUI_SOURCE}

		Launcher/main.cpp
		Launcher/Launcher.cpp
		Launcher/Dependencies.cpp
		Launcher/Models.cpp
		Launcher/Process.cpp
)

target_link_libraries(LynxLauncher PRIVATE
		${GLFW_LIB}
		opengl32
		gdi32
		shell32     # ShellExecute (links to the websites)
		advapi32    # registry (PATH refresh after an install)
)

# Static libstdc++ / libgcc and no console : the launcher runs before MinGW is installed.
target_link_options(LynxLauncher PRIVATE -static -mwindows)


## Package for the game projects (find_package(Lynx), see cmake/LynxConfig.cmake.in)
# A game project (ex : Blocky) can be anywhere on the computer : configuring
# the engine writes its build folder in the CMake package registry
# (Windows : HKEY_CURRENT_USER\Software\Kitware\CMake\Packages\Lynx).

export(TARGETS Lynx
		NAMESPACE Lynx::
		FILE "${CMAKE_BINARY_DIR}/LynxTargets.cmake"
)

configure_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/cmake/LynxConfig.cmake.in"
		"${CMAKE_BINARY_DIR}/LynxConfig.cmake"
		@ONLY
)

set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)
export(PACKAGE Lynx)
