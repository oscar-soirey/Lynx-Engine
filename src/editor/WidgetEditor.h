#pragma once

// =============================================================================
// Widget Editor (Unreal UMG Designer-like)
// -----------------------------------------------------------------------------
// One window per open .widget file, docked in the middle of the editor next to
// the Viewport (Content Browser : double-click, or New file > Widget).
//
//   +-----------+-----------------------------------+--------------+
//   | Palette   |  toolbar : Save, Undo, resolution |  Details     |
//   | (drag)    |                                   |  (slot, then |
//   |-----------|         Designer                  |   widget     |
//   | Hierarchy |  select, move, resize, anchors    |   fields)    |
//   +-----------+-----------------------------------+--------------+
//
// - Palette : drag a widget onto the Designer or the Hierarchy (double-click :
//   into the selected panel). "User Widgets" : the other .widget assets.
// - Hierarchy : drag to move / reparent, right click : rename, duplicate,
//   wrap with a panel, delete, copy / paste.
// - Designer : click to select, drag to move (children of a CanvasPanel),
//   handles to resize, wheel to zoom, right / middle drag to pan, F to fit.
// - Details : the slot of the widget (anchors presets for a CanvasPanel), then
//   its reflected fields.
// - Shortcuts (window focused) : Ctrl+S, Ctrl+Z / Ctrl+Y, Del, Ctrl+D, F2,
//   Ctrl+C / Ctrl+V.
//
// The layout is the one of the game (same code, lynx::UserWidget) ; the look
// is drawn by the editor (close to the renderer, not identical).
// =============================================================================

#include <filesystem>
#include <functional>
#include <string>

#include <imgui/imgui.h>

namespace lynx::editor::widget_editor
{
	bool CanOpen(const std::filesystem::path& path);

	/** Opens the file (or brings its window to the front). */
	void Open(const std::filesystem::path& path);

	/** Every open editor. `dock_id` : where new windows are docked (0 : floating). */
	void DrawAll(ImGuiID dock_id);

	/** A Widget Editor window is focused : the level editor shortcuts wait. */
	bool HasFocus();

	void PathMoved(const std::filesystem::path& from, const std::filesystem::path& to);
	void PathDeleted(const std::filesystem::path& target);

	bool HasUnsavedChanges();
	void SaveAll();

	/** Content of a new .widget file (Content Browser > New file). */
	std::string NewFileTemplate();

	/**
	 * Field of the asset paths of the Details panel (textures, fonts, nested
	 * widgets) : the one of the level Details (picker of assets). Returns true
	 * when the value changed.
	 */
	void SetAssetFieldDrawer(std::function<bool(std::string& value)> drawer);
}
