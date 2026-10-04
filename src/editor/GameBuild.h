#pragma once

// =============================================================================
// Game build & hot reload (editor)
// -----------------------------------------------------------------------------
// - Shadow copy : the editor loads a COPY of the game DLL (in %TEMP%), never
//   build/<game>.dll itself, so the game can be rebuilt while the editor runs
//   (Build.bat, CLion, or "Compile" in the editor).
// - Build : runs the project's Build.bat (or "cmake --build build") in the
//   background, output shown in the "Build" window.
// - Watch : notices when build/<game>.dll changed on disk (built outside the
//   editor) so it can be reloaded.
// =============================================================================

#include <filesystem>
#include <string>
#include <vector>

namespace lynx::editor::game_build
{
	// -------------------------------------------------------------------------
	// Shadow copy
	// -------------------------------------------------------------------------

	// Copies `dll` to a unique file of the editor temp folder and returns it
	// (empty + `error` on failure). Retries a little : the linker may still be
	// writing the file.
	std::filesystem::path MakeShadowCopy(const std::filesystem::path& dll, std::string& error);


	// -------------------------------------------------------------------------
	// Build
	// -------------------------------------------------------------------------

	// Can be called again (ex : the game DLL did not exist before the first build).
	void Init(const std::filesystem::path& project_root, const std::filesystem::path& module_path);

	// Starts a build (nothing if one is running). false = could not start.
	bool Start();
	void Cancel();
	bool IsRunning();

	// Editor exit : stops a running build.
	void Shutdown();

	// Once per frame. Returns true ONCE when a build just ended ; `success`
	// tells whether it succeeded.
	bool PollFinished(bool& success);

	// Short text for the toolbar ("Building...", "Build failed"...), "" = none.
	std::string ToolbarStatus();

	// "Build" window (output). Opened by Start().
	void DrawWindow();

	// Only the output log (scrolling child filling the space left), for other
	// windows (ex : the build done before the editor opens).
	void DrawOutput();

	// Last `max_lines` lines of the build output.
	std::vector<std::string> OutputTail(size_t max_lines);

	// The last build ended and succeeded.
	bool LastBuildSucceeded();
	bool& WindowOpen();


	// -------------------------------------------------------------------------
	// Watch build/<game>.dll
	// -------------------------------------------------------------------------

	// The loaded DLL is the current file (call after each load).
	void MarkModuleLoaded();

	// Once per frame : true when build/<game>.dll changed since the last load
	// and has not been touched for a moment (the linker is done).
	bool ModuleChangedOnDisk();
}
