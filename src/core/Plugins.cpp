#include "Plugins.h"

#include "Engine.h"
#include "Private/SystemModule.h"
#include "GameModuleAPI.h"
#include "../plugins/LynxPlugin.h"

#include <json/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <memory>

namespace sfs = std::filesystem;

namespace lynx::plugins
{
	namespace
	{
		struct Loaded
		{
			std::string name;
			std::unique_ptr<SysModule> module;
			game_api::Hooks hooks;
			StartupFn startup = nullptr;
			ShutdownFn shutdown = nullptr;
			TickFn tick = nullptr;
		};

		std::vector<PluginInfo> g_plugins;
		std::vector<Loaded> g_loaded;
		sfs::path g_settings_file;
		// plugins.json as read (names not found are kept).
		nlohmann::json g_settings = nlohmann::json::object();

		nlohmann::json ReadJson(const sfs::path& file)
		{
			std::ifstream in(file, std::ios::binary);
			if (!in)
				return nlohmann::json();
			return nlohmann::json::parse(in, nullptr, false);
		}

		std::string Str(const nlohmann::json& j, const char* key)
		{
			const auto it = j.find(key);
			return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
		}

		void Scan(const sfs::path& dir, bool engine)
		{
			std::error_code ec;
			if (dir.empty() || !sfs::is_directory(dir, ec))
				return;

			for (const auto& entry : sfs::directory_iterator(dir, ec))
			{
				std::error_code e;
				if (!entry.is_directory(e))
					continue;

				const sfs::path desc = entry.path() / "plugin.json";
				if (!sfs::is_regular_file(desc, e))
					continue;

				const nlohmann::json j = ReadJson(desc);
				if (!j.is_object())
				{
					std::cout << "[PLUGINS] " << desc.string() << " : invalid JSON\n";
					continue;
				}

				PluginInfo info;
				info.name = Str(j, "name");
				if (info.name.empty())
					info.name = entry.path().filename().string();
				info.version = Str(j, "version");
				info.description = Str(j, "description");
				info.author = Str(j, "author");
				info.category = Str(j, "category");
				info.folder = sfs::absolute(entry.path(), e).lexically_normal();
				info.engine_plugin = engine;

				// Installed by the launcher : version of the engine the release was made for.
				const nlohmann::json install = ReadJson(entry.path() / ".lynx-install.json");
				if (install.is_object())
					info.engine_min = Str(install, "engine_min");
				if (info.engine_min.empty())
					info.engine_min = Str(j, "engine_min");

				const std::string runtime = Str(j, "runtime");
				const std::string editor = Str(j, "editor");
				if (!runtime.empty())
					info.runtime_dll = (info.folder / runtime).lexically_normal();
				if (!editor.empty())
					info.editor_dll = (info.folder / editor).lexically_normal();

				// A project plugin hides an engine plugin of the same name.
				auto same = std::find_if(g_plugins.begin(), g_plugins.end(),
				                         [&](const PluginInfo& p) { return p.name == info.name; });
				if (same != g_plugins.end())
				{
					if (!engine)
						*same = info;
					continue;
				}
				g_plugins.push_back(std::move(info));
			}
		}
	}

	void Discover(const sfs::path& engine_dir, const sfs::path& project_dir, const sfs::path& project_root)
	{
		g_plugins.clear();
		Scan(engine_dir, true);
		Scan(project_dir, false);

		g_settings_file = project_root.empty() ? sfs::path() : project_root / "plugins.json";
		g_settings = nlohmann::json::object();
		if (!g_settings_file.empty())
		{
			const nlohmann::json j = ReadJson(g_settings_file);
			if (j.is_object())
				g_settings = j;
		}

		for (PluginInfo& p : g_plugins)
		{
			const auto it = g_settings.find(p.name);
			// Engine plugins : off unless asked ; project plugins : on unless refused.
			p.enabled = it != g_settings.end() && it->is_boolean() ? it->get<bool>() : !p.engine_plugin;
		}

		std::sort(g_plugins.begin(), g_plugins.end(),
		          [](const PluginInfo& a, const PluginInfo& b) { return a.name < b.name; });

		std::cout << "[PLUGINS] " << g_plugins.size() << " plugin(s) found\n";
	}

