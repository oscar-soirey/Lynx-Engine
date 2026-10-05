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

#include <string>

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
}
