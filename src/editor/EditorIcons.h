#pragma once

// =============================================================================
// Editor icons
// -----------------------------------------------------------------------------
// 39 pixel-art icons (16x16, drawn at 64x64 in the atlas) : resources/
// editor_icons.png, embedded in the editor as ui/icons/editor_icons.png
// (8 icons per row, in the order of Icon). The glyphs are white : the editor
// draws them tinted with the text color of the theme.
//
// In the interface files : <Icon Glyph="Play" Size="16"/> (raven/RavenUi.h).
// =============================================================================

namespace lynx::editor::icons
{
	enum class Icon
	{
		Play,
		Stop,
		Pause,
		Save,
		Undo,
		Translate,
		Rotate,
		Scale,
		Compile,
		Reload,
		Settings,
		Windows,
		Console,
		ContentBrowser,
		Folder,
		File,
		FileCode,
		FileImage,
		FileSound,
		Outliner,
		Details,
		PlaceActors,
		Paint,
		Git,
		Commands,
		Input,
		Camera,
		ColorPick,
		Project,
		Search,
		Clear,
		Copy,
		Profiler,
		Redo,
		Shake,
		World,
		Plugin,
		Dialogue,
		Eye,
		Count
	};

	// File icon from an extension (".js" -> FileCode, ".png" -> FileImage...).
	Icon ForFile(const char* path, bool is_directory);
}
