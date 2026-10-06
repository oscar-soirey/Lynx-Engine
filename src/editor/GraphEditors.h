#pragma once

// =============================================================================
// Graph editors : Anim Graph (.animgraph) and Behavior Tree (.bt)
// -----------------------------------------------------------------------------
// One window per open file, docked next to the Viewport (like the Widget
// Editor). Content Browser : double-click, or New file > Anim Graph / Behavior Tree.
//
// Anim Graph (state machine of an AnimationSpriteComponent, Unreal-like) :
//   +-------------+------------------------------------+-------------+
//   | Parameters  |  Entry -> Idle <-> Walk -> Jump     |  Details    |
//   | Animations  |  Any State -> Hurt                  |  (state or  |
//   | Blend spaces|  drag a pin to make a transition    |  transition)|
//   +-------------+------------------------------------+-------------+
//
// Behavior Tree (AI component) :
//   +-------------+------------------------------------+-------------+
//   | Blackboard  |  Root -> Selector -> Sequence ...   |  Details    |
//   | (keys)      |  left to right ; children run top   |  (node,     |
//   |             |  to bottom ; decorators / services  |  decorator, |
//   |             |  inside the nodes                   |  service)   |
//   +-------------+------------------------------------+-------------+
//
// - Right click on the canvas : add a node. Right click on a node : its menu
//   (decorators, services, duplicate, delete...). Drag a pin onto empty space :
//   new node already linked.
// - Del : delete the selection. Ctrl+D : duplicate. Ctrl+Z / Ctrl+Y, Ctrl+S.
// - While the game runs : the active state / nodes are highlighted, with the
//   live parameters / Blackboard values (choose the actor in the toolbar).
// =============================================================================

#include <filesystem>
#include <functional>
#include <string>

#include <imgui/imgui.h>

namespace lynx::editor::graph_editors
{
	/** .animgraph or .bt */
	bool CanOpen(const std::filesystem::path& path);

	/** Opens the file (or brings its window to the front). */
	void Open(const std::filesystem::path& path);

	/** Every open editor. `dock_id` : where new windows are docked (0 : floating). */
	void DrawAll(ImGuiID dock_id);

	/** A graph editor window is focused : the level editor shortcuts wait. */
	bool HasFocus();

	void PathMoved(const std::filesystem::path& from, const std::filesystem::path& to);
	void PathDeleted(const std::filesystem::path& target);

	bool HasUnsavedChanges();
	void SaveAll();

	/** Content of a new file (Content Browser > New file). */
	std::string NewAnimGraphTemplate();
	std::string NewBehaviorTreeTemplate();

	/** Asset path field (textures) : the one of the level Details. Returns true when changed. */
	void SetAssetFieldDrawer(std::function<bool(std::string& value)> drawer);

	/** Frees the node editor contexts (before ImGui's shutdown). */
	void Shutdown();
}
