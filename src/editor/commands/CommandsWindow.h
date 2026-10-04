#pragma once

// =============================================================================
// "Commands" window (editor)
// -----------------------------------------------------------------------------
//   Scripts    : the .py files of <project>/commands/, Run / Stop, output
//   Console    : type a command  (actor.spawn {"class": "Enemy"})
//   Reference  : every command with its parameters
//   AI / MCP   : connection info and the MCP configuration to copy
// =============================================================================

namespace lynx::editor::commands_window
{
	// Once per frame (script end, log). Call even when the window is closed.
	void Update();

	void Draw(bool* open);
}
