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
}
