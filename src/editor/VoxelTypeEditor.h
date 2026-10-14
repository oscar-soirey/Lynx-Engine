#pragma once

// =============================================================================
// Voxel types in the editor
// -----------------------------------------------------------------------------
// - Paint panel : the swatches of the types (click : brush type), "+" adds a
//   type, right click : edit / duplicate / delete (the last one only : the
//   levels store the ids).
// - Color Picking panel (panels/color_picking.xml) : the color and the
//   properties of the type of the brush (name, collision, flags, emissive,
//   texture, indestructible, on_destroyed, physics). C over a voxel of the
//   viewport picks its type.
//
// Every change is shown at once in the viewport and saved in assets/voxels.json
// when the edit ends (the // comments of the file are not kept).
// =============================================================================

#include <Raven.h>

#include <cstdint>
#include <functional>

namespace lynx::editor::voxel_types
{
	struct Host
	{
		Rid window = R_INVALID_ID;
		uint32_t scene = 0;
		std::function<int()> get_selected;            // brush type (0 = empty)
		std::function<void(int)> set_selected;
		std::function<void()> show_editor;            // brings the Color Picking panel
	};

	void SetHost(const Host& host);

	/** The swatches go in `palette` (a WrapBox) ; `hint` : a label under them. */
	void BindPalette(Rid palette, Rid hint);

	/** The Color Picking panel (its widgets are named vt.*). */
	void BindEditor(Rid panel);

	/** Every frame : follows the types (count, colors), the brush type, a reload. */
	void Update();

	/** Changes not written yet (an edit still in progress). */
	bool HasUnsavedChanges();

	/** Writes assets/voxels.json now. */
	bool Save();
}
