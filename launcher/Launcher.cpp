#include "Launcher.h"
#include "Process.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>   // DC.CurrLineTextBaseOffset (alignement des pastilles)

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace
{
	const ImVec4 kAccent   = ImVec4(0.96f, 0.56f, 0.20f, 1.0f);   // orange « lynx »
	const ImVec4 kGreen    = ImVec4(0.40f, 0.80f, 0.45f, 1.0f);
	const ImVec4 kRed      = ImVec4(0.92f, 0.38f, 0.36f, 1.0f);
	const ImVec4 kYellow   = ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
	const ImVec4 kMuted    = ImVec4(0.58f, 0.60f, 0.66f, 1.0f);

	ImVec4 StateColor(DepState state)
	{
		switch (state)
		{
		case DepState::Installed:  return kGreen;
		case DepState::Missing:
		case DepState::Failed:     return kRed;
		case DepState::Installing:
		case DepState::Checking:   return kYellow;
		default:                   return kMuted;
		}
	}

	// Petite pastille de couleur suivie d'un texte.
	void StatusDot(const ImVec4& color)
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		p.y += ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
		float h = ImGui::GetTextLineHeight();
		float r = h * 0.22f;   // proportionnel à la police
		ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + h * 0.5f), r,
		                                           ImGui::ColorConvertFloat4ToU32(color));
		ImGui::Dummy(ImVec2(r * 2.0f + h * 0.3f, h));
		ImGui::SameLine();
	}

	bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0))
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x * 0.85f, kAccent.y * 0.85f, kAccent.z * 0.85f, 1));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(kAccent.x * 0.7f, kAccent.y * 0.7f, kAccent.z * 0.7f, 1));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.10f, 1));
		bool pressed = ImGui::Button(label, size);
		ImGui::PopStyleColor(4);
		return pressed;
	}

	void SectionTitle(const char* title, const char* subtitle)
	{
		ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);   // ImGui 1.92 : taille dynamique
		ImGui::TextUnformatted(title);
		ImGui::PopFont();
		ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
		ImGui::TextWrapped("%s", subtitle);
		ImGui::PopStyleColor();
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
	}
}

Launcher::Launcher()
{
	m_deps = Dependencies::CreateList();
	m_models = Models::Catalog();
	for (ModelInfo& model : m_models)
		model.selected = model.recommended;
	CheckAll();
}

Launcher::~Launcher()
{
	// Une installation peut durer : on ne bloque pas la fermeture. Le thread
	// utilise `this`, donc on quitte le processus tout de suite (winget / ollama
	// continuent de leur côté).
	if (m_busy)
		std::_Exit(0);
	if (m_worker.joinable())
		m_worker.join();
}

// -----------------------------------------------------------------------------
// Tâches
// -----------------------------------------------------------------------------

void Launcher::Log(const std::string& line)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_log.push_back(line);
	if (m_log.size() > 5000)
		m_log.erase(m_log.begin(), m_log.begin() + 1000);
	m_scroll_log = true;
}

void Launcher::RunTask(const std::string& label, std::function<void()> task)
{
	if (m_busy.exchange(true))
		return;
	if (m_worker.joinable())
		m_worker.join();
	// Appelé depuis Draw (m_mutex déjà tenu) ou le constructeur : pas de verrou ici.
	m_task_label = label;
	m_worker = std::thread([this, task = std::move(task)] {
		task();
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_task_label.clear();
		}
		m_busy = false;
	});
}

void Launcher::CheckAll()
{
	RunTask("Vérification des dépendances", [this] {
		std::string info;
		bool available = Dependencies::PackageManagerAvailable(&info);

		std::vector<Dependency> deps;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_package_manager = available;
			m_package_manager_info = info;
			for (Dependency& dep : m_deps)
				dep.state = DepState::Checking;
			deps = m_deps;
		}
		for (size_t i = 0; i < deps.size(); ++i)
		{
			Dependencies::Check(deps[i]);
			std::lock_guard<std::mutex> lock(m_mutex);
			m_deps[i].state = deps[i].state;
			m_deps[i].version = deps[i].version;
		}
		Log("Vérification terminée.");
	});
	// La liste des modèles est rafraîchie à la demande depuis l'onglet Modèles.
}

void Launcher::InstallDependencies(std::vector<size_t> indices)
{
	RunTask("Installation des dépendances", [this, indices] {
		int failures = 0;
		for (size_t index : indices)
		{
			Dependency dep;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_deps[index].state = DepState::Installing;
				dep = m_deps[index];
			}
			bool ok = Dependencies::Install(dep, [this](const std::string& l) { Log(l); });
			failures += ok ? 0 : 1;
			std::lock_guard<std::mutex> lock(m_mutex);
			m_deps[index].state = dep.state;
			m_deps[index].version = dep.version;
		}
		Log(failures == 0 ? "Toutes les dépendances demandées sont installées."
		                  : std::to_string(failures) + " installation(s) en échec, voir ci-dessus.");
	});
}

void Launcher::RefreshModels()
{
	RunTask("Recherche des modèles installés", [this] {
		bool running = Models::EnsureServer([this](const std::string& l) { Log(l); });
		std::vector<std::string> installed = running ? Models::ListInstalled() : std::vector<std::string>{};
		std::lock_guard<std::mutex> lock(m_mutex);
		m_server_running = running;
		m_models_loaded = true;
		m_other_models.clear();
		for (ModelInfo& model : m_models)
			model.installed = false;
		for (const std::string& name : installed)
		{
			bool known = false;
			for (ModelInfo& model : m_models)
				if (Models::SameModel(model.tag, name))
				{
					model.installed = known = true;
					model.selected = false;
				}
			if (!known)
				m_other_models.push_back(name);
		}
	});
}

void Launcher::PullModels(std::vector<std::string> tags)
{
	RunTask("Téléchargement des modèles", [this, tags] {
		for (const std::string& tag : tags)
		{
			auto set_progress = [this, &tag](float p) {
				std::lock_guard<std::mutex> lock(m_mutex);
				for (ModelInfo& model : m_models)
					if (model.tag == tag)
						model.progress = p;
				m_task_label = "Téléchargement de " + tag + " (" + std::to_string(int(p * 100)) + " %)";
			};
			set_progress(0.0f);
			bool ok = Models::Pull(tag, [this](const std::string& l) { Log(l); }, set_progress);
			std::lock_guard<std::mutex> lock(m_mutex);
			for (ModelInfo& model : m_models)
				if (model.tag == tag)
				{
					model.progress = -1.0f;
					model.installed = ok;
					if (ok)
						model.selected = false;
				}
			if (ok && std::none_of(m_models.begin(), m_models.end(),
			                       [&](const ModelInfo& m) { return m.tag == tag; }) &&
			    std::find(m_other_models.begin(), m_other_models.end(), tag) == m_other_models.end())
				m_other_models.push_back(tag);
		}
	});
}

