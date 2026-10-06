#pragma once

// =============================================================================
// Plugins (C++)
// -----------------------------------------------------------------------------
// A plugin is a folder with a plugin.json :
//
//   <Name>/
//     plugin.json           { "name": "Dialogue", "version": "1.0",
//                             "description": "...", "author": "...",
//                             "runtime": "bin/Dialogue.dll",
//                             "editor":  "bin/Dialogue.Editor.dll" }
//     bin/Dialogue.dll          runtime module : classes, components, game code.
//                               Loaded by the editor AND the game.
//     bin/Dialogue.Editor.dll   editor module (optional) : windows, menus,
//                               file editors. Never shipped with the game.
//     assets/                   optional, read like the project's assets/
//                               ("plugins/Dialogue/..." is NOT needed : the
//                               plugin reads its files with PluginAssetPath).
//
// Two places :
//   <editor folder>/plugins/<Name>/   engine plugins (shipped with Lynx :
//                                     Dialogue, CineCamera). Off by default.
//   <project>/plugins/<Name>/         project plugins. On by default.
// Enabled per project in <project>/plugins.json ({ "Dialogue": true }),
// window Plugins of the editor (a change needs a restart of the editor).
// A shipped game has the runtime modules of its enabled plugins in
// <game folder>/plugins/<Name>/ (Ship Game copies them).
//
// The runtime module is a DLL made with LYNX_PLUGIN (plugins/LynxPlugin.h) :
// its classes are registered like the game's (LYNX_LINK_MODULE), and it gets
// Startup / Shutdown / Tick and the game hooks (GameModuleAPI.h).
// =============================================================================

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Common.h"

namespace lynx::plugins
{
	struct PluginInfo
	{
		std::string name;
		std::string version;
		std::string description;
		std::string author;
		std::string category;

		std::filesystem::path folder;        // absolute
		std::filesystem::path runtime_dll;   // absolute, empty : none
		std::filesystem::path editor_dll;    // absolute, empty : none

		bool engine_plugin = false;          // <editor>/plugins (else the project's)
		bool enabled = false;                // plugins.json of the project
		bool loaded = false;                 // runtime module loaded
		std::string error;                   // why it is not loaded
	};

	/**
	 * Looks for the plugins (plugin.json) in `engine_dir` and `project_dir`
	 * (empty = skipped) and reads <project_root>/plugins.json. A project
	 * plugin hides an engine plugin of the same name.
	 */
	LYNX_API void Discover(const std::filesystem::path& engine_dir,
	                       const std::filesystem::path& project_dir,
	                       const std::filesystem::path& project_root);

	/** Every plugin found by Discover. */
	LYNX_API std::vector<PluginInfo>& GetPlugins();
	LYNX_API PluginInfo* FindPlugin(const std::string& name);

	/** Enabled for this project (saved by SaveSettings). Applied at the next start. */
	LYNX_API void SetEnabled(const std::string& name, bool enabled);
	LYNX_API bool SaveSettings();

	/**
	 * Loads the runtime module of every enabled plugin : classes in the
	 * factory, then LynxPlugin_Startup. After Engine::Create, before the
	 * first level. Returns the number of plugins loaded.
	 */
	LYNX_API int LoadRuntimeModules();

	/** Shutdown + unload every plugin (end of the program). */
	LYNX_API void UnloadAll();

	/** Absolute path of a file of a plugin ("" if the plugin is unknown). */
	LYNX_API std::filesystem::path PluginAssetPath(const std::string& plugin, const std::string& relative);

	/**
	 * Called by the hosts / the engine (also broadcast to the plugins) :
	 * scene ready (after the game's LynxGame_SetupScene), Play / Stop,
	 * level (re)created, every frame.
	 */
	LYNX_API void SetupScene(uint32_t scene);
	LYNX_API void OnGameStart();
	LYNX_API void OnGameEnd();
	LYNX_API void OnLevelLoaded();
	LYNX_API void Tick(float dt, bool playing);

	/**
	 * Engine version the plugins must be built for (plugin API). A plugin
	 * module that returns another one in LynxPlugin_Info is not loaded.
	 */
	constexpr int kApiVersion = 1;
}
