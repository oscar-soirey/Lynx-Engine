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
		src/core/PostProcess.cpp
		src/core/Plugins.cpp
		src/core/VoxelPhysics.cpp
		src/core/VoxelEdit.cpp
		src/core/RessourceManager.cpp
		src/core/Voxels.cpp
		src/core/Profiler.cpp

		src/core/data/IniParser.cpp
		src/core/Private/SystemModule.cpp

		src/gameplay/Object.cpp
		src/gameplay/Actor.cpp
		src/gameplay/Input.cpp
		src/gameplay/PlayerController.cpp
		src/gameplay/Private/InputManager.cpp
		src/gameplay/CameraShake.cpp
		src/gameplay/AnimationSystem.cpp
		src/gameplay/AIBehavior.cpp
		src/gameplay/Components.cpp
		src/gameplay/SpriteComponents.cpp
		src/gameplay/CameraComponent.cpp
		src/gameplay/BoxColliderComponent.cpp
		src/gameplay/PhysicsQueries.cpp
		src/gameplay/SoundSourceComponent.cpp
		src/gameplay/LightComponent.cpp
		src/gameplay/EngineActors.cpp
		src/gameplay/Humanoid.cpp
		src/gameplay/Interface.cpp
		src/gameplay/AnimGraph.cpp
		src/gameplay/BehaviorTree.cpp
		src/scripting/AIBindings.cpp

		src/audio/AudioDecoder.cpp
		src/audio/Audio2D.cpp
		src/audio/AudioCommon.cpp
		src/audio/AudioListener.cpp
		src/audio/AudioSource.cpp

		src/widgets/Widget.cpp
		src/widgets/Panels.cpp
		src/widgets/CommonWidgets.cpp
		src/widgets/UserWidget.cpp
		src/widgets/WidgetSystem.cpp

		src/scripting/ScriptSystem.cpp
		src/scripting/ComponentBindings.cpp
		src/scripting/GameplayBindings.cpp
		src/scripting/QueryBindings.cpp
)

target_compile_definitions(Lynx PRIVATE
	LYNX_BUILD_DLL
)

target_compile_definitions(Lynx
		PRIVATE
		LYNX_VERSION_YEAR=26
		LYNX_VERSION_MAJOR=0
		LYNX_VERSION_MINOR=6
)

target_link_libraries(Lynx PRIVATE
		${HRL_LIB}
		${GLFW_LIB}
		OpenAL::OpenAL
		qjs
)

# Profiler (src/core/Profiler.h) : the Tracy client lives in lynx.dll, "on demand"
# (no cost until tracy-profiler.exe connects). The editor, the runtime and the game
# DLLs use the same client (TRACY_IMPORTS) : their zones show up in Tracy too.
# The built-in Profiler window of the editor works with or without Tracy.
option(LYNX_PROFILER_TRACY "Tracy profiler client in the engine (on demand)" ON)

