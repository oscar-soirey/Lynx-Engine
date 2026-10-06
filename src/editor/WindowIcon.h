#pragma once

// =============================================================================
// Window icon
// -----------------------------------------------------------------------------
// Every GLFW window of the editor (project browser, splash, build window,
// editor) gets the same icon :
//   - icon.png (the one of the main window) once it was read, or the
//     icon.png next to the editor executable ;
//   - otherwise, on Windows, the "GLFW_ICON" icon of the executable
//     (src/editor_icon.rc), that GLFW uses by itself.
// =============================================================================

#include <cstdint>
#include <vector>

struct GLFWwindow;

namespace lynx::editor::window_icon
{
	/** Image file bytes (png, jpg...) : the icon of the next windows. false : not an image. */
	bool SetImage(const std::vector<std::uint8_t>& bytes);

	/** Puts the icon on the window (does nothing when there is none). */
	void Apply(GLFWwindow* window);
}
