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

file(GLOB PHYSFS_SOURCE
	"${CMAKE_SOURCE_DIR}/third-party/physfs/*.c"
	"${CMAKE_SOURCE_DIR}/third-party/physfs/win/*.c"
)

add_library(Lynx SHARED
		#Dependencies
		third-party/inih/ini.c
		third-party/xml/tinyxml2.cpp
		${PHYSFS_SOURCE}

		src/core/Engine.cpp
		src/core/Filesystem.cpp
		src/core/Level.cpp

		src/core/data/IniParser.cpp
		src/core/Private/SystemModule.cpp

		src/gameplay/Object.cpp
		src/gameplay/Actor.cpp
		src/gameplay/Input.cpp
		#src/gameplay/PlayerController.cpp
		src/gameplay/Private/InputManager.cpp
		src/gameplay/CameraShake.cpp
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
		"${CMAKE_CURRENT_SOURCE_DIR}/third-party/hrl/lib/libhrldll.dll.a"
		"${CMAKE_CURRENT_SOURCE_DIR}/third-party/glfw/lib/libglfw3.a"
)


##Game example
add_library(GameExample SHARED
		game/Module.cpp
)

target_link_libraries(GameExample
		"${CMAKE_CURRENT_SOURCE_DIR}/third-party/hrl/lib/libhrldll.dll.a"
		Lynx
)


##ImGui Editor

file(GLOB ImGUI_SOURCE
		"${CMAKE_SOURCE_DIR}/third-party/imgui/*.cpp"
)

add_executable(ImGuiEditor
		${ImGUI_SOURCE}
		third-party/glad/glad.c
		game/main.cpp
)

target_link_libraries(ImGuiEditor PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/third-party/hrl/lib/libhrldll.dll.a"
		"${CMAKE_CURRENT_SOURCE_DIR}/third-party/glfw/lib/libglfw3.a"
		Lynx
		opengl32
)



##qt editor

if(false)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KDDockWidgets-Qt6 REQUIRED)

add_executable(LynxEditor
		src/editor/main.cpp
		src/editor/common.cpp
		src/editor/ui/application.cpp
		src/editor/ui/editor_window.cpp
		src/editor/ui/title_bar.cpp
		src/editor/widgets/viewport.cpp
		src/editor/tabs/window_tab.cpp
		src/editor/tabs/main_tab.cpp
		src/editor/window/main_window.cpp
		src/editor/window/editor_settings.cpp
		src/editor/window/setting_sections/general.cpp
		src/editor/utils/editor_dispatcher.cpp
		src/editor/window/loading_window.cpp
		src/editor/widgets/content_browser.cpp
		src/editor/tabs/text_editor/text_editor_tab.cpp
		src/editor/tabs/text_editor/code_editor.cpp
		src/editor/tabs/text_editor/syntax_highlighter.cpp
		src/editor/tabs/text_editor/lua_syntax_highlighter.cpp
		src/icon.rc
)
set_target_properties(LynxEditor PROPERTIES
		ENABLE_EXPORTS TRUE
)
target_compile_definitions(LynxEditor PRIVATE
		EDITOR_BUILD
)

target_link_libraries(LynxEditor
		PRIVATE
		Qt6::Core
		Qt6::Gui
		Qt6::Widgets
		KDAB::kddockwidgets
		Lynx
)

endif()