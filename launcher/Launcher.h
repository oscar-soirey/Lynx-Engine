#pragma once
#include "Dependencies.h"
#include "Engine.h"
#include "Models.h"

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

private:
	enum class Page { Dependencies, Models, Engine };

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

	bool RequiredInstalled() const;   // à appeler sous m_mutex
	bool OllamaInstalled() const;     // à appeler sous m_mutex

	void DrawSidebar();
	void DrawDependencies();
	void DrawModels();
	void DrawEngine();
	void DrawReleaseCard(size_t index, const ImVec2& size);
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
	std::vector<std::string> m_log;
	std::string m_task_label;

	std::atomic<bool> m_busy{ false };
	std::thread m_worker;

	char m_custom_model[128] = "";
	bool m_scroll_log = false;
	bool m_show_log = true;
};
