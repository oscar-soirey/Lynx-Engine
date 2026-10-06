#pragma once

// =============================================================================
// Visual JSON editor
// -----------------------------------------------------------------------------
// The "Editor" view of a .json file in a script editor window (the corner of
// the toolbar switches between "Editor" and "Raw"). Same look as the Input
// Settings window :
//   - objects / arrays : tree rows that open, "+" adds a field, "X" deletes ;
//   - keys : editable text (red when empty or duplicated) ;
//   - values : checkbox, number (drag or type), text, color picker for the
//     colors ([r, g, b(, a)] between 0 and 1, "#rrggbb") ;
//   - the type badge (abc, 123, on/off, { }, [ ], null) changes the type ;
//   - right click on a row : duplicate, move up / down, copy / paste JSON.
// The document stays in the file order (ordered_json).
// =============================================================================

#include <json/json.hpp>

#include <string>

namespace lynx::editor::json_editor
{
	using Json = nlohmann::ordered_json;

	/** What the view keeps from one frame to the next (one per window). */
	struct State
	{
		char filter[128] = {};       // keys / values shown (empty : all)
		int open_all = 0;            // 1 : expand everything this frame, -1 : collapse
		std::string clipboard_error; // "Paste JSON" with a clipboard that is not JSON
		std::string key_buffer;      // key being typed (applied when the field is left)
		bool pending = false;        // edited, waiting for the widget to be released
	};

	/** Text -> document. Accepts // and / * * / comments (`has_comments` : the text had some). */
	bool Parse(const std::string& text, Json& out, std::string& error, bool* has_comments = nullptr);

	/** Document -> text, with the indentation of `like` (the previous text of the file). */
	std::string Dump(const Json& doc, const std::string& like);

	/** Draws the document. true : it changed (an edit was finished : save / undo point). */
	bool Draw(Json& doc, State& state);

	/** Number of values in the document (status bar). */
	int CountValues(const Json& doc);
}