void Launcher::RemoveModel(const std::string& tag)
{
	RunTask("Suppression de " + tag, [this, tag] {
		if (!Models::Remove(tag, [this](const std::string& l) { Log(l); }))
			return;
		Log(tag + " supprimé.");
		std::lock_guard<std::mutex> lock(m_mutex);
		for (ModelInfo& model : m_models)
			if (Models::SameModel(model.tag, tag))
				model.installed = false;
		m_other_models.erase(std::remove(m_other_models.begin(), m_other_models.end(), tag), m_other_models.end());
	});
}

bool Launcher::RequiredInstalled() const
{
	return std::all_of(m_deps.begin(), m_deps.end(), [](const Dependency& d) {
		return !d.required || d.state == DepState::Installed;
	});
}

bool Launcher::OllamaInstalled() const
{
	for (const Dependency& dep : m_deps)
		if (dep.name == "Ollama")
			return dep.state == DepState::Installed;
	return false;
}

// -----------------------------------------------------------------------------
// Interface
// -----------------------------------------------------------------------------

void Launcher::RefreshReleases()
{
	RunTask("Lecture des versions sur GitHub", [this] {
		std::vector<EngineRelease> releases;
		std::string error;
		bool ok = Engine::FetchReleases(releases, error, [this](const std::string& l) { Log(l); });
		std::lock_guard<std::mutex> lock(m_mutex);
		m_releases_loaded = true;
		m_releases_error = ok ? "" : error;
		if (ok)
			m_releases = std::move(releases);
	});
}

void Launcher::InstallRelease(size_t index)
{
	EngineRelease release = m_releases[index];   // appelé depuis Draw : m_mutex tenu
	m_releases[index].progress = 0.0f;
	RunTask("Installation de Lynx " + release.version, [this, release] {
		auto find = [this, &release]() -> EngineRelease* {
			for (EngineRelease& r : m_releases)
				if (r.tag == release.tag)
					return &r;
			return nullptr;
		};
		bool ok = Engine::Install(release, [this](const std::string& l) { Log(l); }, [&](float p) {
			std::lock_guard<std::mutex> lock(m_mutex);
			if (EngineRelease* r = find())
				r->progress = p;
			m_task_label = "Installation de Lynx " + release.version + " (" + std::to_string(int(p * 100)) + " %)";
		});
		std::lock_guard<std::mutex> lock(m_mutex);
		if (EngineRelease* r = find())
		{
			r->progress = -1.0f;
			r->installed = ok;
		}
		m_plugins_scan = true;
	});
}

void Launcher::UninstallRelease(size_t index)
{
	std::string tag = m_releases[index].tag;   // appelé depuis Draw : m_mutex tenu
	RunTask("Désinstallation de " + tag, [this, tag] {
		bool ok = Engine::Uninstall(tag, [this](const std::string& l) { Log(l); });
		std::lock_guard<std::mutex> lock(m_mutex);
		for (EngineRelease& r : m_releases)
			if (r.tag == tag)
				r.installed = !ok && Engine::IsInstalled(tag);
		m_plugins_scan = true;
	});
}

// -----------------------------------------------------------------------------
// Plugins
// -----------------------------------------------------------------------------

void Launcher::OnDrop(const std::vector<std::string>& paths)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const std::string& p : paths)
		// GLFW donne de l'UTF-8 : u8string pour les accents sous Windows
		m_dropped.emplace_back(std::u8string(p.begin(), p.end()));
	m_page = Page::Plugins;
}

void Launcher::ScanPlugins()
{
	m_plugins_scan = false;
	m_engines = PluginStore::ListEngines();
	bool found = std::any_of(m_engines.begin(), m_engines.end(),
	                         [this](const InstalledEngine& e) { return e.tag == m_plugin_engine; });
	if (!found)
		m_plugin_engine = m_engines.empty() ? std::string() : m_engines.front().tag;   // la plus récente
	m_installed_plugins = m_plugin_engine.empty() ? std::vector<InstalledPlugin>{}
	                                              : PluginStore::ListInstalled(m_plugin_engine);
}

std::string Launcher::PluginEngineTag() const
{
	return m_plugin_engine;
}

std::string Launcher::PluginEngineVersion() const
{
	for (const InstalledEngine& e : m_engines)
		if (e.tag == m_plugin_engine)
			return e.version;
	return {};
}

void Launcher::RefreshCatalog()
{
	RunTask("Lecture du catalogue des plugins", [this] {
		std::vector<CatalogPlugin> catalog;
		std::string error;
		bool ok = PluginStore::FetchCatalog(catalog, error, [this](const std::string& l) { Log(l); });
		std::lock_guard<std::mutex> lock(m_mutex);
		m_catalog_loaded = true;
		m_catalog_error = ok ? "" : error;
		if (ok)
			m_catalog = std::move(catalog);
	});
}

void Launcher::InstallPlugin(size_t catalog_index, const std::string& release_tag)
{
	// appelé depuis Draw : m_mutex tenu
	CatalogPlugin plugin = m_catalog[catalog_index];
	const std::string engine = m_plugin_engine;
	auto it = std::find_if(plugin.releases.begin(), plugin.releases.end(),
	                       [&](const PluginRelease& r) { return r.tag == release_tag; });
	if (it == plugin.releases.end() || engine.empty())
		return;
	PluginRelease release = *it;
	m_catalog[catalog_index].progress = 0.0f;

	RunTask("Installation de " + plugin.name, [this, plugin, release, engine] {
		auto set_progress = [this, &plugin](float p) {
			std::lock_guard<std::mutex> lock(m_mutex);
			for (CatalogPlugin& c : m_catalog)
				if (c.name == plugin.name)
					c.progress = p;
			m_task_label = "Installation de " + plugin.name + " (" + std::to_string(int(p * 100)) + " %)";
		};
		PluginStore::Install(plugin, release, engine, [this](const std::string& l) { Log(l); }, set_progress);
		std::lock_guard<std::mutex> lock(m_mutex);
		for (CatalogPlugin& c : m_catalog)
			if (c.name == plugin.name)
				c.progress = -1.0f;
		m_plugins_scan = true;
	});
}

void Launcher::ImportPlugins(std::vector<std::filesystem::path> sources)
{
	const std::string engine = m_plugin_engine;   // appelé depuis Draw : m_mutex tenu
	if (engine.empty() || sources.empty())
		return;
	RunTask("Import de plugin(s)", [this, sources, engine] {
		for (const std::filesystem::path& source : sources)
			PluginStore::Import(source, engine, [this](const std::string& l) { Log(l); });
		std::lock_guard<std::mutex> lock(m_mutex);
		m_plugins_scan = true;
	});
}

void Launcher::RemovePlugin(const InstalledPlugin& plugin)
{
	RunTask("Retrait de " + plugin.name, [this, plugin] {
		PluginStore::Remove(plugin, [this](const std::string& l) { Log(l); });
		std::lock_guard<std::mutex> lock(m_mutex);
		m_plugins_scan = true;
	});
}

