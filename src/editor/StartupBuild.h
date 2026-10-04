#pragma once

// =============================================================================
// Build before opening (editor)
// -----------------------------------------------------------------------------
// When the game DLL is missing, older than the engine, or older than its
// sources, the editor compiles the game BEFORE loading it (a DLL built against
// another version of the engine crashes the editor).
//
// Small window with the build output, like the project browser : it owns its
// GLFW window and ImGui context, both destroyed on return.
// =============================================================================

#include <string>

#include "../host/GameProject.h"

namespace lynx::editor
{
	enum class StartupBuildResult
	{
		Built,          // build succeeded : inspect the project again, then load
		OpenAnyway,     // the user kept the current DLL (only when allowed)
		OtherProject,   // back to the project browser
		Quit,
	};

	// `reason` : why the game must be compiled (shown at the top).
	// `allow_open_anyway` : the current DLL can be loaded safely (only its
	// sources are newer). When false, the DLL is never loaded as it is.
	// game_build::Init() must have been called for this project.
	StartupBuildResult RunStartupBuild(
		const host::GameProject& project,
		const std::string& reason,
		bool allow_open_anyway
	);
}
