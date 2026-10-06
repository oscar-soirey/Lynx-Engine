#pragma once

// =============================================================================
// "Open with" : opens the game folder in a code editor
// -----------------------------------------------------------------------------
// The folder of the project (the one with assets/, build/, CMakeLists.txt...,
// not src/) in :
//   Visual Studio : devenv.exe <folder> (Open Folder), found with vswhere ;
//   VS Code       : Code.exe <folder> (user / system install, else "code" of the PATH) ;
//   CLion         : clion64.exe <folder> (Toolbox script, Program Files, else
//                   "clion" of the PATH).
// Windows only (elsewhere : false).
// =============================================================================

#include <filesystem>
#include <string>

namespace lynx::editor::external_ide
{
	enum class Ide { VisualStudio = 0, VsCode, CLion, Count };

	const char* Name(Ide ide);

	/** Opens `folder` in `ide`. false : not found / could not start (`error` says why). */
	bool Open(Ide ide, const std::filesystem::path& folder, std::string& error);

	// -------------------------------------------------------------------------
	// Toolbar split button : "Open with" the last code editor used, arrow for
	// the others. The icons are the real ones of the installed programs
	// (extracted from their .exe) ; a generic icon when not found.
	// -------------------------------------------------------------------------

	/** Last code editor used (saved next to the editor : open_with.txt). */
	Ide LastUsed();
	void SetLastUsed(Ide ide);

	/**
	 * Searches the programs in the background (the first call starts it).
	 * false while searching.
	 */
	bool DetectionDone();

	/** Found on this computer (false while searching). */
	bool IsInstalled(Ide ide);

	/**
	 * Icon of the program (OpenGL texture id for ImGui, 0 if none). HRL
	 * textures are stored bottom-up : draw with uv0 = (0, 1), uv1 = (1, 0).
	 */
	unsigned int IconTexture(Ide ide);
}
