#pragma once

// =============================================================================
// Property widgets of the Details window (Unreal-like)
// -----------------------------------------------------------------------------
//   Location v   [X| 0.0 ] [Y| 0.0 ] [Z| 0.0 ]
//   Rotation v   [X| 0.0 ] [Y| 0.0 ] [Z| 0.0 ]
//   Scale    v   [X| 1.0 ] [Y| 1.0 ] [Z| 1.0 ]
//
// - X / Y / Z (/ W) : colored tags (red, green, blue), each followed by its
//   field ; the fields share the width of the window. Dragging the tag changes
//   the value too, like dragging the field. Double-click : type a value.
// - "Label v" opens a small menu : Reset to default, Copy, Paste.
// =============================================================================

#include "../core/Common.h"

namespace lynx::editor::property_widgets
{
	// Two columns (name | value) for the rows below. Returns false if the
	// table is not visible (EndTable must then not be called).
	bool BeginTable(const char* id);
	void EndTable();

	// Name of a row (left column), with an optional "v" menu.
	// Opens the right column for the value widget.
	void RowLabel(const char* label);

	// count = 2, 3 or 4 components. `reset` : value of "Reset to default"
	// (nullptr : no reset). Must be called inside a row (after RowLabel).
	bool Vector(const char* id, float* values, int count, float speed, const float* reset, const char* format = "%.3f");

	// A whole row : label + vector (inside BeginTable / EndTable).
	bool VectorRow(const char* label, float* values, int count, float speed, const float* reset,
	               const char* format = "%.3f");

	// Location / Rotation / Scale rows (inside BeginTable / EndTable).
	bool TransformRows(lynx::transform& value);
}
