#pragma once

// =============================================================================
// Project infos (tooltip of the title tab)
// -----------------------------------------------------------------------------
// Hovering the "Lynx - Project" tab of the title bar shows : name, folder,
// creation / last change dates, size on disk, files (scripts, levels,
// images, sounds), game module, engine version.
//
// The folder is scanned in a background thread (a big project takes a
// moment) ; the scan is redone when the tooltip shows up again after 20 s.
// =============================================================================

#include <cstdint>
#include <filesystem>
#include <string>

namespace lynx::editor::project_info
{
	struct Project
	{
		std::string name;
		std::filesystem::path root;
		std::filesystem::path assets_dir;
		std::filesystem::path build_dir;
		std::filesystem::path module_path;   // game DLL (may be empty)
		std::string engine_version;           // "2026.1.0"
	};

	/** The project of the editor (once it is opened). */
	void SetProject(const Project& project);

	/** Content of the tooltip (between BeginTooltip / EndTooltip). */
	void DrawTooltip();

	/** Waits for the background scan (end of the editor). */
	void Shutdown();

	// -------------------------------------------------------------------------
	// Scan (testable alone)
	// -------------------------------------------------------------------------

	struct Stats
	{
		std::uintmax_t bytes = 0;         // whole project folder
		std::uintmax_t assets_bytes = 0;
		std::uintmax_t build_bytes = 0;
		int files = 0;
		int folders = 0;
		int scripts = 0;                  // .js / .py
		int levels = 0;                   // .xml in assets
		int images = 0;                   // .png .jpg ...
		int sounds = 0;                   // .wav .ogg .mp3
		int graphs = 0;                   // .bt .animgraph .widget
		long long created = 0;            // unix time of the folder (0 : unknown)
		long long modified = 0;           // most recent file outside build/ (0 : unknown)
		std::string last_file;            // that file, relative to the root
	};

	Stats Scan(const std::filesystem::path& root, const std::filesystem::path& build_dir);

	/** "1.4 MB" */
	std::string FormatBytes(std::uintmax_t bytes);

	/** "06/10/2026 14:32  (3 hours ago)" */
	std::string FormatDate(long long unix_time, long long now);
}
