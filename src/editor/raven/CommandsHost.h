#pragma once

// =============================================================================
// Lynx Editor (Raven) : the "Commands" window (Scripts, Lynxie, Console,
// Reference, AI / MCP) until it is ported to Raven.
// -----------------------------------------------------------------------------
// commands/CommandsWindow.cpp is ImGui code. This module gives it a native
// window of its own (GLFW + OpenGL 3.3 + Dear ImGui), separate from the Raven
// window : created the first time it is shown, hidden when closed. Its OpenGL
// context is made current only while it draws ; the context of the editor
// (gl::Init, used by HRL) is current again afterwards.
// =============================================================================

namespace lynx::editor::commands_host
{
	/** Once per frame, after commands_window::Update. `open` : the window is shown
	 *  (set to false when the user closes it). */
	void Frame(bool& open);

	/** Before commands_window::Shutdown. */
	void Shutdown();
}
