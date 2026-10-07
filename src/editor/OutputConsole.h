#pragma once

// =============================================================================
// Editor console (output)
// -----------------------------------------------------------------------------
// The editor has no Windows console (linked with -mwindows). Start() redirects
// stdout and stderr (printf, std::cout, std::cerr : editor, lynx.dll, the game
// DLL...) into a pipe ; a thread reads it and the lines are shown in the
// "Console" window. They are also written to editor_output.log next to the
// editor (still readable after a crash).
//
// Not the Commands > Console tab : that one runs editor commands.
// =============================================================================

#include <functional>
#include <string>
#include <vector>

namespace lynx::editor::output_console
{
	// First thing in main(), before anything prints.
	void Start();

	// Adds a line directly (ex : output of the Python scripts).
	void Write(const std::string& line);

	// Every frame, on the main thread : the new lines are also shown over the
	// scene with HRL_AddScreenMessage (colored by kind : log, warning, error).
	// "On screen" / "Errors only" in the Console window (saved in
	// editor_console.txt in the project).
	void FlushToScreen(unsigned int scene);

	void Draw(bool* open);

	// "Fix with Lynxie" on the error lines : the editor gives what to do with the
	// request (prompt for the AI, short text for the chat). Not set : no button.
	using FixHandler = std::function<void(const std::string& prompt, const std::string& display)>;
	void SetFixHandler(FixHandler handler);

	// Request for Lynxie about the error at this line of the Console (error +
	// stack + file / line) ; "" if there is no error there. Exposed for the tests.
	std::string BuildFixPrompt(const std::vector<std::string>& lines, size_t error_index, std::string* display = nullptr);
}