if(LYNX_PROFILER_TRACY)
	target_sources(Lynx PRIVATE third-party/tracy/TracyClient.cpp)
	target_compile_definitions(Lynx
			PUBLIC    TRACY_ENABLE TRACY_ON_DEMAND
			PRIVATE   TRACY_EXPORTS
			INTERFACE TRACY_IMPORTS
	)
	target_link_libraries(Lynx PRIVATE ws2_32 dbghelp secur32 advapi32 user32)
	# <tracy/Tracy.hpp> for the engine, the editor and the games
	target_include_directories(Lynx PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third-party/tracy>")
endif()

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

add_executable(LynxEditor src/editor_icon.rc
		${ImGUI_SOURCE}
		third-party/imgui_textedit/TextEditor.cpp
		third-party/imgui_node/imnodes.cpp
		third-party/glad/glad.c

		src/host/GameProject.cpp

		src/editor/EditorMain.cpp
		src/editor/ProjectBrowser.cpp
		src/editor/InputSettingsEditor.cpp
		src/editor/GameBuild.cpp
		src/editor/StartupBuild.cpp
		src/editor/OutputConsole.cpp
		src/editor/EditorOptions.cpp
		src/editor/ExternalIde.cpp
		src/editor/ShellIntegration.cpp
		src/editor/EditorPlugins.cpp
		src/editor/ProjectTemplates.cpp
		src/editor/LynxieIcon.cpp
		src/editor/EditorIcons.cpp
		src/editor/PropertyWidgets.cpp
		src/editor/ProfilerWindow.cpp
		src/editor/ScriptEditors.cpp
		src/editor/MarkdownText.cpp
		src/editor/EditorFonts.cpp
		src/editor/ShipGame.cpp
		src/editor/SplashScreen.cpp
		src/editor/WidgetEditor.cpp
		src/editor/GraphEditors.cpp
		src/editor/VoxelTypeEditor.cpp
		src/editor/TitleBar.cpp
		src/editor/FileIcons.cpp
		src/editor/SpriteVoxelizer.cpp
		src/editor/TextSelection.cpp
		src/editor/WindowIcon.cpp
		src/editor/JsonEditor.cpp
		src/editor/JsLanguage.cpp
		src/editor/ProjectInfo.cpp

		src/editor/commands/CommandRegistry.cpp
		src/editor/commands/CommandServer.cpp
		src/editor/commands/CommandUtils.cpp
		src/editor/commands/CommandsWindow.cpp
		src/editor/commands/ScriptRunner.cpp
		src/editor/commands/FileEdits.cpp
)

# imnodes includes <imgui.h> / <imgui_internal.h> directly.
target_include_directories(LynxEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third-party/imgui")

target_link_libraries(LynxEditor PRIVATE
		${HRL_LIB}
		${GLFW_LIB}
		Lynx
		opengl32
		ws2_32      # command server (Python scripts / MCP)
		winhttp     # local Ollama chat API
		shell32
		ole32       # Ship Game : folder picker
		user32 gdi32  # Open with : icons of the code editors
		advapi32      # registry : "Open with Lynx" in the Explorer menu
)

# No Windows console : stdout / stderr are shown in the editor "Console" window
# (src/editor/OutputConsole.cpp) and written to editor_output.log.
target_link_options(LynxEditor PRIVATE -mwindows)

# Python tools next to the editor : lynx_editor module + MCP server (see python/README.md)
add_custom_command(TARGET LynxEditor POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_CURRENT_SOURCE_DIR}/python"
				"$<TARGET_FILE_DIR:LynxEditor>/python"
		COMMENT "Copying the Python tools (lynx_editor, lynx_mcp.py)"
)

# Fonts next to the editor : fonts/VCR-OSD-MONO.ttf (editor and Lynxie),
# JetBrainsMono (code). See src/editor/EditorFonts.h.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fonts")
	add_custom_command(TARGET LynxEditor POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory
					"${CMAKE_CURRENT_SOURCE_DIR}/fonts"
					"$<TARGET_FILE_DIR:LynxEditor>/fonts"
			COMMENT "Copying the editor fonts"
	)
endif()

# Project templates next to the editor : "New project" in the project browser
# (see src/editor/ProjectTemplates.h). Edit or add templates in templates/.
# The copy is emptied first : a template removed or renamed in templates/ must
# not stay next to the editor.
# A target that runs at EVERY build (not a POST_BUILD of the editor, which
# only runs when the editor is relinked : a template added or edited without
# touching the C++ never reached the editor).
add_custom_target(LynxTemplates ALL
		COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_CURRENT_BINARY_DIR}/templates"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_CURRENT_SOURCE_DIR}/templates"
				"${CMAKE_CURRENT_BINARY_DIR}/templates"
		COMMENT "Copying the project templates"
		VERBATIM
)
add_dependencies(LynxEditor LynxTemplates)



## Generic game DLL : projects without C++ (see src/host/ScriptGame.cpp)
# <editor>/scriptgame/LynxScriptGame.dll (a sub folder : never taken for the
# game DLL of another project, nor copied by Ship Game unless needed).