// -----------------------------------------------------------------------------
// Interface
// -----------------------------------------------------------------------------
// Aucune largeur en pixels « en dur » : tout est calculé à partir de la taille
// du texte (CalcTextSize / GetFrameHeight), pour que rien ne déborde quelle que
// soit la police.

namespace
{
	// Largeur d'un bouton pour un libellé donné.
	float ButtonWidth(const char* label)
	{
		return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	}

	// SameLine seulement s'il reste la place pour le prochain élément.
	void SameLineIfFits(float next_width)
	{
		float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
		ImGui::SameLine();
		if (ImGui::GetCursorScreenPos().x + next_width > right)
			ImGui::NewLine();
	}

	// Petite étiquette colorée (« Beta », « conseillé »...)
	void Badge(const char* text, const ImVec4& color)
	{
		ImVec2 size = ImGui::CalcTextSize(text);
		ImVec2 pad(ImGui::GetStyle().FramePadding.x * 0.6f, 1.0f);
		ImVec2 p = ImGui::GetCursorScreenPos();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(p, ImVec2(p.x + size.x + pad.x * 2, p.y + size.y + pad.y * 2),
		                    ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 0.18f)), 2.0f);
		draw->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ImGui::ColorConvertFloat4ToU32(color), text);
		ImGui::Dummy(ImVec2(size.x + pad.x * 2, size.y + pad.y * 2));
	}
}

void Launcher::Draw()
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::Begin("##launcher", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
	             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
	ImGui::PopStyleVar(2);

	std::lock_guard<std::mutex> lock(m_mutex);

	DrawSidebar();
	ImGui::SameLine(0, 0);

	const float line = ImGui::GetTextLineHeightWithSpacing();
	float log_height = m_show_log ? line * 5.5f : ImGui::GetFrameHeightWithSpacing() + 12.0f;
	ImGui::BeginGroup();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(line * 1.2f, line));
	ImGui::BeginChild("##content", ImVec2(0, -log_height), ImGuiChildFlags_AlwaysUseWindowPadding);
	switch (m_page)
	{
	case Page::Dependencies: DrawDependencies(); break;
	case Page::Models:       DrawModels();       break;
	case Page::Engine:       DrawEngine();       break;
	case Page::Plugins:      DrawPlugins();      break;
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();
	DrawLog();
	ImGui::EndGroup();

	ImGui::End();
}

