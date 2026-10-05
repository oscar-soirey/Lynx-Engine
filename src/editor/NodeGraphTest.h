#pragma once

// =============================================================================
// Node graph (test window)
// -----------------------------------------------------------------------------
// Small ImNodes graph to try the node editor with the Lynx pixel style
// (third-party/imgui_node, patched like ImGui : see imnodes_pixel.patch).
// Toolbar > Windows > Node Graph (test).
//
// The graph computes numbers live : Time -> Sin -> Multiply -> Output (with a
// plot). Right click on the canvas : add a node ; drag from a pin to another :
// link ; select + Delete : remove ; middle mouse (or Alt + left) : pan.
// =============================================================================

namespace lynx::editor::node_graph_test
{
	void Draw(bool* open);

	// The window had the focus at the last frame (its Delete key is not the
	// editor's "delete the selected actor").
	bool HasFocus();

	// With ImGui (ShutdownImGui) : frees the ImNodes context.
	void Shutdown();
}
