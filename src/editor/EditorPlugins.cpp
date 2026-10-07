#include "EditorPlugins.h"

#include "EditorPluginAPI.h"
#include "../core/Plugins.h"
#include "../host/GameProject.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace sfs = std::filesystem;

namespace lynx::editor::plugins
{
	namespace
	{
		struct Window
		{
			std::string plugin;
			std::string title;
			editor_api::DrawFn draw = nullptr;
			void* user = nullptr;
			bool open = false;
		};

		struct MenuItem
		{
			std::string plugin;
			std::string label;
			editor_api::CallbackFn callback = nullptr;
			void* user = nullptr;
		};

		struct FileEditor
		{
			std::string plugin;
			std::string extension;   // lower case, with the dot
			editor_api::OpenFileFn open = nullptr;
			void* user = nullptr;
		};

		struct TickEntry
		{
			std::string plugin;
			editor_api::TickFn tick = nullptr;
			void* user = nullptr;
		};

		struct EditorModule
		{
			std::string name;
#ifdef _WIN32
			HMODULE module = nullptr;
#endif
			editor_api::ShutdownFn shutdown = nullptr;
		};

		Host g_host;
		editor_api::EditorAPI g_api;
		std::vector<Window> g_windows;
		std::vector<MenuItem> g_menu;
		std::vector<FileEditor> g_file_editors;
		std::vector<TickEntry> g_ticks;
		std::vector<MenuItem> g_before_snapshot;
		std::vector<NewFileKind> g_new_files;
		std::vector<EditorModule> g_modules;

		// Strings handed to the plugins (must stay alive).
		std::string g_project_root_utf8;
		std::string g_assets_root_utf8;

		// Plugins window
		bool g_restart_needed = false;
		char g_new_name[64] = "MyPlugin";
		std::string g_new_message;

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		std::string Utf8(const sfs::path& p)
		{
			const auto u8 = p.u8string();
			return std::string(u8.begin(), u8.end());
		}

		// ---- API given to the plugins ---------------------------------------

		void ApiAddWindow(const char* plugin, const char* title, editor_api::DrawFn draw, void* user, bool open)
		{
			if (!title || !draw)
				return;
			for (Window& w : g_windows)
				if (w.title == title)
				{
					w.draw = draw;
					w.user = user;
					return;
				}
			g_windows.push_back({ plugin ? plugin : "", title, draw, user, open });
		}

		void ApiOpenWindow(const char* title)
		{
			for (Window& w : g_windows)
				if (title && w.title == title)
				{
					w.open = true;
					ImGui::SetWindowFocus(title);
				}
		}

		void ApiAddMenuItem(const char* plugin, const char* label, editor_api::CallbackFn cb, void* user)
		{
			if (label && cb)
				g_menu.push_back({ plugin ? plugin : "", label, cb, user });
		}

		void ApiAddFileEditor(const char* plugin, const char* extension, editor_api::OpenFileFn open, void* user)
		{
			if (!extension || !open)
				return;
			std::string ext = Lower(extension);
			if (!ext.empty() && ext[0] != '.')
				ext = "." + ext;
			g_file_editors.push_back({ plugin ? plugin : "", ext, open, user });
		}

		void ApiAddNewFile(const char* plugin, const char* label, const char* default_name,
		                   const char* extension, const char* content)
		{
			if (!label || !extension)
				return;
			g_new_files.push_back({ plugin ? plugin : "", label, default_name ? default_name : "NewFile",
			                        extension, content ? content : "" });
		}

		void ApiAddTick(const char* plugin, editor_api::TickFn tick, void* user)
		{
			if (tick)
				g_ticks.push_back({ plugin ? plugin : "", tick, user });
		}

		void ApiBeforeSnapshot(const char* plugin, editor_api::CallbackFn cb, void* user)
		{
			if (cb)
				g_before_snapshot.push_back({ plugin ? plugin : "", "", cb, user });
		}

