# =============================================================================
# lynx_add_plugin : builds the modules of a Lynx plugin (see src/core/Plugins.h)
# -----------------------------------------------------------------------------
#   lynx_add_plugin(Dialogue
#       RUNTIME Source/Dialogue.cpp ...          # <Name>.dll : editor AND game
#       EDITOR  Source/DialogueEditor.cpp ...    # <Name>.Editor.dll : editor only (optional)
#       [IMNODES]                                # the editor module uses imnodes (node graphs)
#       [OUTPUT_DIR <dir>]                       # default : <plugin folder>/bin
#   )
#
# In the engine : included by the main CMakeLists.txt.
# In a game project : include("${Lynx_DIR}/LynxPlugin.cmake") after
# find_package(Lynx) (the engine writes that file in its build folder).
#
# LYNX_ROOT_DIR : sources of the engine (src/, third-party/).
# LYNX_ENGINE_TARGET : Lynx (engine tree) or Lynx::Lynx (find_package).
# LYNX_HRL_LIB : import library of HRL (plugins that call HRL directly).
# =============================================================================

if(NOT DEFINED LYNX_ROOT_DIR)
	get_filename_component(LYNX_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

if(NOT DEFINED LYNX_ENGINE_TARGET)
	if(TARGET Lynx)
		set(LYNX_ENGINE_TARGET Lynx)
	else()
		set(LYNX_ENGINE_TARGET Lynx::Lynx)
	endif()
endif()

if(NOT DEFINED LYNX_HRL_LIB)
	set(LYNX_HRL_LIB "${LYNX_ROOT_DIR}/third-party/hrl/lib/libhrldll.dll.a")
endif()

function(lynx_add_plugin NAME)
	cmake_parse_arguments(P "IMNODES" "OUTPUT_DIR" "RUNTIME;EDITOR" ${ARGN})

	if(NOT P_OUTPUT_DIR)
		set(P_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin")
	endif()

	# --- Runtime module -------------------------------------------------------
	add_library(${NAME} SHARED ${P_RUNTIME})
	target_link_libraries(${NAME} PRIVATE ${LYNX_ENGINE_TARGET} ${LYNX_HRL_LIB})
	target_include_directories(${NAME} PRIVATE
			"${LYNX_ROOT_DIR}/src"
			"${LYNX_ROOT_DIR}/third-party"
			"${CMAKE_CURRENT_SOURCE_DIR}/Source")
	target_compile_features(${NAME} PRIVATE cxx_std_20)
	set_target_properties(${NAME} PROPERTIES
			PREFIX ""
			RUNTIME_OUTPUT_DIRECTORY "${P_OUTPUT_DIR}"
			LIBRARY_OUTPUT_DIRECTORY "${P_OUTPUT_DIR}"
			ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
	foreach(CONFIG Debug Release RelWithDebInfo MinSizeRel)
		string(TOUPPER ${CONFIG} CONFIG_UPPER)
		set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${P_OUTPUT_DIR}")
	endforeach()

	# --- Editor module (its own copy of the editor's ImGui) -----------------------
	if(P_EDITOR)
		file(GLOB _LYNX_IMGUI "${LYNX_ROOT_DIR}/third-party/imgui/imgui*.cpp")
		list(FILTER _LYNX_IMGUI EXCLUDE REGEX "imgui_impl_")
		set(_SOURCES ${P_EDITOR} ${_LYNX_IMGUI})
		if(P_IMNODES)
			list(APPEND _SOURCES "${LYNX_ROOT_DIR}/third-party/imgui_node/imnodes.cpp")
		endif()

		add_library(${NAME}.Editor SHARED ${_SOURCES})
		target_link_libraries(${NAME}.Editor PRIVATE ${NAME} ${LYNX_ENGINE_TARGET} ${LYNX_HRL_LIB})
		target_include_directories(${NAME}.Editor PRIVATE
				"${LYNX_ROOT_DIR}/src"
				"${LYNX_ROOT_DIR}/third-party"
				"${LYNX_ROOT_DIR}/third-party/imgui"
				"${LYNX_ROOT_DIR}/third-party/imgui_node"
				"${CMAKE_CURRENT_SOURCE_DIR}/Source")
		target_compile_features(${NAME}.Editor PRIVATE cxx_std_20)
		set_target_properties(${NAME}.Editor PROPERTIES
				PREFIX ""
				RUNTIME_OUTPUT_DIRECTORY "${P_OUTPUT_DIR}"
				LIBRARY_OUTPUT_DIRECTORY "${P_OUTPUT_DIR}"
				ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
		foreach(CONFIG Debug Release RelWithDebInfo MinSizeRel)
			string(TOUPPER ${CONFIG} CONFIG_UPPER)
			set_target_properties(${NAME}.Editor PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${P_OUTPUT_DIR}")
		endforeach()
	endif()
endfunction()
