#pragma once

// =============================================================================
// Editor module of a plugin (<Name>.Editor.dll) : loaded by the editor only,
// after the runtime module of the same plugin. Never shipped with the game.
// -----------------------------------------------------------------------------
//
//   #include <editor/EditorPluginAPI.h>
//
//   static void DrawMyWindow(bool* open, void*) {
//       if (ImGui::Begin("My Window", open)) { ImGui::Text("Hello"); }
//       ImGui::End();
//   }
//
//   LYNX_EDITOR_PLUGIN_STARTUP(api)
//   {
//       LYNX_EDITOR_PLUGIN_INIT(api);                         // ImGui context
//       api->add_window("MyPlugin", "My Window", DrawMyWindow, nullptr, false);
//       api->add_file_editor("MyPlugin", ".dialogue", OpenDialogue, nullptr);
//       api->add_new_file("MyPlugin", "Dialogue (.dialogue)", "NewDialogue", ".dialogue", "{ }");
//   }
//   LYNX_EDITOR_PLUGIN_SHUTDOWN() { ... }
//
// ImGui : the module compiles its own copy of the editor's ImGui sources
// (lynx_add_plugin does it) and draws in the editor's context (set by
// LYNX_EDITOR_PLUGIN_INIT). Use the same third-party/imgui as the editor.
// The engine API (<Lynx.h>) is available as in the runtime module.
// =============================================================================

#include <imgui/imgui.h>

namespace lynx
{
	class Actor;
}

namespace lynx::editor_api
{
	constexpr int kVersion = 1;

	/** Window : draw it (Begin / End are yours). `open` : its close button. */
	using DrawFn = void (*)(bool* open, void* user);
	/** Double-click in the Content Browser (absolute path, UTF-8). true : handled. */
	using OpenFileFn = bool (*)(const char* path, void* user);
	using CallbackFn = void (*)(void* user);
	/** Every editor frame (also while playing). */
	using TickFn = void (*)(float dt, void* user);

	struct EditorAPI
	{
		int version = kVersion;

		// ImGui of the editor (LYNX_EDITOR_PLUGIN_INIT).
		ImGuiContext* imgui = nullptr;
		ImGuiMemAllocFunc imgui_alloc = nullptr;
		ImGuiMemFreeFunc imgui_free = nullptr;
		void* imgui_alloc_user = nullptr;

		/** Window listed in Windows > Plugins (`title` : unique ImGui name). */
		void (*add_window)(const char* plugin, const char* title, DrawFn draw, void* user, bool open_at_start) = nullptr;
		/** Shows a window added by add_window. */
		void (*open_window)(const char* title) = nullptr;

		/** Item of the toolbar menu "Plugins". */
		void (*add_menu_item)(const char* plugin, const char* label, CallbackFn callback, void* user) = nullptr;

		/** Files with `extension` (".dialogue") open with `open` (double-click, Open). */
		void (*add_file_editor)(const char* plugin, const char* extension, OpenFileFn open, void* user) = nullptr;

		/**
		 * Content Browser > New file > `label`. `content` : text of the new
		 * file, {{NAME}} replaced by its name without extension.
		 */
		void (*add_new_file)(const char* plugin, const char* label, const char* default_name,
		                     const char* extension, const char* content) = nullptr;

		void (*add_tick)(const char* plugin, TickFn tick, void* user) = nullptr;

		/**
		 * Called before the level is saved or copied for Play (Ctrl+S, Play,
		 * Simulate) : put back what a preview changed in the actors.
		 */
		void (*add_before_level_snapshot)(const char* plugin, CallbackFn callback, void* user) = nullptr;

		// ---- Editor state ----------------------------------------------------
		Actor* (*get_selected_actor)() = nullptr;
		void (*select_actor)(Actor* actor) = nullptr;
		/** The level changed (asks to save). */
		void (*mark_level_dirty)() = nullptr;
		bool (*is_playing)() = nullptr;
		/** Absolute project folder / assets folder (UTF-8). */
		const char* (*project_root)() = nullptr;
		const char* (*assets_root)() = nullptr;
		/** Popup message (warning). */
		void (*message)(const char* text) = nullptr;
		/** Opens a file of assets/ like a double-click (relative or absolute). */
		void (*open_asset)(const char* path) = nullptr;
	};

	using StartupFn = void (*)(EditorAPI* api);
	using ShutdownFn = void (*)();
	constexpr const char* kStartup = "LynxEditorPlugin_Startup";
	constexpr const char* kShutdown = "LynxEditorPlugin_Shutdown";
}

#ifdef _WIN32
#define LYNX_EDITOR_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define LYNX_EDITOR_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define LYNX_EDITOR_PLUGIN_STARTUP(__api__) \
	LYNX_EDITOR_PLUGIN_EXPORT void LynxEditorPlugin_Startup(lynx::editor_api::EditorAPI* __api__)

#define LYNX_EDITOR_PLUGIN_SHUTDOWN() LYNX_EDITOR_PLUGIN_EXPORT void LynxEditorPlugin_Shutdown()

// The module draws in the editor's ImGui (its own ImGui code, same context).
#define LYNX_EDITOR_PLUGIN_INIT(__api__) \
	do { \
		ImGui::SetAllocatorFunctions((__api__)->imgui_alloc, (__api__)->imgui_free, (__api__)->imgui_alloc_user); \
		ImGui::SetCurrentContext((__api__)->imgui); \
	} while (0)
