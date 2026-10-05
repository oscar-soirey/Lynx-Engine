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

namespace lynx::editor::commands_window
{
	// Once per frame (script end, log). Call even when the window is closed.
	void Update();
	void Shutdown();

	void Draw(bool* open);

	// Next Draw : the window comes to the front on the Lynxie tab
	// (the caller opens the window itself).
	void ShowLynxie();
}
