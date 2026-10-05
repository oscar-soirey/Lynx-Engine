#pragma once

// =============================================================================
// Ship Game
// -----------------------------------------------------------------------------
// Toolbar > "Ship Game" : makes a game that runs without the editor, in a
// folder chosen by the user :
//
//   1. saves the level, compiles the game (Build.bat / cmake, like Compile) ;
//   2. packs assets/ into a zip archive (the engine reads it with PhysFS :
//      lynx::Engine::SetReleaseMode / fs::AssetSource::Archive) ;
//   3. <Game>.exe = LynxRuntime.exe + that archive appended at its end (the
//      runtime finds it in its own executable : fs::HasEmbeddedArchive) ;
//      optionally without console window ;
//   4. copies next to it the DLLs of the engine (every .dll next to the
//      editor), the game DLL and input.json.
//
// The output folder only needs these files : no assets/ folder, no project.
// =============================================================================

#include <filesystem>
#include <functional>

namespace lynx::editor::ship_game
{
	// Project being edited (after it is opened / reloaded).
	void SetProject(const std::filesystem::path& project_root, const std::string& project_name);

	// Opens the "Ship Game" window.
	void Open();

	// Every frame (editor UI). `save_level` : saves the level before shipping.
	void Draw(const std::function<void()>& save_level);

	bool IsBusy();
}
