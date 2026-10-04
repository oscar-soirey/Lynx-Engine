#pragma once

// =============================================================================
// Project browser (editor)
// -----------------------------------------------------------------------------
// Small window shown when the editor starts without a project : recent
// projects, "Browse..." (folder dialog), path field, folder drag & drop.
// It owns its own GLFW window and ImGui context, both destroyed on return,
// so the editor starts afterwards exactly as before.
// =============================================================================

#include <filesystem>
#include <string>

#include "../host/GameProject.h"

namespace lynx::editor
{
	// Returns true when the user opened a valid project (filled in `project`),
	// false when the window was closed.
	// `message` : shown at the top (ex : why the requested project could not
	// be opened). `initial_path` : pre-filled in the path field.
	bool RunProjectBrowser(
		host::GameProject& project,
		const std::string& message = {},
		const std::filesystem::path& initial_path = {}
	);
}
