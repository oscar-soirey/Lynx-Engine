#pragma once

// =============================================================================
// Selectable text (Lynxie's conversation)
// -----------------------------------------------------------------------------
// Text drawn with the draw list (Markdown, wrapped messages) is not an ImGui
// item : it cannot be selected. Between Begin and End, every piece of text
// drawn is recorded (Record) ; End then handles the mouse like a text view :
//
//   drag : select (across messages)   double-click : a word
//   Ctrl+C : copy   Ctrl+A : everything   right click : Copy / Select all
//
// The text keeps its look : the selection is a translucent band over it.
// Outside Begin / End, Record does nothing.
// =============================================================================

#include <imgui/imgui.h>

namespace lynx::editor::text_selection
{
	/** In the window (child) that shows the text, before drawing it. */
	void Begin(const char* id);

	/** A piece of text drawn at `pos` (top left), in reading order. */
	void Record(ImFont* font, float size, const ImVec2& pos, const char* begin, const char* end = nullptr);

	/** The last ImGui item was this text on one line (TextUnformatted...). */
	void RecordLastItem(const char* begin, const char* end = nullptr);

	/** Wrapped text (like TextWrapped / TextUnformatted with a wrap), recorded. `color` 0 : text color. */
	void TextWrapped(const char* begin, const char* end = nullptr, ImU32 color = 0);

	/** After the text : mouse, highlight, copy. */
	void End();

	/** The selected text ("" : none). */
	const char* GetSelectedText();
}
