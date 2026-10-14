# lynx_embed_files(<output header> <root folder> <file>...)
#
# Writes a C++ header holding the bytes of the files, so that the editor needs
# (an entry "<file>=<name>" names the file "ui/<name>")
# nothing beside its executable (src/editor/raven/RavenUi.cpp registers them
# with Rfile_register_memory). Each file is named by its path relative to
# <root folder>, prefixed with "ui/" : ui/editor.xml, ui/icons/editor_icons.png...
#
#   static const LynxEmbeddedFile kLynxEmbeddedFiles[] = { { "ui/editor.xml", data, size }, ... };
#
# The header is written again (and the editor rebuilt) when one of the files
# changes : the files are dependencies of the custom command.

function(lynx_embed_files OUTPUT ROOT)
	set(FILES ${ARGN})
	# the files themselves are the dependencies ("<file>=<name>" : the file)
	set(DEPENDS_ON "")
	foreach(entry IN LISTS FILES)
		string(REGEX REPLACE "=[^=]+$" "" path "${entry}")
		list(APPEND DEPENDS_ON "${path}")
	endforeach()
	set(SCRIPT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/LynxEmbedFilesRun.cmake")
	# ';' would split the argument : the list goes with '|'
	string(REPLACE ";" "|" FILES_ARG "${FILES}")
	add_custom_command(
		OUTPUT "${OUTPUT}"
		COMMAND ${CMAKE_COMMAND}
			"-DOUTPUT=${OUTPUT}"
			"-DROOT=${ROOT}"
			"-DFILES=${FILES_ARG}"
			-P "${SCRIPT}"
		DEPENDS ${DEPENDS_ON} "${SCRIPT}"
		COMMENT "Embedding the editor interface (${OUTPUT})"
		VERBATIM
	)
endfunction()
