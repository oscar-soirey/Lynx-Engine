#pragma once

// =============================================================================
// File edits of Lynxie
// -----------------------------------------------------------------------------
// Lynxie changes the files of the project with SEARCH / REPLACE blocks, applied
// here by the editor (not by a Python script : a small model doing
// text.replace("}", ...) breaks the file everywhere).
//
//     FILE: assets/classes/Wanderer.js
//     <<<<<<< SEARCH
//         Update(dt) {
//     =======
//         EndPlay() {
//             print("hello");
//         }
//
//         Update(dt) {
//     >>>>>>> REPLACE
//
// - The SEARCH text must be found exactly ONCE in the file (trailing spaces
//   and \r\n are ignored), else nothing is written and the error goes back to
//   Lynxie with the real content of the file.
// - Empty SEARCH : creates the file (refused if it already exists).
// - All the blocks of an answer are applied together, or none.
// - .js files are compiled (syntax only) and .json files parsed before being
//   written : a broken result is refused.
// - The previous content is kept : Revert() restores the last edit.
// =============================================================================

#include <filesystem>
#include <string>
#include <vector>

namespace lynx::editor::file_edits
{
	struct Block
	{
		std::string path;      // as written by the model (project root relative)
		std::string search;
		std::string replace;
	};

	// Blocks of an answer (empty if there is none).
	std::vector<Block> Parse(const std::string& answer);

	// Answer for the chat : each block becomes "[edit] path".
	std::string StripBlocks(const std::string& answer);

	struct Result
	{
		bool ok = false;
		std::string message;                    // what was done, or why nothing was done
		std::vector<std::string> files;         // edited files (project relative)
		std::vector<std::string> failed_files;  // files to show again to the model
		bool cpp_changed = false;               // C++ edited : the game must be compiled
	};

	Result Apply(const std::vector<Block>& blocks, const std::filesystem::path& project_root);

	// Last applied edit can be undone (files restored, created files deleted).
	bool CanRevert();
	std::string RevertDescription();
	std::string Revert();
}
