#pragma once

// =============================================================================
// "Commands" window (editor)
// -----------------------------------------------------------------------------
//   Scripts    : the .py files of <project>/commands/, Run / Stop, output
//   Lynxie     : the local AI assistant (Ollama)
//   Console    : type a command  (actor.spawn {"class": "Enemy"})
//   Reference  : every command with its parameters
//   AI / MCP   : connection info and the MCP configuration to copy
// =============================================================================

#include <string>

namespace lynx::editor::commands_window
{
	// Once per frame (script end, log). Call even when the window is closed.
	void Update();
	void Shutdown();

	void Draw(bool* open);

	// Next Draw : the window comes to the front on the Lynxie tab
	// (the caller opens the window itself).
	// Brings the Commands window to the front on the Lynxie tab (for `frames` frames).
	void ShowLynxie(int frames = 1);

	// Sends a request to Lynxie as if typed in the chat (`display` : the text shown
	// in the conversation instead, "" : the prompt). Waits until Lynxie is free and
	// a model is known ; brings the Lynxie tab to the front (open the window yourself).
	void AskLynxie(const std::string& prompt, const std::string& display = "");

	// Lynxie is answering, running a script, or has a request waiting.
	bool IsLynxieBusy();
}
