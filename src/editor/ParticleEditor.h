#pragma once

// =============================================================================
// Particle Editor (.vfx), Unreal Cascade-like
// -----------------------------------------------------------------------------
// One window per open file, docked next to the Viewport :
//
//   [Save] [Restart] [Pause] time scale  loop / duration  [+ Emitter]  grid  bg
//   +--------------------------------------------+----------------------------+
//   |  Preview (its own scene : right drag to   |  Details                   |
//   |  orbit, middle drag to pan, wheel : zoom)  |  (module selected below,   |
//   |                                            |   curves / gradient)       |
//   +--------------------------------------------+----------------------------+
//   |  Emitters : one column per emitter                                      |
//   |  [x] Flames      [x] Sparks      [ + Emitter ]                          |
//   |   Required        Required                                              |
//   |   Spawn           Spawn                                                 |
//   |   Lifetime        ...                                                   |
//   |   Color Over Life (gradient)                                            |
//   +--------------------------------------------------------------------------+
//
// - Click a module : its settings in Details. Right click a column header :
//   rename, duplicate, delete, move, add module ; right click a module : remove.
// - Curves (Size / Rotation Over Life) : drag the keys, double-click to add one,
//   right click a key to delete it. Gradient (Color Over Life) : the same on the
//   markers under the bar.
// - Ctrl+S save, Ctrl+Z / Ctrl+Y undo / redo, Space : restart the preview.
// - Saving reloads the ParticleActors of the level that use the file.
// =============================================================================

#include <filesystem>
#include <functional>
#include <string>

#include <imgui/imgui.h>

namespace lynx::editor::particle_editor
{
	/** .vfx */
	bool CanOpen(const std::filesystem::path& path);

	/** Opens the file (or brings its window to the front). */
	void Open(const std::filesystem::path& path);

	/** Every open editor. `dock_id` : where new windows are docked (0 : floating). */
	void DrawAll(ImGuiID dock_id);

	/** A particle editor window is focused : the level editor shortcuts wait. */
	bool HasFocus();

	void PathMoved(const std::filesystem::path& from, const std::filesystem::path& to);
	void PathDeleted(const std::filesystem::path& target);
	bool HasUnsavedChanges();
	void SaveAll();

	/** Content of a new file (Content Browser > New file > Particle System). */
	std::string NewFileTemplate();

	/** Asset path field (textures) : the one of the level Details. Returns true when changed. */
	void SetAssetFieldDrawer(std::function<bool(std::string& value)> drawer);

	/** Deletes the preview scenes (before the renderer shuts down). */
	void Shutdown();
}
