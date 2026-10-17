#pragma once

// =============================================================================
// "Commands" panel (editor, docked : interface/panels/commands.xml)
// -----------------------------------------------------------------------------
//   Scripts    : the .py files of <project>/commands/, Run / Stop, output
//   Lynxie     : the local AI assistant (Ollama)
//   Console    : type a command  (actor.spawn {"class": "Enemy"})
//   Reference  : every command with its parameters
//   AI / MCP   : connection info and the MCP configuration to copy
// =============================================================================

#include <Raven.h>

#include <string>

namespace lynx::editor::commands_window
{
	// Once per frame (script end, log). Call even when the panel is closed.
	void Update();
	void Shutdown();

	// The panel of the editor (root widget "commands") : binds its widgets.
	void BindPanel(Rid root);

	// Once per frame : the panel follows the state (`shown` : its tab is in front).
	void UpdatePanel(bool shown);

	// true once after ShowLynxie / AskLynxie : the editor shows the panel.
	bool TakeShowRequest();

	// The panel comes to the front on the Lynxie tab (for `frames` frames).
	void ShowLynxie(int frames = 1);

	// Sends a request to Lynxie as if typed in the chat (`display` : the text shown
	// in the conversation instead, "" : the prompt). Waits until Lynxie is free and
	// a model is known ; brings the Lynxie tab to the front.
	void AskLynxie(const std::string& prompt, const std::string& display = "");

	// Lynxie is answering, running a script, or has a request waiting.
	bool IsLynxieBusy();
}