add_library(LynxScriptGame SHARED src/host/ScriptGame.cpp)
target_link_libraries(LynxScriptGame PRIVATE Lynx)
set_target_properties(LynxScriptGame PROPERTIES
		PREFIX ""
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/scriptgame")
add_dependencies(LynxEditor LynxScriptGame)


## Plugins shipped with the engine : <editor>/plugins/<Name>/ (plugin.json + bin/)
# Off by default in a project (Plugins window, <project>/plugins.json).

set(LYNX_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(LYNX_ENGINE_TARGET Lynx)
set(LYNX_HRL_LIB "${HRL_LIB}")
include(cmake/LynxPlugin.cmake)

foreach(LYNX_PLUGIN_NAME Dialogue CineCamera PixelSprite Mesh3D)
	add_subdirectory(plugins/${LYNX_PLUGIN_NAME})
	add_dependencies(LynxEditor ${LYNX_PLUGIN_NAME})
	# Editor module : only for the plugins that have one (Mesh3D : runtime only).
	if(TARGET ${LYNX_PLUGIN_NAME}.Editor)
		add_dependencies(LynxEditor ${LYNX_PLUGIN_NAME}.Editor)
	endif()
endforeach()

# Game projects : include("${Lynx_DIR}/LynxPlugin.cmake") (after find_package(Lynx)).
file(WRITE "${CMAKE_BINARY_DIR}/LynxPlugin.cmake"
		"set(LYNX_ROOT_DIR \"${CMAKE_CURRENT_SOURCE_DIR}\")\n"
		"set(LYNX_HRL_LIB \"${HRL_LIB}\")\n"
		"include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/LynxPlugin.cmake\")\n")


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


## Lynx launcher (installs the dependencies : Python, CMake, MinGW, Git, Ollama + models,
## then the engine versions from the GitHub releases, see launcher/README.md)
# Independent from lynx.dll : it must start on a computer where nothing is installed yet.

add_executable(LynxLauncher src/editor_icon.rc
		${ImGUI_SOURCE}

		launcher/main.cpp
		launcher/Launcher.cpp
		launcher/Dependencies.cpp
		launcher/Models.cpp
		launcher/Engine.cpp
		launcher/PluginStore.cpp
		launcher/Process.cpp
		src/editor/MarkdownText.cpp    # notes de version / descriptions en Markdown
		launcher/MarkdownShims.cpp     # polices et sélection de l'éditeur remplacées
)

target_link_libraries(LynxLauncher PRIVATE
		${GLFW_LIB}
		opengl32
		gdi32
		shell32     # ShellExecute (links to the websites)
		advapi32    # registry (PATH refresh after an install)
		ole32       # folder picker (Plugins > Import a folder)
		uuid
)

# Static libstdc++ / libgcc and no console : the launcher runs before MinGW is installed.
target_link_options(LynxLauncher PRIVATE -static -mwindows)


## Lynx updater (starts BEFORE the launcher, same folder : checks the launcher releases
## on GitHub (tags "launcher*"), updates LynxLauncher.exe if needed, then starts it)
# Small Win32 program (no ImGui) : a progress window is shown only while an update downloads.
# The installed launcher version is the tag written in launcher.version next to the exe.
# Bump LYNX_LAUNCHER_TAG with each launcher release (-DLYNX_LAUNCHER_TAG=launcher-v-0.0.3).

set(LYNX_LAUNCHER_TAG "launcher-v-0.1.1" CACHE STRING "GitHub tag of this launcher build (written in launcher.version)")

add_executable(LynxUpdater src/editor_icon.rc
		launcher/updater/main.cpp
		launcher/updater/Updater.cpp
		launcher/Process.cpp
)

target_link_libraries(LynxUpdater PRIVATE
		comctl32    # progress bar
		shell32
		advapi32    # registry (Process::RefreshPath)
)

target_link_options(LynxUpdater PRIVATE -static -mwindows)

# launcher.version next to LynxLauncher.exe (and LynxUpdater.exe : same build folder)
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:LynxLauncher>/launcher.version" CONTENT "${LYNX_LAUNCHER_TAG}\n")

# Building the launcher also builds the updater
add_dependencies(LynxLauncher LynxUpdater)


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
