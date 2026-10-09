# =============================================================================
# Copies the files of the engine SDK in <build>/sdk (target LynxSDK, at each build).
#   cmake -DSOURCE_DIR=<engine sources> -DSDK_DIR=<build>/sdk -DENGINE_IMPLIB=<libLynx.dll.a> -P MakeSdk.cmake
# The cmake/ part (LynxConfig.cmake...) is generated when the engine is configured.
# file(COPY) only copies the files that changed : fast after the first build.
# =============================================================================

foreach(VAR SOURCE_DIR SDK_DIR ENGINE_IMPLIB)
	if(NOT ${VAR})
		message(FATAL_ERROR "MakeSdk.cmake : ${VAR} is not set")
	endif()
endforeach()

set(HEADER_PATTERNS PATTERN "*.h" PATTERN "*.hh" PATTERN "*.hpp" PATTERN "*.inl")

# Engine headers : <Lynx.h>, <core/...>, <gameplay/...>...
file(COPY "${SOURCE_DIR}/src/" DESTINATION "${SDK_DIR}/src"
	FILES_MATCHING ${HEADER_PATTERNS})

# Third-party headers used by the engine headers (<hrl/hrl.h>, <tracy/Tracy.hpp>,
# <json/json.hpp>, <imgui/imgui.h>...)
file(COPY "${SOURCE_DIR}/third-party/" DESTINATION "${SDK_DIR}/third-party"
	FILES_MATCHING ${HEADER_PATTERNS}
	PATTERN "CMakeFiles" EXCLUDE
	PATTERN ".git" EXCLUDE)

# Sources compiled by the editor modules of the plugins (LynxPlugin.cmake)
file(GLOB IMGUI_SOURCES "${SOURCE_DIR}/third-party/imgui/*.cpp")
file(COPY ${IMGUI_SOURCES} DESTINATION "${SDK_DIR}/third-party/imgui")
file(GLOB IMNODES_SOURCES "${SOURCE_DIR}/third-party/imgui_node/*.cpp")
file(COPY ${IMNODES_SOURCES} DESTINATION "${SDK_DIR}/third-party/imgui_node")

# Import libraries
file(COPY "${SOURCE_DIR}/third-party/hrl/lib/libhrldll.dll.a" DESTINATION "${SDK_DIR}/third-party/hrl/lib")
file(COPY "${ENGINE_IMPLIB}" DESTINATION "${SDK_DIR}/lib")
