#pragma once

// =============================================================================
// Editor side of the plugins : loads the editor modules (<Name>.Editor.dll) of
// the enabled plugins, keeps what they register (windows, menus, file
// editors, new files) and draws the Plugins window (enable / disable, new
// plugin). See core/Plugins.h and editor/EditorPluginAPI.h.
// =============================================================================

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace lynx
{
	class Actor;
}

namespace lynx::editor::plugins
{
	/** What the editor gives to the plugins (set before LoadEditorModules). */
	struct Host
	{
		std::function<Actor*()> get_selected_actor;
		std::function<void(Actor*)> select_actor;
		std::function<void()> mark_level_dirty;
		std::function<bool()> is_playing;
		std::function<void(const std::string&)> message;
		std::function<void(const std::filesystem::path&)> open_asset;
		std::filesystem::path project_root;
	};

	void SetHost(const Host& host);

	/** After ImGui is created and the runtime modules are loaded. */
	void LoadEditorModules();
	void UnloadEditorModules();

	/** Before Save / Play : the plugins undo their previews. */
	void BeforeLevelSnapshot();

	/** Every frame : plugin ticks and their open windows. */
	void Tick(float dt);
	void DrawWindows();
	/** A window of a plugin has the keyboard focus (the editor shortcuts stay off). */
	bool HasFocus();

	/** Windows menu : one checkbox per plugin window. */
	void DrawWindowsMenuItems();
	/** Toolbar "Plugins" menu content (items of the plugins, Plugins window). */
	void DrawPluginsMenu(bool* show_manager);
	bool HasMenuItems();

	/** Double-click : a plugin editor for this file ? true : opened. */
	bool OpenFile(const std::filesystem::path& path);
	bool CanOpen(const std::filesystem::path& path);

	struct NewFileKind
	{
		std::string plugin;
		std::string label;
		std::string default_name;
		std::string extension;
		std::string content;   // {{NAME}}
	};
	const std::vector<NewFileKind>& GetNewFileKinds();

	/** Window "Plugins" : list, enable, new plugin. */
	void DrawManager(bool* open);

	/** Same content, inside another window (Options > Plugins) : list, tools, new plugin. */
	void DrawManagerContent();
}
