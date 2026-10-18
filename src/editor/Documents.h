#pragma once

// =============================================================================
// Document tabs of the editor (raven/Documents.inl), as in Unreal : the
// level tab, then one tab per open file (.vasm, .widget, .animgraph, .bt...).
// Each tab has a dock area of its own, with the panels of its editor.
// =============================================================================

namespace lynx::editor::documents
{
	/** The level tab is the one shown : the shortcuts of the level editor apply. */
	bool LevelTabActive();
}
