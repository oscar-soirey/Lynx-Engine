# Plugin Dialogue (shipped with the engine) : <editor>/plugins/Dialogue/
lynx_add_plugin(Dialogue
	RUNTIME
		Source/DialogueData.cpp
		Source/Story.cpp
		Source/DialogueRuntime.cpp
	EDITOR
		Source/DialogueData.cpp
		Source/DialogueEditor.cpp
	IMNODES
	OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins/Dialogue/bin"
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/plugin.json" "${CMAKE_BINARY_DIR}/plugins/Dialogue/plugin.json" COPYONLY)