void Launcher::DrawSidebar()
{
	int installed = 0;
	for (const Dependency& dep : m_deps)
		installed += dep.state == DepState::Installed;
	std::string dep_badge = std::to_string(installed) + "/" + std::to_string(m_deps.size());
	bool ready = RequiredInstalled();

	int models = static_cast<int>(m_other_models.size());
	for (const ModelInfo& model : m_models)
		models += model.installed;
	std::string model_badge = m_models_loaded ? std::to_string(models) : "";

	int versions = 0;
	for (const EngineRelease& release : m_releases)
		versions += release.installed;
	std::string engine_badge = m_releases_loaded ? std::to_string(versions) + "/" + std::to_string(m_releases.size()) : "";

	std::string plugin_badge = m_plugin_engine.empty() ? "" : std::to_string(m_installed_plugins.size());

	struct Item { const char* label; Page page; bool enabled; std::string badge; ImVec4 color; };
	Item items[] = {
		{ "Dépendances", Page::Dependencies, true, dep_badge, ready ? kGreen : kYellow },
		{ "Modèles IA",  Page::Models, OllamaInstalled(), model_badge, kMuted },
		{ "Moteur",      Page::Engine, true, engine_badge, kMuted },
		{ "Plugins",     Page::Plugins, true, plugin_badge, kMuted },
	};

	// largeur : le plus long « libellé + badge », et le texte d'état du bas
	const ImGuiStyle& style = ImGui::GetStyle();
	const float pad = ImGui::GetTextLineHeight();
	float width = 0.0f;
	for (const Item& item : items)
		width = std::max(width, ImGui::CalcTextSize(item.label).x + ImGui::CalcTextSize("0/0").x + pad * 1.8f);
	width += pad * 2;

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.08f, 0.095f, 1));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
	ImGui::BeginChild("##sidebar", ImVec2(width, 0), ImGuiChildFlags_AlwaysUseWindowPadding);

	ImGui::PushFont(nullptr, style.FontSizeBase * 1.8f);
	ImGui::TextColored(kAccent, "LYNX");
	ImGui::PopFont();
	ImGui::TextColored(kMuted, "launcher");
	ImGui::Dummy(ImVec2(0, pad));

	const float row = ImGui::GetFrameHeight() * 1.3f;
	for (const Item& item : items)
	{
		bool active = m_page == item.page;
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.17f, 0.18f, 0.21f, 1));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.14f, 0.15f, 0.18f, 1));
		ImGui::BeginDisabled(!item.enabled);
		ImVec2 pos = ImGui::GetCursorScreenPos();
		float avail = ImGui::GetContentRegionAvail().x;
		ImGui::PushID(item.label);
		if (ImGui::Selectable("##nav", active, 0, ImVec2(avail, row)))
		{
			m_page = item.page;
			if (item.page == Page::Plugins)
				m_plugins_scan = true;   // une version a pu être installée / un plugin ajouté à la main
		}
		ImGui::PopID();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		float text_y = pos.y + (row - ImGui::GetTextLineHeight()) * 0.5f;
		if (active)
			draw->AddRectFilled(pos, ImVec2(pos.x + 3, pos.y + row), ImGui::ColorConvertFloat4ToU32(kAccent));
		draw->AddText(ImVec2(pos.x + pad * 0.7f, text_y), ImGui::GetColorU32(ImGuiCol_Text), item.label);
		if (!item.badge.empty())
		{
			ImVec2 size = ImGui::CalcTextSize(item.badge.c_str());
			draw->AddText(ImVec2(pos.x + avail - size.x - pad * 0.4f, text_y),
			              ImGui::ColorConvertFloat4ToU32(item.color), item.badge.c_str());
		}
		ImGui::EndDisabled();
		ImGui::PopStyleColor(2);
	}

	// bas de la barre : état général
	float bottom = ImGui::GetWindowHeight() - ImGui::GetTextLineHeightWithSpacing() * 2.0f - pad;
	if (ImGui::GetCursorPosY() < bottom)
		ImGui::SetCursorPosY(bottom);
	ImGui::Separator();
	ImGui::Spacing();
	if (Busy())
	{
		const char* spinner = "|/-\\";
		ImGui::TextColored(kYellow, "%c Travail en cours", spinner[int(ImGui::GetTime() * 8) % 4]);
	}
	else
	{
		StatusDot(ready ? kGreen : kYellow);
		ImGui::TextUnformatted(ready ? "Prêt" : "À compléter");
	}

	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void Launcher::DrawDependencies()
{
	SectionTitle("Dépendances",
	             "Outils nécessaires pour compiler et utiliser le moteur Lynx. "
	             "Ollama est installé par défaut ; les modèles se choisissent dans l'onglet Modèles IA.");

	if (!m_package_manager && !m_package_manager_info.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, kRed);
		ImGui::TextWrapped("%s", m_package_manager_info.c_str());
		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	// « Tout installer » : les outils nécessaires (les optionnels ont leur bouton).
	std::vector<size_t> missing;
	for (size_t i = 0; i < m_deps.size(); ++i)
		if (m_deps[i].required && (m_deps[i].state == DepState::Missing || m_deps[i].state == DepState::Failed))
			missing.push_back(i);

	const float tall = ImGui::GetFrameHeight() * 1.3f;
	ImGui::BeginDisabled(Busy() || missing.empty() || !m_package_manager);
	std::string install_label = missing.empty() ? "Tout est installé"
	                                            : "Tout installer (" + std::to_string(missing.size()) + ")";
	if (AccentButton(install_label.c_str(), ImVec2(0, tall)))
		InstallDependencies(missing);
	ImGui::EndDisabled();
	SameLineIfFits(ButtonWidth("Revérifier"));
	ImGui::BeginDisabled(Busy());
	if (ImGui::Button("Revérifier", ImVec2(0, tall)))
		CheckAll();
	ImGui::EndDisabled();
	if (m_package_manager && !m_package_manager_info.empty())
	{
		std::string via = "via " + m_package_manager_info;
		SameLineIfFits(ImGui::CalcTextSize(via.c_str()).x);
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(kMuted, "%s", via.c_str());
	}
	ImGui::Spacing();

	ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
	if (ImGui::BeginTable("##deps", 3, flags))
	{
		// colonnes fixes ajustées automatiquement à leur contenu
		ImGui::TableSetupColumn("Outil", ImGuiTableColumnFlags_WidthStretch);
		float dot = ImGui::GetTextLineHeight() * 0.75f;
		ImGui::TableSetupColumn("État", ImGuiTableColumnFlags_WidthFixed,
		                        dot + std::max(ImGui::CalcTextSize("Installé 00.00.0").x, ImGui::CalcTextSize("Vérification...").x));
		ImGui::TableSetupColumn("##action", ImGuiTableColumnFlags_WidthFixed,
		                        ButtonWidth("Installer") + ImGui::GetStyle().ItemSpacing.x + ButtonWidth("Site"));
		ImGui::TableHeadersRow();

		for (size_t i = 0; i < m_deps.size(); ++i)
		{
			Dependency& dep = m_deps[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(dep.name.c_str());
			if (!dep.required)
			{
				ImGui::SameLine();
				ImGui::TextColored(kMuted, "(optionnel)");
			}
			ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
			ImGui::TextWrapped("%s", dep.description.c_str());
			ImGui::PopStyleColor();

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			StatusDot(StateColor(dep.state));
			if (dep.state == DepState::Installed)
				ImGui::Text("%s %s", Dependencies::StateLabel(dep.state), dep.version.c_str());
			else if (dep.state == DepState::Missing && !dep.version.empty())
			{
				ImGui::TextColored(kRed, "%s", Dependencies::StateLabel(dep.state));
				ImGui::TextColored(kMuted, "(%s trouvé)", dep.version.c_str());
			}
			else
				ImGui::TextUnformatted(Dependencies::StateLabel(dep.state));

			ImGui::TableNextColumn();
			if (dep.state == DepState::Missing || dep.state == DepState::Failed)
			{
				ImGui::BeginDisabled(Busy() || (!m_package_manager && dep.download_url.empty()));
				if (ImGui::Button("Installer"))
					InstallDependencies({ i });
				ImGui::EndDisabled();
				ImGui::SameLine();
			}
			if (ImGui::Button("Site"))
				Process::OpenUrl(dep.url);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", dep.url.c_str());

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::Spacing();
	ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
	ImGui::TextWrapped("Les installations passent par winget et peuvent demander une confirmation "
	                   "administrateur (UAC). Si un outil reste « Manquant » après son installation, "
	                   "relance le launcher pour recharger le PATH.");
	ImGui::PopStyleColor();
}

void Launcher::DrawModels()
{
	SectionTitle("Modèles IA",
	             "Modèles locaux utilisés par Lynxie, l'assistant de l'éditeur (Commands > Local AI). "
	             "Aucun n'est installé par défaut : choisis ceux que ta machine peut faire tourner.");

	if (!OllamaInstalled())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
		ImGui::TextWrapped("Ollama n'est pas installé : installe-le depuis l'onglet Dépendances.");
		ImGui::PopStyleColor();
		return;
	}

	// première ouverture : on interroge Ollama une fois
	if (!m_models_loaded && !Busy())
		RefreshModels();

	// état du serveur
	ImGui::AlignTextToFramePadding();
	StatusDot(m_server_running ? kGreen : kMuted);
	const char* server = m_server_running ? "Serveur Ollama actif (localhost:11434)" : "Serveur Ollama non contacté";
	ImGui::TextUnformatted(server);
	const char* refresh = m_server_running ? "Actualiser" : "Démarrer / actualiser";
	SameLineIfFits(ButtonWidth(refresh));
	ImGui::BeginDisabled(Busy());
	if (ImGui::Button(refresh))
		RefreshModels();
	ImGui::EndDisabled();
	ImGui::Spacing();

	std::vector<std::string> selected;
	for (const ModelInfo& model : m_models)
		if (model.selected && !model.installed)
			selected.push_back(model.tag);

	ImGui::BeginDisabled(Busy() || selected.empty());
	std::string pull_label = selected.empty() ? "Aucun modèle sélectionné"
	                                          : "Télécharger la sélection (" + std::to_string(selected.size()) + ")";
	if (AccentButton(pull_label.c_str(), ImVec2(0, ImGui::GetFrameHeight() * 1.3f)))
		PullModels(selected);
	ImGui::EndDisabled();
	ImGui::Spacing();

	ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
	if (ImGui::BeginTable("##models", 4, flags))
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		ImGui::TableSetupColumn("##sel", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
		ImGui::TableSetupColumn("Modèle", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Taille", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("~19.0 Go").x);
		ImGui::TableSetupColumn("État", ImGuiTableColumnFlags_WidthFixed,
		                        std::max(ImGui::CalcTextSize("Non installé").x + ImGui::GetTextLineHeight() * 0.75f,
		                                 ButtonWidth("Supprimer")));
		ImGui::TableHeadersRow();

		for (size_t i = 0; i < m_models.size(); ++i)
		{
			ModelInfo& model = m_models[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::BeginDisabled(model.installed || Busy());
			ImGui::Checkbox("##s", &model.selected);
			ImGui::EndDisabled();

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(model.tag.c_str());
			if (model.recommended)
			{
				SameLineIfFits(ImGui::CalcTextSize("conseillé").x + style.FramePadding.x * 2);
				Badge("conseillé", kAccent);
			}
			ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
			ImGui::TextWrapped("%s VRAM conseillée : %s.", model.description.c_str(), model.vram.c_str());
			ImGui::PopStyleColor();

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(model.size.c_str());

			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			if (model.progress >= 0.0f)
			{
				char overlay[16];
				std::snprintf(overlay, sizeof(overlay), "%d %%", int(model.progress * 100));
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
				ImGui::ProgressBar(model.progress, ImVec2(-1, 0), overlay);
				ImGui::PopStyleColor();
			}
			else if (model.installed)
			{
				StatusDot(kGreen);
				ImGui::TextUnformatted("Installé");
				ImGui::BeginDisabled(Busy());
				if (ImGui::Button("Supprimer"))
					RemoveModel(model.tag);
				ImGui::EndDisabled();
			}
			else
			{
				StatusDot(kMuted);
				ImGui::TextColored(kMuted, "Non installé");
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::TextUnformatted("Autre modèle Ollama");
	ImGui::SetNextItemWidth(ImGui::CalcTextSize("qwen2.5-coder:14b-instruct").x + 20.0f);
	ImGui::InputTextWithHint("##custom", "ex : llama3.1:8b", m_custom_model, sizeof(m_custom_model));
	SameLineIfFits(ButtonWidth("Télécharger"));
	ImGui::BeginDisabled(Busy() || m_custom_model[0] == '\0');
	if (ImGui::Button("Télécharger"))
	{
		PullModels({ m_custom_model });
		m_custom_model[0] = '\0';
	}
	ImGui::EndDisabled();
	SameLineIfFits(ButtonWidth("Catalogue Ollama"));
	if (ImGui::Button("Catalogue Ollama"))
		Process::OpenUrl("https://ollama.com/library");

	if (!m_other_models.empty())
	{
		ImGui::Spacing();
		ImGui::TextColored(kMuted, "Autres modèles installés :");
		for (const std::string& name : m_other_models)
		{
			ImGui::PushID(name.c_str());
			ImGui::AlignTextToFramePadding();
			ImGui::BulletText("%s", name.c_str());
			ImGui::SameLine();
			ImGui::BeginDisabled(Busy());
			if (ImGui::Button("Supprimer"))
				RemoveModel(name);
			ImGui::EndDisabled();
			ImGui::PopID();
		}
	}
}

void Launcher::DrawEngine()
{
	SectionTitle("Moteur",
	             "Versions de Lynx publiées sur GitHub (releases de oscar-soirey/Lynx-Engine). "
	             "Chaque version s'installe dans son propre dossier : on peut en garder plusieurs.");

	if (!m_releases_loaded && !Busy())
		RefreshReleases();

	ImGui::BeginDisabled(Busy());
	if (ImGui::Button("Actualiser"))
		RefreshReleases();
	ImGui::EndDisabled();
	SameLineIfFits(ButtonWidth("Dossier des versions"));
	if (ImGui::Button("Dossier des versions"))
	{
		std::error_code error;
		std::filesystem::create_directories(Engine::VersionsDirectory(), error);
		Process::OpenFolder(Engine::VersionsDirectory());
	}
	SameLineIfFits(ButtonWidth("Page GitHub"));
	if (ImGui::Button("Page GitHub"))
		Process::OpenUrl(std::string("https://github.com/") + Engine::kRepository + "/releases");

	if (!RequiredInstalled())
	{
		ImGui::Spacing();
		ImGui::AlignTextToFramePadding();
		StatusDot(kYellow);
		ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
		ImGui::TextWrapped("Installe toutes les dépendances pour pouvoir installer une version.");
		ImGui::PopStyleColor();
		SameLineIfFits(ButtonWidth("Aller aux dépendances"));
		if (ImGui::Button("Aller aux dépendances"))
			m_page = Page::Dependencies;
	}
	if (!m_releases_error.empty())
	{
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, kRed);
		ImGui::TextWrapped("%s", m_releases_error.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::Spacing();
	ImGui::Spacing();

	if (m_releases.empty())
	{
		ImGui::TextColored(kMuted, m_releases_loaded ? "Aucune version publiée pour l'instant."
		                                             : "Lecture des versions...");
		return;
	}

	// Grille de cartes : autant de colonnes que la largeur le permet.
	const ImGuiStyle& style = ImGui::GetStyle();
	const float line = ImGui::GetTextLineHeightWithSpacing();
	const float gap = style.ItemSpacing.x * 2.0f;
	const float card_pad = ImGui::GetTextLineHeight() * 0.9f;
	const float min_width = ButtonWidth("Lancer") + ButtonWidth("Dossier") + ButtonWidth("Retirer") +
	                        style.ItemSpacing.x * 2 + card_pad * 2 + 4.0f;
	const float avail = ImGui::GetContentRegionAvail().x;
	const int columns = std::max(1, int((avail + gap) / (min_width + gap)));
	const float width = (avail - gap * (columns - 1)) / columns;
	const float height = line * 9.5f + ImGui::GetFrameHeight();   // ~4 lignes de notes

	for (size_t i = 0; i < m_releases.size(); ++i)
	{
		if (i % columns != 0)
			ImGui::SameLine(0, gap);
		DrawReleaseCard(i, ImVec2(width, height));
		if ((i + 1) % columns == 0)
			ImGui::Dummy(ImVec2(0, gap * 0.5f));
	}
}

void Launcher::DrawReleaseCard(size_t index, const ImVec2& size)
{
	EngineRelease& release = m_releases[index];
	const ImGuiStyle& style = ImGui::GetStyle();
	const bool latest = index == 0;
	const bool installing = release.progress >= 0.0f;

	ImGui::PushID(release.tag.c_str());
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.135f, 0.14f, 0.165f, 1));
	ImGui::PushStyleColor(ImGuiCol_Border, latest ? kAccent : ImVec4(0.24f, 0.25f, 0.29f, 1));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, latest ? 2.0f : 1.0f);
	const float pad = ImGui::GetTextLineHeight() * 0.9f;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
	ImGui::BeginChild("##card", size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
	                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// En-tête : « 26.0.1 (Beta) »
	const float top = ImGui::GetCursorPosY();
	ImGui::PushFont(nullptr, style.FontSizeBase * 1.5f);
	ImGui::TextUnformatted(release.version.c_str());
	const float title_height = ImGui::GetItemRectSize().y;
	ImGui::PopFont();
	// badges centrés verticalement sur le numéro de version
	const float badge_y = top + (title_height - ImGui::GetTextLineHeight() - 2.0f) * 0.5f;
	ImGui::SameLine();
	ImGui::SetCursorPosY(badge_y);
	Badge(release.prerelease ? "Beta" : "Stable", release.prerelease ? kYellow : kGreen);
	if (latest)
	{
		ImGui::SameLine();
		ImGui::SetCursorPosY(badge_y);
		Badge("Dernière", kAccent);
	}
	ImGui::SetCursorPosY(top + title_height + style.ItemSpacing.y);

	// Infos
	ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
	std::string info = release.date;
	if (release.asset_size > 0)
		info += "  |  " + Engine::FormatSize(release.asset_size);
	ImGui::TextUnformatted(info.c_str());
	ImGui::PopStyleColor();

	ImGui::AlignTextToFramePadding();
	if (installing)
	{
		StatusDot(kYellow);
		ImGui::TextUnformatted("Installation...");
	}
	else if (release.installed)
	{
		StatusDot(kGreen);
		ImGui::TextUnformatted("Installée");
	}
	else
	{
		StatusDot(kMuted);
		ImGui::TextColored(kMuted, release.asset_url.empty() ? "Pas d'archive Windows" : "Non installée");
	}

	// Notes de version : zone qui prend la place restante, au-dessus des boutons
	const float buttons_height = ImGui::GetFrameHeight() + style.ItemSpacing.y;
	float notes_height = ImGui::GetContentRegionAvail().y - buttons_height - style.ItemSpacing.y;
	if (notes_height > ImGui::GetTextLineHeight())
	{
		ImGui::BeginChild("##notes", ImVec2(0, notes_height), ImGuiChildFlags_None);
		std::string notes = release.body.empty() ? release.name : release.body;
		if (notes.empty())
			notes = "Pas de notes pour cette version.";
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.81f, 0.85f, 1));
		ImGui::TextWrapped("%s", notes.c_str());
		ImGui::PopStyleColor();
		ImGui::EndChild();
	}

	// Actions, en bas de la carte
	ImGui::SetCursorPosY(ImGui::GetWindowHeight() - pad - ImGui::GetFrameHeight());
	if (installing)
	{
		char overlay[16];
		std::snprintf(overlay, sizeof(overlay), "%d %%", int(release.progress * 100));
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
		ImGui::ProgressBar(release.progress, ImVec2(-1, 0), overlay);
		ImGui::PopStyleColor();
	}
	else if (release.installed)
	{
		ImGui::BeginDisabled(Busy());
		if (AccentButton("Lancer"))
			Engine::Launch(release.tag, [this](const std::string& l) {
				m_log.push_back(l);   // m_mutex déjà tenu (Draw)
				m_scroll_log = true;
			});
		ImGui::SameLine();
		if (ImGui::Button("Dossier"))
			Process::OpenFolder(Engine::VersionDirectory(release.tag));
		ImGui::SameLine();
		if (ImGui::Button("Retirer"))
			UninstallRelease(index);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Désinstaller cette version");
		ImGui::EndDisabled();
	}
	else
	{
		ImGui::BeginDisabled(Busy() || !RequiredInstalled() || release.asset_url.empty());
		if (AccentButton("Installer"))
			InstallRelease(index);
		ImGui::EndDisabled();
		if (!RequiredInstalled() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Installe d'abord les dépendances.");
		ImGui::SameLine();
		if (ImGui::Button("Notes complètes"))
			Process::OpenUrl(release.page_url);
	}

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);
	ImGui::PopID();
}

namespace
{
	std::string LowerCase(std::string text)
	{
		for (char& c : text)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return text;
	}

	bool ContainsI(const std::string& text, const std::string& lower_needle)
	{
		return lower_needle.empty() || LowerCase(text).find(lower_needle) != std::string::npos;
	}
}

void Launcher::DrawPlugins()
{
	SectionTitle("Plugins",
	             "Plugins du moteur, installés dans une version de Lynx (dossier plugins de la version). "
	             "Une fois installé, un plugin s'active par projet dans l'éditeur : Options > Plugins.");

	if (m_plugins_scan)
		ScanPlugins();
	if (!m_catalog_loaded && !Busy())
		RefreshCatalog();

	auto log = [this](const std::string& line) {   // m_mutex déjà tenu (Draw)
		m_log.push_back(line);
		m_scroll_log = true;
	};

	// Dossiers / archives glissés sur la fenêtre
	if (!m_dropped.empty() && !Busy())
	{
		if (m_plugin_engine.empty())
			log("Échec de l'import : installe d'abord une version du moteur (onglet Moteur).");
		else
			ImportPlugins(m_dropped);
		m_dropped.clear();
	}

	if (m_engines.empty())
	{
		ImGui::AlignTextToFramePadding();
		StatusDot(kYellow);
		ImGui::TextColored(kYellow, "Aucune version du moteur installée : les plugins s'installent dans une version.");
		SameLineIfFits(ButtonWidth("Aller au moteur"));
		if (ImGui::Button("Aller au moteur"))
			m_page = Page::Engine;
		ImGui::Spacing();
	}

	// --- Version cible + actions ---
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Version du moteur");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::CalcTextSize("v00.00.00 (dernière)").x + ImGui::GetFrameHeight() * 2.0f);
	ImGui::BeginDisabled(Busy() || m_engines.empty());
	if (ImGui::BeginCombo("##engine", m_plugin_engine.empty() ? "aucune" : m_plugin_engine.c_str()))
	{
		for (size_t i = 0; i < m_engines.size(); ++i)
		{
			std::string label = m_engines[i].tag + (i == 0 ? " (dernière)" : "");
			if (ImGui::Selectable(label.c_str(), m_engines[i].tag == m_plugin_engine))
			{
				m_plugin_engine = m_engines[i].tag;
				m_plugins_scan = true;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();

	SameLineIfFits(ButtonWidth("Actualiser"));
	ImGui::BeginDisabled(Busy());
	if (ImGui::Button("Actualiser"))
	{
		m_plugins_scan = true;
		RefreshCatalog();
	}
	ImGui::EndDisabled();

	SameLineIfFits(ButtonWidth("Importer un dossier..."));
	ImGui::BeginDisabled(Busy() || m_plugin_engine.empty());
	if (ImGui::Button("Importer un dossier..."))
	{
#ifdef _WIN32
		std::filesystem::path folder = PluginStore::PickFolder();
		if (!folder.empty())
			ImportPlugins({ folder });
#endif
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Un dossier contenant plugin.json (et bin/).\n"
		                  "On peut aussi glisser un dossier ou un .zip sur la fenêtre.");

	SameLineIfFits(ButtonWidth("Dossier des plugins"));
	ImGui::BeginDisabled(m_plugin_engine.empty());
	if (ImGui::Button("Dossier des plugins"))
	{
		std::error_code error;
		std::filesystem::path dir = PluginStore::PluginsDirectory(m_plugin_engine);
		std::filesystem::create_directories(dir, error);
		Process::OpenFolder(dir);
	}
	ImGui::EndDisabled();

	// --- Filtres ---
	std::vector<std::string> categories;
	for (const CatalogPlugin& c : m_catalog)
		if (!c.category.empty() && std::find(categories.begin(), categories.end(), c.category) == categories.end())
			categories.push_back(c.category);
	for (const InstalledPlugin& p : m_installed_plugins)
		if (!p.category.empty() && std::find(categories.begin(), categories.end(), p.category) == categories.end())
			categories.push_back(p.category);
	std::sort(categories.begin(), categories.end());

	ImGui::SetNextItemWidth(ImGui::CalcTextSize("nom, auteur, description...").x + ImGui::GetFrameHeight());
	ImGui::InputTextWithHint("##search", "Rechercher", m_plugin_search, sizeof(m_plugin_search));
	ImGui::SameLine();
	float category_width = ImGui::CalcTextSize("Toutes les catégories").x;
	for (const std::string& c : categories)
		category_width = std::max(category_width, ImGui::CalcTextSize(c.c_str()).x);
	ImGui::SetNextItemWidth(category_width + ImGui::GetFrameHeight() * 1.5f);
	if (ImGui::BeginCombo("##category", m_plugin_category.empty() ? "Toutes les catégories" : m_plugin_category.c_str()))
	{
		if (ImGui::Selectable("Toutes les catégories", m_plugin_category.empty()))
			m_plugin_category.clear();
		for (const std::string& c : categories)
			if (ImGui::Selectable(c.c_str(), c == m_plugin_category))
				m_plugin_category = c;
		ImGui::EndCombo();
	}
	SameLineIfFits(ImGui::CalcTextSize("Versions bêta").x + ImGui::GetFrameHeight() * 1.5f);
	ImGui::Checkbox("Versions bêta", &m_plugin_prerelease);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Proposer aussi les pré-releases des plugins.");

	if (!m_catalog_error.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, kRed);
		ImGui::TextWrapped("%s", m_catalog_error.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::Spacing();

	// --- Liste : catalogue + plugins installés absents du catalogue (intégrés, importés) ---
	struct Entry { const CatalogPlugin* catalog; size_t index; const InstalledPlugin* installed; };
	std::vector<Entry> entries;
	for (size_t i = 0; i < m_catalog.size(); ++i)
	{
		const InstalledPlugin* inst = nullptr;
		for (const InstalledPlugin& p : m_installed_plugins)
			if (LowerCase(p.name) == LowerCase(m_catalog[i].name))
				inst = &p;
		entries.push_back({ &m_catalog[i], i, inst });
	}
	for (const InstalledPlugin& p : m_installed_plugins)
		if (std::none_of(m_catalog.begin(), m_catalog.end(),
		                 [&](const CatalogPlugin& c) { return LowerCase(c.name) == LowerCase(p.name); }))
			entries.push_back({ nullptr, 0, &p });

	// installés d'abord, puis par nom
	std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
		if ((a.installed != nullptr) != (b.installed != nullptr))
			return a.installed != nullptr;
		const std::string& na = a.catalog ? a.catalog->name : a.installed->name;
		const std::string& nb = b.catalog ? b.catalog->name : b.installed->name;
		return LowerCase(na) < LowerCase(nb);
	});

	const std::string needle = LowerCase(m_plugin_search);
	entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) {
		const std::string& name = e.catalog ? e.catalog->name : e.installed->name;
		const std::string& desc = e.catalog ? e.catalog->description : e.installed->description;
		const std::string& author = e.catalog ? e.catalog->author : e.installed->author;
		const std::string& category = e.catalog && !e.catalog->category.empty() ? e.catalog->category
		                              : e.installed ? e.installed->category : std::string();
		if (!m_plugin_category.empty() && category != m_plugin_category)
			return true;
		return !(ContainsI(name, needle) || ContainsI(desc, needle) || ContainsI(author, needle));
	}), entries.end());

	if (entries.empty())
	{
		ImGui::TextColored(kMuted, "%s", !m_catalog_loaded ? "Lecture du catalogue..."
		                                 : (needle.empty() && m_plugin_category.empty()) ? "Aucun plugin."
		                                 : "Aucun plugin ne correspond à la recherche.");
		return;
	}

	// Grille de cartes, comme l'onglet Moteur
	const ImGuiStyle& style = ImGui::GetStyle();
	const float line = ImGui::GetTextLineHeightWithSpacing();
	const float gap = style.ItemSpacing.x * 2.0f;
	const float card_pad = ImGui::GetTextLineHeight() * 0.9f;
	const float min_width = ButtonWidth("Mettre à jour") + ButtonWidth("Dossier") + ButtonWidth("Retirer") +
	                        ButtonWidth("Dépôt") + style.ItemSpacing.x * 3 + card_pad * 2 + 4.0f;
	const float avail = ImGui::GetContentRegionAvail().x;
	const int columns = std::max(1, int((avail + gap) / (min_width + gap)));
	const float width = (avail - gap * (columns - 1)) / columns;
	const float height = line * 8.5f + ImGui::GetFrameHeight();

	for (size_t i = 0; i < entries.size(); ++i)
	{
		if (i % columns != 0)
			ImGui::SameLine(0, gap);
		DrawPluginCard(entries[i].catalog, entries[i].index, entries[i].installed, ImVec2(width, height));
		if ((i + 1) % columns == 0)
			ImGui::Dummy(ImVec2(0, gap * 0.5f));
	}
}

void Launcher::DrawPluginCard(const CatalogPlugin* catalog, size_t catalog_index,
                              const InstalledPlugin* installed, const ImVec2& size)
{
	const ImGuiStyle& style = ImGui::GetStyle();
	const std::string engine_version = PluginEngineVersion();
	const std::string& name = catalog ? catalog->name : installed->name;
	const std::string& description = catalog && !catalog->description.empty() ? catalog->description
	                                 : installed ? installed->description : catalog->description;
	const std::string& author = catalog && !catalog->author.empty() ? catalog->author
	                            : installed ? installed->author : catalog->author;
	const std::string category = catalog && !catalog->category.empty() ? catalog->category
	                             : installed ? installed->category : std::string();

	const bool builtin = installed && installed->builtin;
	const bool installing = catalog && catalog->progress >= 0.0f;
	const PluginRelease* best = catalog && !engine_version.empty()
		? PluginStore::BestRelease(*catalog, engine_version, m_plugin_prerelease) : nullptr;
	const bool update = installed && !builtin && best &&
	                    PluginStore::CompareVersions(best->version, installed->version) > 0;

	// version du moteur visée : celle de la release proposée, sinon celle de l'installation
	std::string engine_min;
	if (installed && !update)
		engine_min = installed->engine_min;
	else if (best)
		engine_min = best->engine_min;
	const bool older_engine = !engine_min.empty() && !engine_version.empty() &&
	                          PluginStore::CompareVersions(engine_min, engine_version) < 0;

	ImGui::PushID(name.c_str());
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.135f, 0.14f, 0.165f, 1));
	ImGui::PushStyleColor(ImGuiCol_Border, installed ? ImVec4(0.30f, 0.45f, 0.33f, 1) : ImVec4(0.24f, 0.25f, 0.29f, 1));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
	const float pad = ImGui::GetTextLineHeight() * 0.9f;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
	ImGui::BeginChild("##plugin", size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
	                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// En-tête : nom + badges
	const float top = ImGui::GetCursorPosY();
	ImGui::PushFont(nullptr, style.FontSizeBase * 1.35f);
	ImGui::TextUnformatted(name.c_str());
	const float title_height = ImGui::GetItemRectSize().y;
	ImGui::PopFont();
	const float badge_y = top + (title_height - ImGui::GetTextLineHeight() - 2.0f) * 0.5f;
	auto badge = [&](const char* text, const ImVec4& color) {
		SameLineIfFits(ImGui::CalcTextSize(text).x + style.FramePadding.x * 2);
		ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), badge_y));
		Badge(text, color);
	};
	if (!category.empty())
		badge(category.c_str(), kMuted);
	if (builtin)
		badge("Intégré", kAccent);
	else if (installed && installed->source == "local")
		badge("Local", kYellow);
	if (best && best->prerelease)
		badge("Beta", kYellow);
	ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), top + title_height + style.ItemSpacing.y));

	// Auteur / dépôt
	std::string info;
	if (!author.empty())
		info = "par " + author;
	if (catalog)
		info += (info.empty() ? "" : "  |  ") + catalog->repo;
	if (!info.empty())
		ImGui::TextColored(kMuted, "%s", info.c_str());

	// État
	ImGui::AlignTextToFramePadding();
	if (installing)
	{
		StatusDot(kYellow);
		ImGui::TextUnformatted("Installation...");
	}
	else if (update)
	{
		StatusDot(kYellow);
		ImGui::TextColored(kYellow, "Mise à jour : %s -> %s",
		                   installed->version.empty() ? "?" : installed->version.c_str(), best->version.c_str());
	}
	else if (installed)
	{
		StatusDot(kGreen);
		ImGui::Text("Installé %s", installed->version.c_str());
	}
	else if (best)
	{
		StatusDot(kMuted);
		ImGui::TextColored(kMuted, "Non installé  |  %s%s", best->version.c_str(),
		                   best->asset_size ? ("  |  " + Engine::FormatSize(best->asset_size)).c_str() : "");
	}
	else if (catalog && !catalog->error.empty())
	{
		StatusDot(kRed);
		ImGui::TextColored(kRed, "Dépôt illisible");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", catalog->error.c_str());
	}
	else if (catalog && !catalog->releases.empty())
	{
		// des releases, mais aucune pour cette version : la plus ancienne version du moteur demandée
		std::string needed;
		for (const PluginRelease& r : catalog->releases)
			if (!r.asset_url.empty() && (m_plugin_prerelease || !r.prerelease) &&
			    (needed.empty() || PluginStore::CompareVersions(r.engine_min, needed) < 0))
				needed = r.engine_min;
		StatusDot(kRed);
		if (needed.empty())
			ImGui::TextColored(kRed, "Aucune release téléchargeable");
		else
			ImGui::TextColored(kRed, "Demande Lynx %s ou plus", needed.c_str());
	}
	else
	{
		StatusDot(kMuted);
		ImGui::TextColored(kMuted, engine_version.empty() ? "Aucune version du moteur" : "Aucune release publiée");
	}

	if (older_engine && !builtin)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
		ImGui::TextWrapped("Conçu pour Lynx %s : l'éditeur préviendra à l'activation.", engine_min.c_str());
		ImGui::PopStyleColor();
	}

	// Description : la place restante au-dessus des boutons
	const float buttons_height = ImGui::GetFrameHeight() + style.ItemSpacing.y;
	float desc_height = ImGui::GetContentRegionAvail().y - buttons_height - style.ItemSpacing.y;
	if (desc_height > ImGui::GetTextLineHeight())
	{
		ImGui::BeginChild("##desc", ImVec2(0, desc_height), ImGuiChildFlags_None);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.81f, 0.85f, 1));
		ImGui::TextWrapped("%s", description.empty() ? "Pas de description." : description.c_str());
		ImGui::PopStyleColor();
		ImGui::EndChild();
	}

	// Actions
	ImGui::SetCursorPosY(ImGui::GetWindowHeight() - pad - ImGui::GetFrameHeight());
	if (installing)
	{
		char overlay[16];
		std::snprintf(overlay, sizeof(overlay), "%d %%", int(catalog->progress * 100));
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
		ImGui::ProgressBar(catalog->progress, ImVec2(-1, 0), overlay);
		ImGui::PopStyleColor();
	}
	else
	{
		bool first = true;
		auto same_line = [&](const char* label) {
			if (!first)
				SameLineIfFits(ButtonWidth(label));
			first = false;
		};

		ImGui::BeginDisabled(Busy());
		if (catalog && best && !builtin && (!installed || update))
		{
			const char* label = update ? "Mettre à jour" : "Installer";
			same_line(label);
			if (AccentButton(label))
				InstallPlugin(catalog_index, best->tag);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s (%s) dans Lynx %s", best->tag.c_str(), best->date.c_str(), m_plugin_engine.c_str());
		}
		if (installed)
		{
			same_line("Dossier");
			if (ImGui::Button("Dossier"))
				Process::OpenFolder(installed->folder);
			if (!builtin)
			{
				same_line("Retirer");
				if (ImGui::Button("Retirer"))
					RemovePlugin(*installed);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Supprimer ce plugin de Lynx %s", m_plugin_engine.c_str());
			}
		}
		ImGui::EndDisabled();
		if (catalog)
		{
			same_line("Dépôt");
			if (ImGui::Button("Dépôt"))
				Process::OpenUrl(best && !best->page_url.empty() ? best->page_url
				                                                  : "https://github.com/" + catalog->repo);
		}
	}

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);
	ImGui::PopID();
}

