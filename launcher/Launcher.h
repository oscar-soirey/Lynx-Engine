#pragma once
#include "Dependencies.h"
#include "Engine.h"
#include "Models.h"
#include "PluginStore.h"

#include <Raven.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

// Interface du launcher, en Raven (fenêtre interface/launcher.xml).
//
// Les tâches longues (winget, curl, ollama...) tournent sur un thread de travail,
// une à la fois, et modifient l'état sous m_mutex. L'interface est retenue (Raven) :
// Tick() (thread de l'UI, ~10 fois par seconde) reconstruit les listes de la page
// affichée quand l'état a changé (m_version), et met à jour les barres de
// progression, le journal et la barre latérale sans rien reconstruire.
class Launcher
{
public:
	explicit Launcher(Rid window);
	~Launcher();

	void Tick();

	// Fichiers / dossiers glissés sur la fenêtre (chemins UTF-8) :
	// importés comme plugins dans la version choisie de l'onglet Plugins.
	void OnDrop(const std::vector<std::string>& paths);

private:
	enum class Page { Dependencies = 0, Models, Engine, Plugins };

	// Travail en arrière-plan : un seul à la fois.
	bool Busy() const { return m_busy; }
	void RunTask(const std::string& label, std::function<void()> task);
	void Log(const std::string& line);
	void LogLocked(const std::string& line);   // m_mutex déjà tenu
	void Changed() { ++m_version; }            // l'interface se reconstruit au prochain Tick

	void CheckAll();
	void InstallDependencies(std::vector<size_t> indices);
	void RefreshModels();
	void PullModels(std::vector<std::string> tags);
	void RemoveModel(const std::string& tag);
	void RefreshReleases();
	void InstallRelease(size_t index);
	void UninstallRelease(size_t index);
	void AddLocalEngine();
	void RemoveLocalEngine(const std::string& tag);

	void ScanPlugins();                                  // versions + plugins installés (sous m_mutex)
	void RefreshCatalog();
	void InstallPlugin(size_t catalog_index, const std::string& release_tag);
	void ImportPlugins(std::vector<std::filesystem::path> sources);
	void RemovePlugin(const InstalledPlugin& plugin);
	std::string PluginEngineTag() const;                 // version choisie ("" : aucune)
	std::string PluginEngineVersion() const;
	std::string EngineLabel(const std::string& tag) const;

	bool RequiredInstalled() const;   // à appeler sous m_mutex
	bool OllamaInstalled() const;     // à appeler sous m_mutex

	// ---- interface (thread de l'UI, m_mutex tenu) ----
	void BindUi();
	void ShowPage(Page page);
	void UpdateSidebar();
	void UpdateLog();
	void UpdateProgress();
	void BuildDependencies();
	void BuildModels();
	void BuildEngine();
	void BuildPlugins();
	Rid W(const char* name) const;                       // un widget nommé de la fenêtre
	// Le clic d'un bouton, exécuté avec m_mutex tenu.
	void OnClick(Rid button, std::function<void()> fn);

	Rid m_window = R_INVALID_ID;
	Page m_page = Page::Dependencies;
	Page m_shown_page = Page::Plugins;   // != m_page : la première frame affiche la page
	std::atomic<std::uint64_t> m_version{ 1 };
	std::uint64_t m_built_version = 0;
	std::set<std::string> m_open_notes;   // notes de version dépliées (tags)
	bool m_log_visible = true;

	// Barres de progression des cartes, mises à jour sans reconstruire la page.
	std::map<std::string, Rid> m_release_bars;   // tag -> barre
	std::map<std::string, Rid> m_model_bars;     // tag du modèle -> barre
	std::map<std::string, Rid> m_plugin_bars;    // nom du plugin -> barre

	// Listes des combos (refaites seulement quand elles changent)
	std::vector<std::string> m_engine_items;
	std::vector<std::string> m_category_items;
	bool m_updating = false;   // combos remplis par le code : leurs callbacks sont ignorés

	mutable std::recursive_mutex m_mutex;   // protège tout ce qui suit (récursif : les callbacks de Raven peuvent être appelés pendant une mise à jour)
	std::vector<Dependency> m_deps;
	std::vector<ModelInfo> m_models;
	std::vector<std::string> m_other_models;   // installés mais hors catalogue
	bool m_server_running = false;
	bool m_models_loaded = false;
	bool m_package_manager = false;
	std::string m_package_manager_info;
	std::vector<EngineRelease> m_releases;
	std::vector<LocalEngine> m_local_engines;
	std::string m_releases_error;
	bool m_releases_loaded = false;
	// Plugins
	std::vector<InstalledEngine> m_engines;
	std::string m_plugin_engine;                  // tag de la version choisie
	std::vector<InstalledPlugin> m_installed_plugins;
	std::vector<CatalogPlugin> m_catalog;
	std::string m_catalog_error;
	bool m_catalog_loaded = false;
	bool m_plugins_scan = true;                   // relire les dossiers au prochain Tick
	std::vector<std::filesystem::path> m_dropped; // glissés, en attente d'import
	std::string m_plugin_search;
	std::string m_plugin_category;                // "" : toutes
	bool m_plugin_prerelease = false;             // proposer les pré-releases

	std::vector<std::string> m_log;
	std::uint64_t m_log_total = 0;                // lignes ajoutées depuis le début
	std::uint64_t m_log_shown = 0;                // lignes déjà dans la vue
	bool m_log_refill = false;                    // journal effacé / raccourci : tout réafficher
	std::string m_task_label;

	std::atomic<bool> m_busy{ false };
	std::thread m_worker;
};
