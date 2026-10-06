#pragma once

// =============================================================================
// Script editors
// -----------------------------------------------------------------------------
// One window per open file (several at once), docked in the middle of the
// editor next to the Viewport. Opened by a double-click in the Content Browser
// (.js, .py, .json, .xml, .txt, .md, .glsl, .cpp, .h...).
//
// - Toolbar : Save (Ctrl+S), Reload from disk, Undo / Redo, Find (Ctrl+F,
//   F3 / Shift+F3), Go to line (Ctrl+G), zoom (Ctrl + wheel, Ctrl+0).
// - JavaScript is checked while typing (lynx::CheckScriptSyntax : syntax and
//   class structure) : the error line is marked, the message shown below.
// - Unsaved file : "*" in the tab ; closing asks Save / Don't save / Cancel.
// - File changed on disk : reloaded if there is no unsaved edit, otherwise a
//   bar offers "Reload" or "Keep my version".
// - Brackets ( [ { are closed automatically.
// - .js : language service (JsLanguage.h) : errors / warnings underlined,
//   completion (as you type, Ctrl+Space), parameters of the call, hover,
//   F12 / Ctrl+click : go to definition, F8 : next problem.
// - Font : JetBrains Mono (fonts/JetBrainsMono-*.ttf).
// =============================================================================

#include <filesystem>

#include <imgui/imgui.h>

namespace lynx::editor::script_editors
{
	// Project root : the JavaScript language service reads assets/ (classes of
	// the other files, input.json actions, widgets...).
	void SetProjectRoot(const std::filesystem::path& root);

	// Text files the editors open.
	bool CanOpen(const std::filesystem::path& path);

	// Opens the file (or brings its window to the front).
	void Open(const std::filesystem::path& path, int line = 0);

	// Draws every open editor. `dock_id` : where new windows are docked
	// (0 : floating).
	void DrawAll(ImGuiID dock_id);

	// A script editor has the keyboard (the editor's own shortcuts wait).
	bool HasKeyboardFocus();

	// A file or folder was renamed / moved (Content Browser) : the editors
	// of the files inside follow it.
	void PathMoved(const std::filesystem::path& from, const std::filesystem::path& to);

	// A file or folder was deleted : the editors of the files inside close
	// (the deleted files are in the trash folder of the project anyway).
	void PathDeleted(const std::filesystem::path& target);

	// Unsaved files (asked before quitting the editor).
	bool HasUnsavedChanges();
	void SaveAll();
}