void Launcher::DrawLog()
{
	const float pad = ImGui::GetTextLineHeight() * 0.8f;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.065f, 0.075f, 1));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad * 0.5f));
	ImGui::BeginChild("##logpanel", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);

	if (ImGui::Button(m_show_log ? "Masquer le journal" : "Journal"))
		m_show_log = !m_show_log;
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	if (!m_task_label.empty())
		ImGui::TextColored(kYellow, "%s", m_task_label.c_str());
	else
		ImGui::TextColored(kMuted, "%d lignes", static_cast<int>(m_log.size()));

	// boutons alignés à droite
	const float right_width = ButtonWidth("Copier") + ButtonWidth("Effacer") + ImGui::GetStyle().ItemSpacing.x;
	ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - right_width));
	if (ImGui::Button("Copier"))
	{
		std::string all;
		for (const std::string& line : m_log)
			all += line + "\n";
		ImGui::SetClipboardText(all.c_str());
	}
	ImGui::SameLine();
	if (ImGui::Button("Effacer"))
		m_log.clear();

	if (m_show_log)
	{
		ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(m_log.size()));
		while (clipper.Step())
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
			{
				const std::string& line = m_log[i];
				bool error = line.find("chec") != std::string::npos;   // « Échec », « échec »
				bool title = line.rfind("==", 0) == 0;
				if (error)      ImGui::TextColored(kRed, "%s", line.c_str());
				else if (title) ImGui::TextColored(kAccent, "%s", line.c_str());
				else            ImGui::TextUnformatted(line.c_str());
			}
		if (m_scroll_log)
		{
			ImGui::SetScrollHereY(1.0f);
			m_scroll_log = false;
		}
		ImGui::EndChild();
	}

	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}
