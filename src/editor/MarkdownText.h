#pragma once

// =============================================================================
// Markdown text (Lynxie's answers)
// -----------------------------------------------------------------------------
// Draws a small subset of Markdown with ImGui, wrapped to the available width :
//
//   # Heading / ## Heading / ### Heading      bigger, bold
//   **bold**  __bold__  *italic*  _italic_   ~~strike~~   `code`   [text](url)
//   - item / * item / + item                  bullets (indent by 2 spaces)
//   1. item / 1) item                         numbered lists
//   > quote                                   bar on the left, dimmed
//   ---                                       separator
//   | a | b |  tables (the |---| line is skipped)
//   ``` code blocks ```                       background, language, Copy button
//
// Bold is drawn twice with a 1 pixel offset (the pixel font has no bold face),
// italic in a dimmer color. Anything else is shown as plain text.
// =============================================================================

#include <string>

namespace lynx::editor::markdown
{
	void Render(const std::string& text);
}
