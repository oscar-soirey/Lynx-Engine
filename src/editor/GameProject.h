#pragma once

// =============================================================================
// Game project (editor)
// -----------------------------------------------------------------------------
// A game project is a folder :
//
//   MyGame/
//     assets/          <- game assets (levels, textures, sounds, scripts...)
//     build/           <- the compiled game DLL (ex : libGameExample.dll)
//
// The game DLL is the one in build/ (or one sub folder below, ex :
// build/Debug/) that exports "FactoryRegisterClasses" (LYNX_LINK_MODULE).
// The exports are read from the file itself : no DLL is loaded to find it.
//
// When the editor opens a project, the working directory becomes the project
// root : every "assets/..." path of the engine then points into the project.
// =============================================================================

#include <filesystem>
#include <string>
#include <vector>

namespace lynx::editor
{
	struct GameProject
	{
		std::string name;                       // folder name
		std::filesystem::path root;             // absolute
		std::filesystem::path assets_dir;       // root / "assets"
		std::filesystem::path build_dir;        // root / "build"

		// Every game DLL found in build/ (most recently built first).
		std::vector<std::filesystem::path> modules;

		// The one the editor loads (absolute). Defaults to modules[0].
		std::filesystem::path module_path;
	};

	// Checks `folder` and fills `out`. On failure returns false and explains
	// why in `error` (no assets/ folder, no game DLL in build/...).
	bool InspectProject(
		const std::filesystem::path& folder,
		GameProject& out,
		std::string& error
	);

	// True when the PE file `dll_path` exports a function called `symbol`.
	bool DllExportsSymbol(
		const std::filesystem::path& dll_path,
		const char* symbol
	);

	// File of the engine DLL loaded by the editor (lynx), empty if unknown.
	std::filesystem::path GetEngineModulePath();

	// Non-empty when the game DLL is older than the engine DLL : it was not
	// rebuilt after an engine change, and the engine classes it embeds
	// (InputAction, actors...) may not match anymore -> random crashes.
	std::string CheckModuleUpToDate(const std::filesystem::path& game_module);

	// Folder of the editor executable (editor-wide files live there).
	std::filesystem::path GetEditorDirectory();

	// Recent projects (most recent first), stored next to the editor exe.
	std::vector<std::filesystem::path> LoadRecentProjects();
	void AddRecentProject(const std::filesystem::path& root);
	void RemoveRecentProject(const std::filesystem::path& root);

	// Starts a new editor process (same exe). `project` empty = project
	// browser. Returns false if the process could not be started.
	bool LaunchEditorProcess(const std::filesystem::path& project);
}
