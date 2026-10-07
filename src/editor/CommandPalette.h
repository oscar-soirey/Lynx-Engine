#pragma once

// =============================================================================
// Command palette (Ctrl+P) : one search field to run anything in the editor.
// -----------------------------------------------------------------------------
// The editor gives the items when it opens the palette (commands, windows,
// actors of the level, files of assets/) ; typing filters them (fuzzy : the
// letters in order : "pospro" finds "Post Process", "savlev" "Save level"), arrows +
// Enter or a click runs one, Escape closes.
//
// Prefixes : ">" commands and windows only, "@" actors only, "#" files only.
// The last items run come first when the field is empty.
// =============================================================================

#include <functional>
#include <string>
#include <vector>

namespace lynx::editor::command_palette
{
	enum class Kind
	{
		Command,   // ">"
		Window,    // ">"
		Actor,     // "@"
		File,      // "#"
	};

	struct Item
	{
		std::string label;     // searched and shown
		std::string detail;    // shown dimmed on the right (shortcut, path, class...)
		Kind kind = Kind::Command;
		std::function<void()> run;
	};

	/** Opens the palette with these items ; `prefix` : text already typed (">" ...). */
	void Open(std::vector<Item> items, const std::string& prefix = "");

	bool IsOpen();

	/** Every frame (draws nothing when closed). */
	void Draw();

	/**
	 * Score of `query` in `text` (fuzzy, case insensitive) ; < 0 : no match.
	 * Consecutive letters, word starts and an early match score higher.
	 */
	int FuzzyScore(const std::string& query, const std::string& text);
}
