#pragma once

// =============================================================================
// Lynxie icon (the assistant of the editor)
// -----------------------------------------------------------------------------
// Pixel art embedded in the editor (resources/lynxie.png -> lynxie_png.inl).
// A lynxie.png next to the editor executable is used instead when present.
// The image is white line art : it is drawn tinted with the text color of the
// theme (black on the light pixel theme, white on a dark one).
// Created on first use (needs the HRL renderer, so: inside an editor frame).
// =============================================================================

#include <imgui/imgui.h>

namespace lynx::editor::lynxie_icon
{
	// false while the texture is not available (not created yet, decode error...).
	bool Ready();

	// Draws the icon `height` pixels high (width from the image ratio).
	// tint : w = 0 -> text color of the current style.
	void Draw(float height, ImVec4 tint = ImVec4(0.f, 0.f, 0.f, 0.f));

	// Same, at a given position of a draw list (no layout).
	void DrawAt(ImDrawList* draw_list, const ImVec2& min, float height, ImU32 tint);

	// Toolbar button with the icon (a text button while the texture loads).
	bool Button(const char* id, float height);

	// Width for a given height.
	float WidthFor(float height);
}
