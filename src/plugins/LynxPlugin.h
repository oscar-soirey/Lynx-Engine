#pragma once

// =============================================================================
// Runtime module of a plugin (loaded by the editor AND the game)
// -----------------------------------------------------------------------------
//
//   #include <plugins/LynxPlugin.h>
//
//   LYNX_PLUGIN(Dialogue)                       // required, once per DLL
//
//   LYNX_LINK_MODULE(                           // optional : the classes
//       LYNX_MODULE_REGISTER(DialogueNPC);
//   )
//
//   LYNX_PLUGIN_STARTUP()  { ... }               // optional : after the classes
//   LYNX_PLUGIN_SHUTDOWN() { ... }               // optional : end of the program
//   LYNX_PLUGIN_TICK(dt, playing) { ... }        // optional : every frame
//
// The game hooks of GameModuleAPI.h work too (LYNX_GAME_EXPORT
// LynxGame_OnGameStart...).
//
// From here, the whole engine API (<Lynx.h>) is available : actors,
// components, interfaces, widgets, JS functions (lynx::RegisterScriptFunction).
// Files of the plugin : lynx::plugins::PluginAssetPath("Dialogue", "ui/box.png").
// =============================================================================

#include "../Lynx.h"
#include "../core/Factory.h"
#include "../core/GameModuleAPI.h"
#include "../core/Plugins.h"

namespace lynx::plugins
{
	/** What LynxPlugin_Info returns (C layout). */
	struct ModuleInfo
	{
		int api_version;
		const char* name;
	};

	using InfoFn = const ModuleInfo* (*)();
	using StartupFn = void (*)();
	using ShutdownFn = void (*)();
	using TickFn = void (*)(float dt, int playing);

	constexpr const char* kInfo = "LynxPlugin_Info";
	constexpr const char* kStartup = "LynxPlugin_Startup";
	constexpr const char* kShutdown = "LynxPlugin_Shutdown";
	constexpr const char* kTick = "LynxPlugin_Tick";
}

#define LYNX_PLUGIN(__name__) \
	LYNX_GAME_EXPORT const lynx::plugins::ModuleInfo* LynxPlugin_Info() \
	{ \
		static const lynx::plugins::ModuleInfo info{ lynx::plugins::kApiVersion, #__name__ }; \
		return &info; \
	}

#define LYNX_PLUGIN_STARTUP() LYNX_GAME_EXPORT void LynxPlugin_Startup()
#define LYNX_PLUGIN_SHUTDOWN() LYNX_GAME_EXPORT void LynxPlugin_Shutdown()
#define LYNX_PLUGIN_TICK(__dt__, __playing__) LYNX_GAME_EXPORT void LynxPlugin_Tick(float __dt__, int __playing__)
