#pragma once

// =============================================================================
// Voxel Assembly editor (.vasm files) : actor classes made of blocks
// (raven/VoxelAssembly.inl, Raven BlockEditor widget ; runtime : src/vasm)
// -----------------------------------------------------------------------------
// A .vasm file is an actor class : a name, a parent C++ class, variables,
// event graphs and functions (each function has its own graph), like a
// Blueprint of Unreal, with blocks like Scratch. The engine runs the blocks
// itself (no JavaScript) : a save puts the class in the factory at once.
// =============================================================================

#include <filesystem>
#include <string>

namespace lynx::editor::vasm_editor
{
	bool CanOpen(const std::filesystem::path& path);
	void Open(const std::filesystem::path& path);

	/** The keyboard is in a Voxel Assembly tab (the level editor waits). */
	bool HasFocus();

	void PathMoved(const std::filesystem::path& from, const std::filesystem::path& to);
	void PathDeleted(const std::filesystem::path& target);

	bool HasUnsavedChanges();
	void SaveAll();

	/** Content of a new .vasm file (class_name : letters, digits, _). */
	std::string NewFileTemplate(const std::string& class_name);

	/** A copy of a .vasm file takes a class name of its own. */
	void RenameClassInCopy(const std::filesystem::path& copy, const std::string& new_class_name);
}
