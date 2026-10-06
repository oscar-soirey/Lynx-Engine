# Plugin CineCamera (shipped with the engine) : <editor>/plugins/CineCamera/
lynx_add_plugin(CineCamera
	RUNTIME
		Source/SequenceData.cpp
		Source/CineCameraRuntime.cpp
	EDITOR
		Source/SequenceData.cpp
		Source/CineCameraEditor.cpp
	OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins/CineCamera/bin"
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/plugin.json" "${CMAKE_BINARY_DIR}/plugins/CineCamera/plugin.json" COPYONLY)
