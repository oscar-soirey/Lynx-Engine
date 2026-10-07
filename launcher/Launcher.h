#pragma once
#include "Dependencies.h"
#include "Engine.h"
#include "Models.h"
#include "PluginStore.h"

#include <imgui/imgui.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Launcher
{
public:
	Launcher();
	~Launcher();

	void Draw();   // une frame d'UI, sur toute la fenêtre

	// Fichiers / dossiers glissés sur la fenêtre (callback GLFW, chemins UTF-8) :
	// importés comme plugins dans la version choisie de l'onglet Plugins.
	void OnDrop(const std::vector<std::string>& paths);

private:
	enum class Page { Dependencies, Models, Engine, Plugins };

	// Travail en arrière-plan : un seul à la fois.
	bool Busy() const { return m_busy; }
	void RunTask(const std::string& label, std::function<void()> task);
	void Log(const std::string& line);

	void CheckAll();
	void InstallDependencies(std::vector<size_t> indices);
	void RefreshModels();
	void PullModels(std::vector<std::string> tags);
	void RemoveModel(const std::string& tag);
	void RefreshReleases();
	void InstallRelease(size_t index);
	void UninstallRelease(size_t index);

	void ScanPlugins();                                  // versions + plugins installés (sous m_mutex)
	void RefreshCatalog();
	void InstallPlugin(size_t catalog_index, const std::string& release_tag);
	void ImportPlugins(std::vector<std::filesystem::path> sources);
	void RemovePlugin(const InstalledPlugin& plugin);
	std::string PluginEngineTag() const;                 // version choisie ("" : aucune)
	std::string PluginEngineVersion() const;

	bool RequiredInstalled() const;   // à appeler sous m_mutex
	bool OllamaInstalled() const;     // à appeler sous m_mutex

	void DrawSidebar();
	void DrawDependencies();
	void DrawModels();
	void DrawEngine();
	void DrawReleaseCard(size_t index, const ImVec2& size);
	void DrawPlugins();
	void DrawPluginCard(const CatalogPlugin* catalog, size_t catalog_index,
	                    const InstalledPlugin* installed, const ImVec2& size);
	void DrawLog();

	Page m_page = Page::Dependencies;

	mutable std::mutex m_mutex;          // protège tout ce qui suit
	std::vector<Dependency> m_deps;
	std::vector<ModelInfo> m_models;
	std::vector<std::string> m_other_models;   // installés mais hors catalogue
	bool m_server_running = false;
	bool m_models_loaded = false;
	bool m_package_manager = false;
	std::string m_package_manager_info;
	std::vector<EngineRelease> m_releases;
	std::string m_releases_error;
	bool m_releases_loaded = false;
	// Plugins
	std::vector<InstalledEngine> m_engines;
	std::string m_plugin_engine;                  // tag de la version choisie
	std::vector<InstalledPlugin> m_installed_plugins;
	std::vector<CatalogPlugin> m_catalog;
	std::string m_catalog_error;
	bool m_catalog_loaded = false;
	bool m_plugins_scan = true;                   // relire les dossiers à la prochaine frame
	std::vector<std::filesystem::path> m_dropped; // glissés, en attente d'import
	char m_plugin_search[128] = "";
	std::string m_plugin_category;                // "" : toutes
	bool m_plugin_prerelease = false;             // proposer les pré-releases

	std::vector<std::string> m_log;
	std::string m_task_label;

	std::atomic<bool> m_busy{ false };
	std::thread m_worker;

	char m_custom_model[128] = "";
	bool m_scroll_log = false;
	bool m_show_log = true;
};
