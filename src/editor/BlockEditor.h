#pragma once

// =============================================================================
// Block Editor (.blocks files) : actor classes made of blocks, like Scratch
// (raven/BlockEditor.inl, Raven BlockEditor widget)
// -----------------------------------------------------------------------------
// A .blocks file is an actor class : its name, its parent class (Actor,
// Humanoid...), its variables and its scripts of blocks. Each save writes the
// JavaScript class next to it (Hero.blocks -> Hero.blocks.js, "generated, do
// not edit") : the engine loads it like any class (Place Actors, levels,
// Level.spawn("Hero")).
// =============================================================================

#include <filesystem>
#include <string>

namespace lynx::editor::block_editor
{
	bool CanOpen(const std::filesystem::path& path);
	void Open(const std::filesystem::path& path);

	/** The keyboard is in a block editor (the level editor waits). */
	bool HasFocus();

	void PathMoved(const std::filesystem::path& from, const std::filesystem::path& to);
	void PathDeleted(const std::filesystem::path& target);

	bool HasUnsavedChanges();
	void SaveAll();

	/** Content of a new .blocks file (class_name : a valid JavaScript name). */
	std::string NewFileTemplate(const std::string& class_name);

	/** A copy of a .blocks file takes a class name of its own. */
	void RenameClassInCopy(const std::filesystem::path& copy, const std::string& new_class_name);

	/** Hero.blocks -> Hero.blocks.js (the generated class). */
	std::filesystem::path GeneratedScript(const std::filesystem::path& blocks_file);
}
