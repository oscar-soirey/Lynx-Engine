#pragma once

// =============================================================================
// Splash screen (editor)
// -----------------------------------------------------------------------------
// Shown between the project browser and the editor window, while the project
// loads (build check, game DLL, renderer, voxel world, level, editor UI...).
//
// It behaves like a real splash screen :
//   - borderless, not resizable, centered on the primary monitor, cannot be
//     closed (Alt+F4 is ignored) ;
//   - appears with a short fade-in, never shows an empty frame ;
//   - shows the current loading step and a progress bar ;
//   - stays on screen a minimum time (no flash when loading is instant) ;
//   - the editor window is revealed BEHIND it (kept hidden while loading),
//     then the splash fades out over the running editor and is destroyed.
//
// It owns its GLFW window, OpenGL context and ImGui context. Every frame it
// draws saves and restores the current GL / ImGui contexts : it can be
// updated in the middle of the editor startup (HRL, editor ImGui...).
// Everything runs on the main thread : the splash is redrawn at each step.
// =============================================================================

#include <string>

#include "../host/GameProject.h"

namespace lynx::editor::splash
{
	// Opens the splash for `project` (fade-in). Already open : updates the
	// project and shows it again if it was hidden.
	void Open(const host::GameProject& project);

	// Current loading step and overall progress [0, 1] (never goes back).
	// Redraws the splash immediately : the caller is about to block.
	void SetStep(const std::string& text, float progress);

	// Another window takes over (startup build, project browser) : hide the
	// splash meanwhile, then Show() it again.
	void Hide();
	void Show();

	// Loading finished (blocking) : fills the bar, waits for the minimum
	// display time, then keeps the splash above the windows so the editor
	// window can be shown behind it.
	void Complete();

	// Once the editor window is visible : call after each editor frame. The
	// splash fades out after the first frames, then closes itself.
	void Tick();

	// Destroys the splash right away (quit, error, editor closed early).
	void Close();

	bool IsOpen();
}
