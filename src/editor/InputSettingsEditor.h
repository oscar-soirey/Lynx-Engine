#pragma once

// =============================================================================
// Input Settings window (editor)
// -----------------------------------------------------------------------------
// Edits the project's input.json (project root), like Unreal's Input settings :
//   - Action Mappings : name -> keys        (InputAction)
//   - Axis Mappings   : name -> keys+scale  (InputAxis1D)
// A key is chosen in a list, or by clicking its button then pressing the key
// (keyboard, mouse button, mouse wheel, gamepad button / stick / trigger).
//
// Every change is saved right away and input.json is reloaded in the engine :
// the game uses the new bindings immediately.
// =============================================================================

#include <filesystem>

struct GLFWwindow;

namespace lynx::editor::input_settings
{
	// `file` : the project's input.json (it may not exist yet).
	void Init(GLFWwindow* window, const std::filesystem::path& file);

	// Once per frame, before the editor shortcuts (gamepad capture...).
	void Update();

	// Draws the window. `open` : visibility flag (window close button).
	void Draw(bool* open);

	// GLFW events, from the editor callbacks. true = used by a key capture :
	// the event must not reach the editor / the game.
	bool OnKey(int key, int action);
	bool OnMouseButton(int button, int action);
	bool OnScroll(double yoffset);

	// True while a key is being captured, and until every key / mouse button
	// is released afterwards : the editor shortcuts (Delete, Space, F3...)
	// must ignore the key that was just bound.
	bool BlocksEditorShortcuts();
}
