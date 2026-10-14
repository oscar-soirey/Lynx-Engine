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

#include <cstdint>
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

	// The lines, for the Console panel. kind : 0 log, 1 warning, 2 error.
	struct ConsoleLine
	{
		std::string text;
		int kind = 0;
		uint64_t sequence = 0;
	};

	// Number of the last line written (lines are numbered 1, 2, 3...).
	uint64_t LastSequence();
	// The lines written after `after` (at most the last `max_lines` of them).
	void GetSince(uint64_t after, std::vector<ConsoleLine>& out, size_t max_lines = 20000);
	// Every line kept (Copy, Fix with Lynxie).
	std::vector<std::string> AllText();
	void Clear();

	// "On screen" (the new lines over the scene) and "errors only" (saved in editor_console.txt).
	bool OnScreen();
	bool OnScreenErrorsOnly();
	void SetOnScreen(bool on, bool errors_only);

	// "Fix with Lynxie" on the error lines : the editor gives what to do with the
	// request (prompt for the AI, short text for the chat). Not set : no button.
	using FixHandler = std::function<void(const std::string& prompt, const std::string& display)>;
	void SetFixHandler(FixHandler handler);
	bool CanFix();
	// Sends the error at `error_index` of `lines` to the fix handler. false : no handler / no error there.
	bool AskFix(const std::vector<std::string>& lines, size_t error_index);

	// Request for Lynxie about the error at this line of the Console (error +
	// stack + file / line) ; "" if there is no error there. Exposed for the tests.
	std::string BuildFixPrompt(const std::vector<std::string>& lines, size_t error_index, std::string* display = nullptr);
}
