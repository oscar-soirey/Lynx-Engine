#pragma once

// =============================================================================
// File icons of the Content Browser
// -----------------------------------------------------------------------------
// 18 colored pixel-art icons (32x32) in one atlas embedded in the editor
// (resources/file_icons.png, made by resources/file_icons_gen.py ->
// file_icons_png.inl). A file_icons.png next to the editor executable is used
// instead when present (same layout : 8 icons per row, in the order of Kind).
// Created on first use (needs the HRL renderer : inside an editor frame).
// =============================================================================

#include <filesystem>

#include <imgui/imgui.h>

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
		Level,          // .xml, .hrlv
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

	/** At a position of a draw list. Sharpest at 32, 64, 96... pixels. */
	void DrawAt(ImDrawList* draw_list, Kind kind, const ImVec2& min, float size, ImU32 tint = IM_COL32_WHITE);

	/** In the layout. */
	void Draw(Kind kind, float size);
}
