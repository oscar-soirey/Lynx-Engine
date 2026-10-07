# Plugin PixelSprite (shipped with the engine) : <editor>/plugins/PixelSprite/
lynx_add_plugin(PixelSprite
	RUNTIME
		Source/LSprite.cpp
		Source/PixelSpriteRuntime.cpp
	EDITOR
		Source/LSprite.cpp
		Source/PixelSpriteEditor.cpp
	OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins/PixelSprite/bin"
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/plugin.json" "${CMAKE_BINARY_DIR}/plugins/PixelSprite/plugin.json" COPYONLY)
