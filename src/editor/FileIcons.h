#pragma once

// =============================================================================
// File icons of the Content Browser
// -----------------------------------------------------------------------------
// 18 colored pixel-art icons (32x32, drawn at 128x128 in the atlas) :
// resources/file_icons.png (made by resources/file_icons_gen.py), embedded in
// the editor as ui/icons/file_icons.png (8 icons per row, in the order of Kind).
// In the interface files : <FileIcon Kind="Folder" Size="32"/> (raven/RavenUi.h).
// =============================================================================

#include <filesystem>


namespace lynx::editor::file_icons
{
	enum class Kind
	{
		Folder,
		File,
		Text,
		Script,         // .js
		Python,
		Cpp,
		Data,           // .json
		Level,          // .level, .xml, .hrlv
		Image,
		Sound,
		Font,
		Widget,
		AnimGraph,
		BehaviorTree,
		Voxels,         // voxels.json
		Config,         // .ini, .cfg...
		Code,           // other code (.glsl, .lua...)
		Markup,         // .html, .svg...
		Count
	};

	Kind ForFile(const std::filesystem::path& path, bool is_directory);

	/** Short name of the kind ("Folder", "JavaScript"...) : tooltips. */
	const char* Describe(Kind kind);
}
