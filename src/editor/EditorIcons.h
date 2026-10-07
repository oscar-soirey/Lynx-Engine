#pragma once

// =============================================================================
// Editor icons
// -----------------------------------------------------------------------------
// 39 pixel-art icons (16x16) in one atlas embedded in the editor
// (resources/editor_icons.png -> editor_icons_png.inl). An editor_icons.png
// next to the editor executable is used instead when present (same layout :
// 8 icons per row, in the order of Icon).
//
// The glyphs are white : they are drawn tinted with the text color of the
// theme (or any color : Play in green, Stop in red...).
// Created on first use (needs the HRL renderer : inside an editor frame).
// =============================================================================

#include <imgui/imgui.h>

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

	// tint : w = 0 -> text color of the current style.
	constexpr ImVec4 kThemeTint{ 0.f, 0.f, 0.f, 0.f };

	// Icon in the layout (a square of `size` pixels ; default : text height).
	void Draw(Icon icon, float size = 0.f, ImVec4 tint = kThemeTint);

	// Icon drawn at a position (no layout).
	void DrawAt(ImDrawList* draw_list, Icon icon, const ImVec2& min, float size, ImU32 tint);

	// Toolbar button : the icon only (tooltip by the caller). `active` : drawn
	// pressed (current gizmo mode...).
	bool Button(const char* id, Icon icon, ImVec4 tint = kThemeTint, bool active = false);

	// Button with the icon then a label ("[>] Play"), same height as a text button.
	bool ButtonWithLabel(const char* label, Icon icon, ImVec4 tint = kThemeTint, bool active = false);

	// Icon + text on one line (menus, lists, window headers).
	void Label(Icon icon, const char* text, ImVec4 tint = kThemeTint);

	// Checkbox with an icon before its label (Windows menu).
	bool Checkbox(const char* label, Icon icon, bool* value);

	// File icon from an extension (".js" -> FileCode, ".png" -> FileImage...).
	Icon ForFile(const char* path, bool is_directory);
}