		Actor* ApiSelected() { return g_host.get_selected_actor ? g_host.get_selected_actor() : nullptr; }
		void ApiSelect(Actor* a) { if (g_host.select_actor) g_host.select_actor(a); }
		void ApiDirty() { if (g_host.mark_level_dirty) g_host.mark_level_dirty(); }
		bool ApiPlaying() { return g_host.is_playing ? g_host.is_playing() : false; }
		const char* ApiProjectRoot() { return g_project_root_utf8.c_str(); }
		const char* ApiAssetsRoot() { return g_assets_root_utf8.c_str(); }
		void ApiMessage(const char* text) { if (g_host.message && text) g_host.message(text); }
		void ApiOpenAsset(const char* path)
		{
			if (!path || !g_host.open_asset)
				return;
			sfs::path p = sfs::u8path(path);
			if (p.is_relative())
				p = g_host.project_root / "assets" / p;
			g_host.open_asset(p);
		}

		// ---- New plugin (project) --------------------------------------------

		bool ValidIdentifier(const std::string& name)
		{
			if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0])))
				return false;
			return std::all_of(name.begin(), name.end(),
			                   [](unsigned char c) { return std::isalnum(c) || c == '_'; });
		}

		bool WriteText(const sfs::path& file, const std::string& text)
		{
			std::error_code ec;
			sfs::create_directories(file.parent_path(), ec);
			std::ofstream out(file, std::ios::binary | std::ios::trunc);
			if (!out)
				return false;
			out << text;
			return true;
		}

		std::string Replace(std::string text, const std::string& name)
		{
			for (size_t pos = 0; (pos = text.find("{{NAME}}", pos)) != std::string::npos;)
			{
				text.replace(pos, 8, name);
				pos += name.size();
			}
			return text;
		}

		bool CreateProjectPlugin(const std::string& name, std::string& message)
		{
			if (!ValidIdentifier(name))
			{
				message = "The name must be an identifier (letters, digits, _).";
				return false;
			}

			const sfs::path folder = g_host.project_root / "plugins" / name;
			std::error_code ec;
			if (sfs::exists(folder, ec))
			{
				message = "plugins/" + name + " already exists.";
				return false;
			}

			const char* plugin_json =
				"{\n"
				"  \"name\": \"{{NAME}}\",\n"
				"  \"version\": \"1.0\",\n"
				"  \"description\": \"\",\n"
				"  \"author\": \"\",\n"
				"  \"category\": \"Project\",\n"
				"  \"runtime\": \"bin/{{NAME}}.dll\",\n"
				"  \"editor\": \"bin/{{NAME}}.Editor.dll\"\n"
				"}\n";

			const char* cmake =
				"# Plugin {{NAME}} : built with the project (add_subdirectory in its CMakeLists.txt).\n"
				"# lynx_add_plugin comes from the engine : include(\"${Lynx_DIR}/LynxPlugin.cmake\").\n"
				"if(NOT COMMAND lynx_add_plugin)\n"
				"\tinclude(\"${Lynx_DIR}/LynxPlugin.cmake\")\n"
				"endif()\n"
				"\n"
				"lynx_add_plugin({{NAME}}\n"
				"\tRUNTIME Source/{{NAME}}.cpp\n"
				"\tEDITOR  Source/{{NAME}}Editor.cpp\n"
				")\n";

			const char* runtime =
				"// Runtime module of the plugin {{NAME}} : loaded by the editor and the game.\n"
				"#include <plugins/LynxPlugin.h>\n"
				"\n"
				"LYNX_PLUGIN({{NAME}})\n"
				"\n"
				"// Classes of the plugin (actors, widgets...) :\n"
				"// LYNX_LINK_MODULE(\n"
				"//     LYNX_MODULE_REGISTER(MyActor);\n"
				"// )\n"
				"\n"
				"LYNX_PLUGIN_STARTUP()\n"
				"{\n"
				"\t// JS : {{NAME}}.hello()\n"
				"\tlynx::RegisterScriptFunction(\"{{NAME}}\", \"hello\", [](const lynx::InterfaceArgs&) -> lynx::InterfaceArg\n"
				"\t{\n"
				"\t\treturn std::string(\"Hello from {{NAME}}\");\n"
				"\t});\n"
				"}\n"
				"\n"
				"LYNX_PLUGIN_SHUTDOWN()\n"
				"{\n"
				"}\n";

			const char* editor =
				"// Editor module of the plugin {{NAME}} : the editor only (never shipped).\n"
				"#include <editor/EditorPluginAPI.h>\n"
				"\n"
				"static void DrawWindow(bool* open, void*)\n"
				"{\n"
				"\tif (ImGui::Begin(\"{{NAME}}\", open))\n"
				"\t\tImGui::TextUnformatted(\"Window of the plugin {{NAME}}.\");\n"
				"\tImGui::End();\n"
				"}\n"
				"\n"
				"LYNX_EDITOR_PLUGIN_STARTUP(api)\n"
				"{\n"
				"\tLYNX_EDITOR_PLUGIN_INIT(api);\n"
				"\tapi->add_window(\"{{NAME}}\", \"{{NAME}}\", DrawWindow, nullptr, false);\n"
				"}\n"
				"\n"
				"LYNX_EDITOR_PLUGIN_SHUTDOWN()\n"
				"{\n"
				"}\n";

			bool ok = WriteText(folder / "plugin.json", Replace(plugin_json, name)) &&
			          WriteText(folder / "CMakeLists.txt", Replace(cmake, name)) &&
			          WriteText(folder / "Source" / (name + ".cpp"), Replace(runtime, name)) &&
			          WriteText(folder / "Source" / (name + "Editor.cpp"), Replace(editor, name));
			if (!ok)
			{
				message = "Could not write the files of plugins/" + name;
				return false;
			}

			// The project builds it : add_subdirectory in its CMakeLists.txt.
			const sfs::path project_cmake = g_host.project_root / "CMakeLists.txt";
			std::string added;
			if (sfs::is_regular_file(project_cmake, ec))
			{
				std::ifstream in(project_cmake, std::ios::binary);
				std::stringstream text;
				text << in.rdbuf();
				in.close();
				const std::string line = "add_subdirectory(plugins/" + name + ")";
				if (text.str().find(line) == std::string::npos)
				{
					std::ofstream out(project_cmake, std::ios::binary | std::ios::app);
					out << "\n# Plugin " << name << " (created by the editor)\n" << line << "\n";
					added = " CMakeLists.txt of the project : add_subdirectory added.";
				}
			}
			else
			{
				added = " This project has no CMakeLists.txt : build the plugin with its own CMakeLists.txt.";
			}

			message = "plugins/" + name + " created. Compile the project, then restart the editor." + added;
			return true;
		}
	}

	void SetHost(const Host& host)
	{
		g_host = host;
		g_project_root_utf8 = Utf8(host.project_root);
		g_assets_root_utf8 = Utf8(host.project_root / "assets");
	}

	void LoadEditorModules()
	{
		g_api = editor_api::EditorAPI{};
		g_api.imgui = ImGui::GetCurrentContext();
		ImGui::GetAllocatorFunctions(&g_api.imgui_alloc, &g_api.imgui_free, &g_api.imgui_alloc_user);
		g_api.add_window = ApiAddWindow;
		g_api.open_window = ApiOpenWindow;
		g_api.add_menu_item = ApiAddMenuItem;
		g_api.add_file_editor = ApiAddFileEditor;
		g_api.add_new_file = ApiAddNewFile;
		g_api.add_tick = ApiAddTick;
		g_api.add_before_level_snapshot = ApiBeforeSnapshot;
		g_api.get_selected_actor = ApiSelected;
		g_api.select_actor = ApiSelect;
		g_api.mark_level_dirty = ApiDirty;
		g_api.is_playing = ApiPlaying;
		g_api.project_root = ApiProjectRoot;
		g_api.assets_root = ApiAssetsRoot;
		g_api.message = ApiMessage;
		g_api.open_asset = ApiOpenAsset;

#ifdef _WIN32
		for (lynx::plugins::PluginInfo& p : lynx::plugins::GetPlugins())
		{
			// The editor module goes with a loaded runtime module (or a
			// plugin made of an editor module only).
			if (!p.enabled || p.editor_dll.empty() || (!p.loaded && !p.runtime_dll.empty()))
				continue;

			std::error_code ec;
			if (!sfs::is_regular_file(p.editor_dll, ec))
			{
				std::cout << "[PLUGINS] " << p.name << " : editor module not built (" << p.editor_dll.string() << ")\n";
				continue;
			}

			HMODULE module = LoadLibraryExW(p.editor_dll.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (!module)
			{
				p.error = "could not load the editor module " + p.editor_dll.string();
				std::cout << "[PLUGINS] " << p.name << " : " << p.error << "\n";
				continue;
			}

			const auto startup = reinterpret_cast<editor_api::StartupFn>(GetProcAddress(module, editor_api::kStartup));
			const auto shutdown = reinterpret_cast<editor_api::ShutdownFn>(GetProcAddress(module, editor_api::kShutdown));
			if (!startup)
			{
				p.error = "the editor module has no LYNX_EDITOR_PLUGIN_STARTUP";
				std::cout << "[PLUGINS] " << p.name << " : " << p.error << "\n";
				FreeLibrary(module);
				continue;
			}

			startup(&g_api);
			g_modules.push_back({ p.name, module, shutdown });
			std::cout << "[PLUGINS] " << p.name << " : editor module loaded\n";
		}
#endif
	}

	void UnloadEditorModules()
	{
		for (auto it = g_modules.rbegin(); it != g_modules.rend(); ++it)
		{
			if (it->shutdown)
				it->shutdown();
#ifdef _WIN32
			if (it->module)
				FreeLibrary(it->module);
#endif
		}
		g_modules.clear();
		g_windows.clear();
		g_menu.clear();
		g_file_editors.clear();
		g_ticks.clear();
		g_new_files.clear();
		g_before_snapshot.clear();
	}

	void BeforeLevelSnapshot()
	{
		for (size_t i = 0; i < g_before_snapshot.size(); ++i)
			g_before_snapshot[i].callback(g_before_snapshot[i].user);
	}

	void Tick(float dt)
	{
		for (size_t i = 0; i < g_ticks.size(); ++i)
			g_ticks[i].tick(dt, g_ticks[i].user);
	}

	void DrawWindows()
	{
		for (size_t i = 0; i < g_windows.size(); ++i)
		{
			Window& w = g_windows[i];
			if (w.open)
				w.draw(&w.open, w.user);
		}
	}

	void DrawWindowsMenuItems()
	{
		if (g_windows.empty() && g_menu.empty())
			return;
		ImGui::Separator();
		ImGui::TextDisabled("Plugins");
		for (Window& w : g_windows)
			ImGui::Checkbox(w.title.c_str(), &w.open);

		// Menu items of the plugins (actions).
		for (size_t i = 0; i < g_menu.size(); ++i)
		{
			const MenuItem item = g_menu[i];
			if (ImGui::MenuItem(item.label.c_str()))
				item.callback(item.user);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", item.plugin.c_str());
		}
	}

	bool HasMenuItems()
	{
		return !g_menu.empty();
	}

	void DrawPluginsMenu(bool* show_manager)
	{
		if (ImGui::MenuItem("Plugins...") && show_manager)
			*show_manager = true;

		if (!g_menu.empty())
			ImGui::Separator();

		std::string plugin;
		for (size_t i = 0; i < g_menu.size(); ++i)
		{
			const MenuItem item = g_menu[i];
			if (item.plugin != plugin)
			{
				plugin = item.plugin;
				ImGui::TextDisabled("%s", plugin.c_str());
			}
			if (ImGui::MenuItem(item.label.c_str()))
				item.callback(item.user);
		}

		if (!g_windows.empty())
		{
			ImGui::Separator();
			for (Window& w : g_windows)
				if (ImGui::MenuItem(w.title.c_str(), nullptr, w.open))
					w.open = !w.open;
		}
	}

	bool CanOpen(const sfs::path& path)
	{
		const std::string ext = Lower(path.extension().string());
		return std::any_of(g_file_editors.begin(), g_file_editors.end(),
		                   [&](const FileEditor& f) { return f.extension == ext; });
	}

	bool OpenFile(const sfs::path& path)
	{
		const std::string ext = Lower(path.extension().string());
		const std::string utf8 = Utf8(path);
		for (const FileEditor& f : g_file_editors)
			if (f.extension == ext && f.open(utf8.c_str(), f.user))
				return true;
		return false;
	}

	const std::vector<NewFileKind>& GetNewFileKinds()
	{
		return g_new_files;
	}

	void DrawManager(bool* open)
	{
		ImGui::SetNextWindowSize(ImVec2(760.f, 480.f), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Plugins", open))
			DrawManagerContent();
		ImGui::End();
	}

	void DrawManagerContent()
	{
		ImGui::SeparatorText("Plugins");
		ImGui::TextDisabled("Engine plugins : <editor>/plugins ; project plugins : <project>/plugins.");

		auto& list = lynx::plugins::GetPlugins();

		if (g_restart_needed)
		{
			ImGui::TextColored(ImVec4(1.f, 0.75f, 0.3f, 1.f), "Restart the editor to apply the changes.");
			ImGui::Separator();
		}

		if (list.empty())
			ImGui::TextDisabled("No plugin found (<editor>/plugins and <project>/plugins).");

		if (ImGui::BeginTable("##plugins", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
		                                        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
		                      ImVec2(0.f, ImGui::GetFrameHeightWithSpacing() * 9.f)))
		{
			ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 34.f);
			ImGui::TableSetupColumn("Plugin", ImGuiTableColumnFlags_WidthFixed, 170.f);
			ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthFixed, 70.f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 150.f);
			ImGui::TableHeadersRow();

			for (lynx::plugins::PluginInfo& p : list)
			{
				ImGui::PushID(p.name.c_str());
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				bool enabled = p.enabled;
				if (ImGui::Checkbox("##on", &enabled))
				{
					lynx::plugins::SetEnabled(p.name, enabled);
					lynx::plugins::SaveSettings();
					g_restart_needed = true;
				}

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(p.name.c_str());
				if (!p.version.empty())
				{
					ImGui::SameLine();
					ImGui::TextDisabled("%s", p.version.c_str());
				}
				if (ImGui::IsItemHovered() || ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", p.folder.string().c_str());

				ImGui::TableNextColumn();
				ImGui::TextWrapped("%s", p.description.c_str());
				if (!p.author.empty())
					ImGui::TextDisabled("by %s", p.author.c_str());

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(p.engine_plugin ? "Engine" : "Project");

				ImGui::TableNextColumn();
				if (!p.error.empty())
				{
					ImGui::TextColored(ImVec4(1.f, 0.4f, 0.35f, 1.f), "Error");
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", p.error.c_str());
				}
				else if (p.loaded)
					ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.f), "Loaded");
				else if (p.enabled)
					ImGui::TextDisabled("Enabled (restart)");
				else
					ImGui::TextDisabled("Off");

				if (ImGui::SmallButton("Folder"))
				{
#ifdef _WIN32
					ShellExecuteW(nullptr, L"open", p.folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
				}

				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		ImGui::Separator();
		ImGui::TextUnformatted("New project plugin");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(200.f);
		ImGui::InputText("##newplugin", g_new_name, sizeof(g_new_name));
		ImGui::SameLine();
		if (ImGui::Button("Create"))
			CreateProjectPlugin(g_new_name, g_new_message);
		if (!g_new_message.empty())
			ImGui::TextWrapped("%s", g_new_message.c_str());

		// Tools of the loaded plugins : their windows and their menu items.
		if (!g_windows.empty() || !g_menu.empty())
		{
			ImGui::SeparatorText("Tools of the plugins");
			ImGui::TextDisabled("Also in the toolbar : Windows.");
			for (Window& w : g_windows)
				ImGui::Checkbox(w.title.c_str(), &w.open);
			std::string plugin;
			for (size_t i = 0; i < g_menu.size(); ++i)
			{
				const MenuItem item = g_menu[i];
				if (item.plugin != plugin)
				{
					plugin = item.plugin;
					ImGui::TextDisabled("%s", plugin.c_str());
				}
				ImGui::PushID(static_cast<int>(i));
				if (ImGui::Button(item.label.c_str()))
					item.callback(item.user);
				ImGui::PopID();
			}
		}
	}
}
