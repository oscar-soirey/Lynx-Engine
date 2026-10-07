# Plugin Mesh3D (shipped with the engine) : <editor>/plugins/Mesh3D/
lynx_add_plugin(Mesh3D
	RUNTIME
		Source/Mesh3DRuntime.cpp
	OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins/Mesh3D/bin"
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/plugin.json" "${CMAKE_BINARY_DIR}/plugins/Mesh3D/plugin.json" COPYONLY)