	int CompareVersions(const std::string& a, const std::string& b)
	{
		size_t i = 0, j = 0;
		while (i < a.size() || j < b.size())
		{
			long x = 0, y = 0;
			while (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i])))
				x = x * 10 + (a[i++] - '0');
			while (j < b.size() && std::isdigit(static_cast<unsigned char>(b[j])))
				y = y * 10 + (b[j++] - '0');
			if (x != y)
				return x < y ? -1 : 1;
			if (i < a.size()) ++i;
			if (j < b.size()) ++j;
		}
		return 0;
	}

	std::vector<PluginInfo>& GetPlugins()
	{
		return g_plugins;
	}

	PluginInfo* FindPlugin(const std::string& name)
	{
		for (PluginInfo& p : g_plugins)
			if (p.name == name)
				return &p;
		return nullptr;
	}

	void SetEnabled(const std::string& name, bool enabled)
	{
		if (PluginInfo* p = FindPlugin(name))
			p->enabled = enabled;
		g_settings[name] = enabled;
	}

	bool SaveSettings()
	{
		if (g_settings_file.empty())
			return false;
		for (const PluginInfo& p : g_plugins)
			g_settings[p.name] = p.enabled;
		std::ofstream out(g_settings_file, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out << g_settings.dump(2) << "\n";
		return true;
	}

	int LoadRuntimeModules()
	{
		int count = 0;

		for (PluginInfo& p : g_plugins)
		{
			p.loaded = false;
			p.error.clear();

			if (!p.enabled || p.runtime_dll.empty())
				continue;

			if (std::any_of(g_loaded.begin(), g_loaded.end(), [&](const Loaded& l) { return l.name == p.name; }))
			{
				p.loaded = true;
				continue;
			}

			std::error_code ec;
			if (!sfs::is_regular_file(p.runtime_dll, ec))
			{
				p.error = "runtime module not built : " + p.runtime_dll.string();
				std::cout << "[PLUGINS] " << p.name << " : " << p.error << "\n";
				continue;
			}

			Loaded loaded;
			loaded.name = p.name;
			loaded.module = std::make_unique<SysModule>(p.runtime_dll.string().c_str());

			if (!loaded.module->IsLoaded())
			{
				p.error = "could not load " + p.runtime_dll.string();
				std::cout << "[PLUGINS] " << p.name << " : " << p.error << "\n";
				continue;
			}

			const auto info_fn = reinterpret_cast<InfoFn>(loaded.module->GetSymbol(kInfo));
			const ModuleInfo* info = info_fn ? info_fn() : nullptr;
			if (!info || info->api_version != kApiVersion)
			{
				p.error = info ? "built for another plugin API (" + std::to_string(info->api_version) +
				                     ", engine : " + std::to_string(kApiVersion) + ") : rebuild it"
				               : "not a Lynx plugin (no LYNX_PLUGIN in the DLL)";
				std::cout << "[PLUGINS] " << p.name << " : " << p.error << "\n";
				continue;
			}

			loaded.module->RegisterFactory();
			loaded.hooks.Load(*loaded.module);
			loaded.startup = reinterpret_cast<StartupFn>(loaded.module->GetSymbol(kStartup));
			loaded.shutdown = reinterpret_cast<ShutdownFn>(loaded.module->GetSymbol(kShutdown));
			loaded.tick = reinterpret_cast<TickFn>(loaded.module->GetSymbol(kTick));

			if (loaded.startup)
				loaded.startup();

			g_loaded.push_back(std::move(loaded));
			p.loaded = true;
			++count;
			std::cout << "[PLUGINS] " << p.name << " loaded\n";
		}

		return count;
	}

	void UnloadAll()
	{
		// Reverse order : a plugin may use another one loaded before it.
		for (auto it = g_loaded.rbegin(); it != g_loaded.rend(); ++it)
		{
			if (it->shutdown)
				it->shutdown();
			it->hooks.Clear();
			it->module.reset();   // deletes the level first (see SysModule::Unload)
		}
		g_loaded.clear();
		for (PluginInfo& p : g_plugins)
			p.loaded = false;
	}

	sfs::path PluginAssetPath(const std::string& plugin, const std::string& relative)
	{
		const PluginInfo* p = FindPlugin(plugin);
		return p ? (p->folder / relative).lexically_normal() : sfs::path();
	}

	void SetupScene(uint32_t scene)
	{
		for (Loaded& l : g_loaded)
			if (l.hooks.setup_scene)
				l.hooks.setup_scene(scene);
	}

	void OnGameStart()
	{
		for (Loaded& l : g_loaded)
			if (l.hooks.on_game_start)
				l.hooks.on_game_start();
	}

	void OnGameEnd()
	{
		for (Loaded& l : g_loaded)
			if (l.hooks.on_game_end)
				l.hooks.on_game_end();
	}

	void OnLevelLoaded()
	{
		for (Loaded& l : g_loaded)
			if (l.hooks.on_level_loaded)
				l.hooks.on_level_loaded(0xFFFFFFFFu);
	}

	void Tick(float dt, bool playing)
	{
		for (Loaded& l : g_loaded)
			if (l.tick)
				l.tick(dt, playing ? 1 : 0);
	}
}
