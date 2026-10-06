#pragma once

// =============================================================================
// Voxel types in the editor
// -----------------------------------------------------------------------------
// - Paint window : the swatches of the types (click : brush type), "+" adds a
//   type, right click : edit / duplicate / delete (the last one only : the
//   levels store the ids).
// - Color Picking window : the color picker and the properties of the type of
//   the brush (name, collision, flags, emissive, indestructible, on_destroyed).
//   C over a voxel of the viewport picks its type.
//
// Every change is shown at once in the viewport and saved in assets/voxels.json
// when the edit ends (the // comments of the file are not kept).
// =============================================================================

#include <cstdint>

namespace lynx::editor::voxel_types
{
	/**
	 * Swatches of the types (Paint window). `selected` : brush type (0 = empty).
	 * `open_editor` is set to true when the user asks to edit a type.
	 */
	void DrawPalette(int& selected, uint32_t scene, bool& open_editor);

	/** Content of the Color Picking window : the selected type. */
	void DrawEditor(int& selected, uint32_t scene);

	/** Changes not written yet (an edit still in progress). */
	bool HasUnsavedChanges();

	/** Writes assets/voxels.json now. */
	bool Save();
}
