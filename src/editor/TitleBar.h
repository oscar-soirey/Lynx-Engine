#pragma once

// =============================================================================
// Custom title bar of the editor window (Windows)
// -----------------------------------------------------------------------------
// The window keeps its native frame styles (resize, Aero snap, animations,
// shadow) but the frame and the system title bar are removed : the editor
// draws its own bar at the top (Lynxie, the title in a tab in the middle,
// minimize / maximize / close).
//
//   - drag the bar : moves the window (double-click : maximize / restore,
//     right click : system menu) ; drag against a screen edge : snap.
//   - the edges and corners resize the window (not when maximized).
//
// The rest of the bar (buttons, and any item registered with AddInteractive)
// stays to ImGui. Other platforms : Install returns false and the native
// title bar is kept (Draw does nothing).
// =============================================================================

#include <functional>
#include <string>

#include <imgui/imgui.h>

struct GLFWwindow;

namespace lynx::editor::title_bar
{
	/** After glfwCreateWindow. false : not available (the native bar stays). */
	bool Install(GLFWwindow* window);

	/** The custom bar is used. */
	bool Active();

	/**
	 * The bar, as a side bar at the top of the main viewport : call it every
	 * frame before the other side bars (toolbar). `on_close` : the close button,
	 * `on_lynxie` : the Lynxie logo on the left.
	 *
	 *   [Lynxie]            \  Lynx - Project  /            [_] [#] [X]
	 */
	void Draw(const std::string& title, const std::function<void()>& on_close,
	          const std::function<void()>& on_lynxie = {});

	/** Height of the bar (pixels). */
	float Height();

	/** Draws the bar even without the Windows frame (headless previews / tests). */
	void ForceDraw(bool force);

	/** Windows hit test of a client point (tests) : 0 client, 1 caption (drag). */
	int HitTestBar(float x, float y);

	/** A rect of the bar handled by ImGui (not a drag area) for this frame. */
	void AddInteractive(const ImVec2& min, const ImVec2& max);
}
