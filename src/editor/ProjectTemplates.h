#pragma once

// =============================================================================
// Project templates (editor "New project")
// -----------------------------------------------------------------------------
// A template is a folder next to the editor : <editor>/templates/<id>/ (copied
// from Lynx/templates at build time). A new project = templates/_common/ then
// templates/<id>/ on top of it (same path : the template's file wins).
//
//   templates/<id>/template.json   { "name": ..., "description": ..., "order": 1,
//                                    "no_cpp": false, "plugins": [ "Dialogue" ] }
//   templates/<id>/icon.png        optional : icon shown in "New project" (pixel art,
//                                    drawn with nearest filtering ; 32x32 works well)
// template.json and icon.png describe the template : they are not copied.
//
// In text files (and file names), these words are replaced :
//   {{PROJECT_NAME}}    identifier : CMake target, DLL name   (ex : MyGame)
//   {{PROJECT_TITLE}}   name as typed by the user             (ex : My Game)
//
// .lynx/starter_terrain.txt (optional) : voxels painted the first time the
// project is opened, see ApplyStarterTerrain().
// =============================================================================

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lynx::editor::project_templates
{
	struct Template
	{
		std::string id;            // folder name
		std::string name;
		std::string description;
		int order = 100;
		std::filesystem::path folder;
		/** <folder>/icon.png, empty if the template has none. */
		std::filesystem::path icon;

		/**
		 * "no_cpp": true in template.json : project without C++. The build
		 * files of _common (CMakeLists.txt, Build.bat, src/, *.cpp / *.h at
		 * the root) are not copied : the editor loads the engine's generic
		 * game DLL and never compiles (GameProject::script_only).
		 */
		bool no_cpp = false;

		/** "plugins": [ "Dialogue" ] : enabled in the new project (plugins.json). */
		std::vector<std::string> plugins;
	};

	// <editor>/templates
	std::filesystem::path TemplatesFolder();

	// Every template, sorted by "order" then name. `error` : why the list is empty.
	std::vector<Template> List(std::string& error);

	// "Mon jeu 2" -> "MonJeu2" ("Game" in front when it starts with a digit).
	std::string ToIdentifier(const std::string& title);

	// Folder of the new project : `parent` / title without the characters
	// Windows does not allow in a file name.
	std::filesystem::path ProjectFolder(const std::filesystem::path& parent, const std::string& title);

	// Creates the project. The folder must not exist, or be empty.
	bool Create(const Template& tmpl, const std::filesystem::path& parent, const std::string& title,
	            std::filesystem::path& out_root, std::string& error);

	// Editor startup, once the voxel world of the project is loaded : paints
	// the rectangles of .lynx/starter_terrain.txt (working directory = project
	// root), saves the world to `world_file`, then deletes the text file.
	// Lines : "x_min y_min x_max y_max type" in WORLD units, '#' comments.
	// Returns true if something was painted.
	bool ApplyStarterTerrain(uint32_t scene, const std::string& world_file);
}
