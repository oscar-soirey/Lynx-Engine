#include "Launcher.h"
#include "Process.h"
#include "../src/editor/raven/RavenUi.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace ui = lynx::editor::ui;

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

	// Un morceau d'interface (XML sans texte de l'utilisateur : les textes sont posés
	// ensuite avec ui::SetText, rien à échapper).
	Rid Make(const char* xml)
	{
		return Rwidget_create_from_xml(xml, std::strlen(xml));
	}

	Rid Child(Rid scope, const char* name)
	{
		return Rwidget_find(scope, name);
	}

	// Couleur d'un texte d'état : une des classes des thèmes (success, warning, error, dim).
	void SetTone(Rid label, const char* tone)
	{
		for (const char* c : { "success", "warning", "error", "dim" })
			ui::SetClass(label, c, tone && std::strcmp(c, tone) == 0);
	}

	const char* StateTone(DepState state)
	{
		switch (state)
		{
		case DepState::Installed:  return "success";
		case DepState::Missing:
		case DepState::Failed:     return "error";
		case DepState::Installing:
		case DepState::Checking:   return "warning";
		default:                   return "dim";
		}
	}

	std::string Percent(float p)
	{
		return std::to_string(static_cast<int>(p * 100.0f)) + " %";
	}

	// ---- Morceaux d'interface des listes ------------------------------------

	const char* kDependencyRow = R"xml(
<Border Class="toolgroup" BorderWidth="1" Padding="14,10">
	<HBox Spacing="12">
		<VBox Spacing="2" Slot.Fill="1">
			<HBox Spacing="6">
				<Label Name="name"/>
				<Label Name="optional" Text="(optionnel)" Class="dim" Visibility="Collapsed"/>
			</HBox>
			<Label Name="desc" Class="dim" Wrap="true"/>
		</VBox>
		<Label Name="state" Width="230" Slot.VAlign="Center"/>
		<Button Name="install" Text="Installer" Slot.VAlign="Center" Visibility="Collapsed"/>
		<Button Name="site" Text="Site" Slot.VAlign="Center"/>
	</HBox>
</Border>)xml";

	const char* kModelRow = R"xml(
<Border Class="toolgroup" BorderWidth="1" Padding="14,10">
	<HBox Spacing="12">
		<Checkbox Name="select" Slot.VAlign="Center"/>
		<VBox Spacing="2" Slot.Fill="1">
			<HBox Spacing="8">
				<Label Name="name"/>
				<Label Name="recommended" Text="conseillé" Class="section" Visibility="Collapsed"/>
			</HBox>
			<Label Name="desc" Class="dim" Wrap="true"/>
		</VBox>
		<Label Name="size" Width="90" Slot.VAlign="Center"/>
		<VBox Spacing="4" Width="200" Slot.VAlign="Center">
			<Label Name="state"/>
			<ProgressBar Name="progress" Visibility="Collapsed"/>
			<Button Name="remove" Text="Supprimer" Slot.HAlign="Left" Visibility="Collapsed"/>
		</VBox>
	</HBox>
</Border>)xml";

	const char* kOtherModelRow = R"xml(
<HBox Spacing="8">
	<Label Name="name" Slot.VAlign="Center"/>
	<Button Name="remove" Text="Supprimer"/>
</HBox>)xml";

	const char* kLocalEngineCard = R"xml(
<Border Class="toolgroup" BorderWidth="1" Padding="16,12">
	<HBox Spacing="10">
		<VBox Spacing="2" Slot.Fill="1">
			<HBox Spacing="8">
				<Label Name="name" Class="dialog-title"/>
				<Label Name="badge" Text="Locale" Class="warning" Slot.VAlign="Center"/>
				<Label Name="state" Slot.VAlign="Center"/>
			</HBox>
			<Label Name="info" Class="dim" Wrap="true"/>
		</VBox>
		<Button Name="launch" Text="Lancer" Style="Primary" Slot.VAlign="Center"/>
		<Button Name="folder" Text="Dossier" Slot.VAlign="Center"/>
		<Button Name="remove" Text="Oublier" Tooltip="Retire cette version de la liste. Ses fichiers ne sont pas supprimés." Slot.VAlign="Center"/>
	</HBox>
</Border>)xml";

	const char* kReleaseCard = R"xml(
<Border Class="toolgroup" BorderWidth="1" Padding="16,12">
	<VBox Spacing="6">
		<HBox Spacing="8">
			<Label Name="version" Class="dialog-title" Slot.VAlign="Center"/>
			<Label Name="kind" Slot.VAlign="Center"/>
			<Label Name="latest" Text="Dernière" Class="section" Slot.VAlign="Center" Visibility="Collapsed"/>
			<Spacer Slot.Fill="1"/>
			<Label Name="state" Slot.VAlign="Center"/>
		</HBox>
		<Label Name="info" Class="dim"/>
		<Label Name="notes" Wrap="true" Visibility="Collapsed" Slot.Padding="0,4"/>
		<HBox Spacing="6">
			<ProgressBar Name="progress" Slot.Fill="1" Slot.VAlign="Center" Visibility="Collapsed"/>
			<Button Name="install" Text="Installer" Style="Primary" Visibility="Collapsed"/>
			<Button Name="launch" Text="Lancer" Style="Primary" Visibility="Collapsed"/>
			<Button Name="folder" Text="Dossier" Visibility="Collapsed"/>
			<Button Name="remove" Text="Retirer" Tooltip="Désinstaller cette version" Visibility="Collapsed"/>
			<Spacer Name="spacer" Slot.Fill="1"/>
			<Button Name="notes.toggle" Text="Notes de version"/>
			<Button Name="page" Text="Page GitHub"/>
		</HBox>
	</VBox>
</Border>)xml";

	const char* kPluginCard = R"xml(
<Border Class="toolgroup" BorderWidth="1" Padding="16,12">
	<VBox Spacing="4">
		<HBox Spacing="8">
			<Label Name="name" Class="dialog-title" Slot.VAlign="Center"/>
			<Label Name="category" Class="dim" Slot.VAlign="Center"/>
			<Label Name="badge" Class="section" Slot.VAlign="Center" Visibility="Collapsed"/>
			<Label Name="beta" Text="Beta" Class="warning" Slot.VAlign="Center" Visibility="Collapsed"/>
			<Spacer Slot.Fill="1"/>
			<Label Name="state" Slot.VAlign="Center"/>
		</HBox>
		<Label Name="info" Class="dim"/>
		<Label Name="older" Class="warning" Wrap="true" Visibility="Collapsed"/>
		<Label Name="desc" Wrap="true" Slot.Padding="0,4"/>
		<HBox Spacing="6">
			<ProgressBar Name="progress" Slot.Fill="1" Slot.VAlign="Center" Visibility="Collapsed"/>
			<Button Name="install" Text="Installer" Style="Primary" Visibility="Collapsed"/>
			<Button Name="folder" Text="Dossier" Visibility="Collapsed"/>
			<Button Name="remove" Text="Retirer" Visibility="Collapsed"/>
			<Spacer Slot.Fill="1"/>
			<Button Name="repo" Text="Dépôt" Visibility="Collapsed"/>
		</HBox>
	</VBox>
</Border>)xml";
}

Launcher::Launcher(Rid window)
	: m_window(window)
{
	m_deps = Dependencies::CreateList();
	m_models = Models::Catalog();
	for (ModelInfo& model : m_models)
		model.selected = model.recommended;
	m_local_engines = Engine::LocalEngines();

	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	BindUi();
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
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	LogLocked(line);
}

void Launcher::LogLocked(const std::string& line)
{
	m_log.push_back(line);
	++m_log_total;
	if (m_log.size() > 5000)
	{
		m_log.erase(m_log.begin(), m_log.begin() + 1000);
		m_log_refill = true;
	}
}

void Launcher::RunTask(const std::string& label, std::function<void()> task)
{
	if (m_busy.exchange(true))
		return;
	if (m_worker.joinable())
		m_worker.join();
	// Appelé depuis l'interface (m_mutex déjà tenu) ou le constructeur.
	m_task_label = label;
	Changed();
	m_worker = std::thread([this, task = std::move(task)] {
		task();
		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_task_label.clear();
		}
		m_busy = false;
		Changed();
	});
}

void Launcher::CheckAll()
{
	RunTask("Vérification des dépendances", [this] {
		std::string info;
		bool available = Dependencies::PackageManagerAvailable(&info);

		std::vector<Dependency> deps;
		{
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_package_manager = available;
			m_package_manager_info = info;
			for (Dependency& dep : m_deps)
				dep.state = DepState::Checking;
			deps = m_deps;
		}
		for (size_t i = 0; i < deps.size(); ++i)
		{
			Dependencies::Check(deps[i]);
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_deps[i].state = deps[i].state;
			m_deps[i].version = deps[i].version;
			Changed();
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
				std::lock_guard<std::recursive_mutex> lock(m_mutex);
				m_deps[index].state = DepState::Installing;
				dep = m_deps[index];
				Changed();
			}
			bool ok = Dependencies::Install(dep, [this](const std::string& l) { Log(l); });
			failures += ok ? 0 : 1;
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
				std::lock_guard<std::recursive_mutex> lock(m_mutex);
				for (ModelInfo& model : m_models)
					if (model.tag == tag)
						model.progress = p;
				m_task_label = "Téléchargement de " + tag + " (" + std::to_string(int(p * 100)) + " %)";
			};
			set_progress(0.0f);
			bool ok = Models::Pull(tag, [this](const std::string& l) { Log(l); }, set_progress);
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
// Moteur
// -----------------------------------------------------------------------------

void Launcher::RefreshReleases()
{
	RunTask("Lecture des versions sur GitHub", [this] {
		// find_package(Lynx) des projets de jeu : versions installées dans le registre CMake
		Engine::RegisterInstalledVersions([this](const std::string& l) { Log(l); });

		std::vector<EngineRelease> releases;
		std::string error;
		bool ok = Engine::FetchReleases(releases, error, [this](const std::string& l) { Log(l); });
		std::vector<LocalEngine> local = Engine::LocalEngines();
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_local_engines = std::move(local);
		m_releases_loaded = true;
		m_releases_error = ok ? "" : error;
		if (ok)
			m_releases = std::move(releases);
	});
}

void Launcher::InstallRelease(size_t index)
{
	EngineRelease release = m_releases[index];   // appelé depuis l'interface : m_mutex tenu
	m_releases[index].progress = 0.0f;
	RunTask("Installation de Lynx " + release.version, [this, release] {
		auto find = [this, &release]() -> EngineRelease* {
			for (EngineRelease& r : m_releases)
				if (r.tag == release.tag)
					return &r;
			return nullptr;
		};
		bool ok = Engine::Install(release, [this](const std::string& l) { Log(l); }, [&](float p) {
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			if (EngineRelease* r = find())
				r->progress = p;
			m_task_label = "Installation de Lynx " + release.version + " (" + std::to_string(int(p * 100)) + " %)";
		});
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
	std::string tag = m_releases[index].tag;   // appelé depuis l'interface : m_mutex tenu
	RunTask("Désinstallation de " + tag, [this, tag] {
		bool ok = Engine::Uninstall(tag, [this](const std::string& l) { Log(l); });
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	for (const std::string& p : paths)
		// Raven donne de l'UTF-8 : u8string pour les accents sous Windows
		m_dropped.emplace_back(std::u8string(p.begin(), p.end()));
	m_page = Page::Plugins;
	Changed();
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
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_catalog_loaded = true;
		m_catalog_error = ok ? "" : error;
		if (ok)
			m_catalog = std::move(catalog);
	});
}

void Launcher::InstallPlugin(size_t catalog_index, const std::string& release_tag)
{
	// appelé depuis l'interface : m_mutex tenu
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
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			for (CatalogPlugin& c : m_catalog)
				if (c.name == plugin.name)
					c.progress = p;
			m_task_label = "Installation de " + plugin.name + " (" + std::to_string(int(p * 100)) + " %)";
		};
		PluginStore::Install(plugin, release, engine, [this](const std::string& l) { Log(l); }, set_progress);
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		for (CatalogPlugin& c : m_catalog)
			if (c.name == plugin.name)
				c.progress = -1.0f;
		m_plugins_scan = true;
	});
}

void Launcher::ImportPlugins(std::vector<std::filesystem::path> sources)
{
	const std::string engine = m_plugin_engine;   // appelé depuis l'interface : m_mutex tenu
	if (engine.empty() || sources.empty())
		return;
	RunTask("Import de plugin(s)", [this, sources, engine] {
		for (const std::filesystem::path& source : sources)
			PluginStore::Import(source, engine, [this](const std::string& l) { Log(l); });
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_plugins_scan = true;
	});
}

void Launcher::RemovePlugin(const InstalledPlugin& plugin)
{
	RunTask("Retrait de " + plugin.name, [this, plugin] {
		PluginStore::Remove(plugin, [this](const std::string& l) { Log(l); });
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_plugins_scan = true;
	});
}

// -----------------------------------------------------------------------------

void Launcher::AddLocalEngine()
{
#ifdef _WIN32
	// appelé depuis l'interface : m_mutex tenu
	const std::filesystem::path folder = PluginStore::PickFolder();
	if (folder.empty())
		return;
	RunTask("Ajout d'une version locale", [this, folder] {
		std::string error;
		const std::string tag = Engine::AddLocal(folder, error, [this](const std::string& l) { Log(l); });
		std::vector<LocalEngine> local = Engine::LocalEngines();
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_local_engines = std::move(local);
		if (!tag.empty())
			m_plugin_engine = tag;   // les plugins visent la version qu'on vient d'ajouter
		m_plugins_scan = true;
	});
#endif
}

void Launcher::RemoveLocalEngine(const std::string& tag)
{
	Engine::RemoveLocal(tag, [this](const std::string& l) { LogLocked(l); });
	m_local_engines = Engine::LocalEngines();
	m_plugins_scan = true;
	Changed();
}

std::string Launcher::EngineLabel(const std::string& tag) const
{
	for (const LocalEngine& local : m_local_engines)
		if (local.tag == tag)
			return local.name + " (locale" + (local.version.empty() ? "" : ", " + local.version) + ")";
	return tag;
}

// -----------------------------------------------------------------------------
// Interface : la fenêtre (interface/launcher.xml)
// -----------------------------------------------------------------------------

Rid Launcher::W(const char* name) const
{
	return ui::Find(m_window, name);
}

void Launcher::OnClick(Rid button, std::function<void()> fn)
{
	ui::OnClick(button, [this, fn = std::move(fn)] {
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		fn();
		Changed();
	});
}

void Launcher::BindUi()
{
	Rimage_set_texture(W("side.logo"), ui::Texture("icons/lynxie.png"));

	// Barre latérale
	const std::pair<const char*, Page> nav[] = {
		{ "nav.deps", Page::Dependencies }, { "nav.models", Page::Models },
		{ "nav.engine", Page::Engine }, { "nav.plugins", Page::Plugins },
	};
	for (const auto& [name, page] : nav)
	{
		const Page target = page;
		OnClick(W(name), [this, target] {
			m_page = target;
			if (target == Page::Plugins)
				m_plugins_scan = true;   // une version a pu être installée / un plugin ajouté à la main
		});
	}

	// Dépendances
	OnClick(W("deps.install"), [this] {
		std::vector<size_t> missing;
		for (size_t i = 0; i < m_deps.size(); ++i)
			if (m_deps[i].required && (m_deps[i].state == DepState::Missing || m_deps[i].state == DepState::Failed))
				missing.push_back(i);
		if (!missing.empty() && m_package_manager && !Busy())
			InstallDependencies(missing);
	});
	OnClick(W("deps.check"), [this] { if (!Busy()) CheckAll(); });

	// Modèles
	OnClick(W("models.refresh"), [this] { if (!Busy()) RefreshModels(); });
	OnClick(W("models.pull"), [this] {
		std::vector<std::string> selected;
		for (const ModelInfo& model : m_models)
			if (model.selected && !model.installed)
				selected.push_back(model.tag);
		if (!selected.empty() && !Busy())
			PullModels(selected);
	});
	OnClick(W("models.custom.pull"), [this] {
		Rid input = W("models.custom");
		const char* text = Rtext_input_get_text(input);
		std::string tag = text ? text : "";
		tag.erase(0, tag.find_first_not_of(" \t"));
		while (!tag.empty() && (tag.back() == ' ' || tag.back() == '\t'))
			tag.pop_back();
		if (tag.empty() || Busy())
			return;
		PullModels({ tag });
		ui::SetText(input, "");
	});
	OnClick(W("models.catalog"), [] { Process::OpenUrl("https://ollama.com/library"); });

	// Moteur
	OnClick(W("engine.refresh"), [this] { if (!Busy()) RefreshReleases(); });
	OnClick(W("engine.add_local"), [this] { if (!Busy()) AddLocalEngine(); });
	OnClick(W("engine.folder"), [] {
		std::error_code error;
		std::filesystem::create_directories(Engine::VersionsDirectory(), error);
		Process::OpenFolder(Engine::VersionsDirectory());
	});
	OnClick(W("engine.github"), [] {
		Process::OpenUrl(std::string("https://github.com/") + Engine::kRepository + "/releases");
	});
	OnClick(W("engine.goto_deps"), [this] { m_page = Page::Dependencies; });

	// Plugins
	OnClick(W("plugins.goto_engine"), [this] { m_page = Page::Engine; });
	OnClick(W("plugins.refresh"), [this] {
		m_plugins_scan = true;
		if (!Busy())
			RefreshCatalog();
	});
	OnClick(W("plugins.import"), [this] {
#ifdef _WIN32
		if (Busy() || m_plugin_engine.empty())
			return;
		std::filesystem::path folder = PluginStore::PickFolder();
		if (!folder.empty())
			ImportPlugins({ folder });
#endif
	});
	OnClick(W("plugins.folder"), [this] {
		if (m_plugin_engine.empty())
			return;
		std::error_code error;
		std::filesystem::path dir = PluginStore::PluginsDirectory(m_plugin_engine);
		std::filesystem::create_directories(dir, error);
		Process::OpenFolder(dir);
	});
	ui::OnSelect(W("plugins.engine"), [this](int index) {
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		if (m_updating || index < 0 || index >= static_cast<int>(m_engines.size()))
			return;
		if (m_engines[static_cast<size_t>(index)].tag != m_plugin_engine)
		{
			m_plugin_engine = m_engines[static_cast<size_t>(index)].tag;
			m_plugins_scan = true;
			Changed();
		}
	});
	ui::OnSelect(W("plugins.category"), [this](int index) {
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		if (m_updating)
			return;
		m_plugin_category = index <= 0 || index > static_cast<int>(m_category_items.size())
			? std::string() : m_category_items[static_cast<size_t>(index - 1)];
		Changed();
	});
	ui::OnText(W("plugins.search"), [this](const std::string& text) {
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_plugin_search = text;
		Changed();
	});
	ui::OnToggle(W("plugins.beta"), [this](bool on) {
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		m_plugin_prerelease = on;
		Changed();
	});

	// Journal
	OnClick(W("log.toggle"), [this] {
		m_log_visible = !m_log_visible;
		ui::SetVisible(W("log.lines"), m_log_visible);
		ui::SetText(W("log.toggle"), m_log_visible ? "Masquer le journal" : "Journal");
	});
	OnClick(W("log.copy"), [this] {
		std::string all;
		for (const std::string& line : m_log)
			all += line + "\n";
		Rclipboard_set_text(all.c_str());
	});
	OnClick(W("log.clear"), [this] {
		m_log.clear();
		m_log_refill = true;
	});
}

void Launcher::ShowPage(Page page)
{
	m_shown_page = page;
	ui::SetVisible(W("page.deps"), page == Page::Dependencies);
	ui::SetVisible(W("page.models"), page == Page::Models);
	ui::SetVisible(W("page.engine"), page == Page::Engine);
	ui::SetVisible(W("page.plugins"), page == Page::Plugins);
	ui::SetClass(W("nav.deps"), "active", page == Page::Dependencies);
	ui::SetClass(W("nav.models"), "active", page == Page::Models);
	ui::SetClass(W("nav.engine"), "active", page == Page::Engine);
	ui::SetClass(W("nav.plugins"), "active", page == Page::Plugins);
}

void Launcher::Tick()
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);

	// Chargements à la première ouverture d'une page
	if (m_page == Page::Models && OllamaInstalled() && !m_models_loaded && !Busy())
		RefreshModels();
	if (m_page == Page::Engine && !m_releases_loaded && !Busy())
		RefreshReleases();
	if (m_page == Page::Plugins)
	{
		if (m_plugins_scan)
		{
			ScanPlugins();
			Changed();
		}
		if (!m_catalog_loaded && !Busy())
			RefreshCatalog();
		// Dossiers / archives glissés sur la fenêtre
		if (!m_dropped.empty() && !Busy())
		{
			if (m_plugin_engine.empty())
				LogLocked("Échec de l'import : installe d'abord une version du moteur (onglet Moteur).");
			else
				ImportPlugins(m_dropped);
			m_dropped.clear();
		}
	}

	if (m_page != m_shown_page)
	{
		ShowPage(m_page);
		m_built_version = 0;   // la page affichée est reconstruite
	}

	const std::uint64_t version = m_version;
	if (version != m_built_version)
	{
		m_built_version = version;
		switch (m_page)
		{
		case Page::Dependencies: BuildDependencies(); break;
		case Page::Models:       BuildModels();       break;
		case Page::Engine:       BuildEngine();       break;
		case Page::Plugins:      BuildPlugins();      break;
		}
		UpdateSidebar();
	}

	UpdateProgress();
	UpdateLog();
}

void Launcher::UpdateSidebar()
{
	int installed = 0;
	for (const Dependency& dep : m_deps)
		installed += dep.state == DepState::Installed;
	const bool ready = RequiredInstalled();
	Rid deps_badge = W("nav.deps.badge");
	ui::SetText(deps_badge, std::to_string(installed) + "/" + std::to_string(m_deps.size()));
	SetTone(deps_badge, ready ? "success" : "warning");

	int models = static_cast<int>(m_other_models.size());
	for (const ModelInfo& model : m_models)
		models += model.installed;
	ui::SetText(W("nav.models.badge"), m_models_loaded ? std::to_string(models) : "");
	ui::SetEnabled(W("nav.models"), OllamaInstalled() || m_page == Page::Models);

	int versions = 0;
	for (const EngineRelease& release : m_releases)
		versions += release.installed;
	for (const LocalEngine& local : m_local_engines)
		versions += local.present;
	ui::SetText(W("nav.engine.badge"), m_releases_loaded || !m_local_engines.empty() ? std::to_string(versions) : "");

	ui::SetText(W("nav.plugins.badge"), m_plugin_engine.empty() ? "" : std::to_string(m_installed_plugins.size()));

	Rid state = W("side.state");
	if (Busy())
	{
		ui::SetText(state, "Travail en cours...");
		SetTone(state, "warning");
	}
	else
	{
		ui::SetText(state, ready ? "Prêt" : "Dépendances à compléter");
		SetTone(state, ready ? "success" : "warning");
	}
}

void Launcher::UpdateProgress()
{
	for (const auto& [tag, bar] : m_release_bars)
		for (const EngineRelease& r : m_releases)
			if (r.tag == tag && r.progress >= 0.0f && Rwidget_is_valid(bar))
				Rprogress_bar_set_value(bar, r.progress);
	for (const auto& [tag, bar] : m_model_bars)
		for (const ModelInfo& m : m_models)
			if (m.tag == tag && m.progress >= 0.0f && Rwidget_is_valid(bar))
				Rprogress_bar_set_value(bar, m.progress);
	for (const auto& [name, bar] : m_plugin_bars)
		for (const CatalogPlugin& c : m_catalog)
			if (c.name == name && c.progress >= 0.0f && Rwidget_is_valid(bar))
				Rprogress_bar_set_value(bar, c.progress);

	Rid task = W("log.task");
	if (!m_task_label.empty())
	{
		ui::SetText(task, m_task_label);
		SetTone(task, "warning");
	}
	else
	{
		ui::SetText(task, std::to_string(m_log.size()) + " lignes");
		SetTone(task, "dim");
	}
}

void Launcher::UpdateLog()
{
	Rid tree = W("log.lines");
	if (m_log_refill)
	{
		Rtree_view_clear_items(tree);
		m_log_shown = m_log_total - m_log.size();
		m_log_refill = false;
	}
	if (m_log_shown >= m_log_total)
		return;

	const size_t first = m_log.size() - static_cast<size_t>(std::min<std::uint64_t>(m_log_total - m_log_shown, m_log.size()));
	Rid last = R_INVALID_ID;
	for (size_t i = first; i < m_log.size(); ++i)
	{
		const std::string& line = m_log[i];
		last = Rtree_view_add_item(tree, R_INVALID_ID, line.c_str());
		if (line.find("chec") != std::string::npos)         // « Échec », « échec »
			Rtree_item_set_text_color(last, 0.95f, 0.45f, 0.45f, 1.0f);
		else if (line.rfind("==", 0) == 0)                   // titres
			Rtree_item_set_text_color(last, 0.96f, 0.62f, 0.25f, 1.0f);
	}
	m_log_shown = m_log_total;
	if (last != R_INVALID_ID)
		Rtree_view_scroll_to_item(tree, last);
}

// -----------------------------------------------------------------------------
// Pages
// -----------------------------------------------------------------------------

void Launcher::BuildDependencies()
{
	Rid pm_error = W("deps.pm_error");
	ui::SetVisible(pm_error, !m_package_manager && !m_package_manager_info.empty());
	ui::SetText(pm_error, m_package_manager_info);

	size_t missing = 0;
	for (const Dependency& dep : m_deps)
		missing += dep.required && (dep.state == DepState::Missing || dep.state == DepState::Failed);
	Rid install = W("deps.install");
	ui::SetText(install, missing == 0 ? "Tout est installé" : "Tout installer (" + std::to_string(missing) + ")");
	ui::SetEnabled(install, !Busy() && missing > 0 && m_package_manager);
	ui::SetEnabled(W("deps.check"), !Busy());
	ui::SetText(W("deps.via"), m_package_manager && !m_package_manager_info.empty() ? "via " + m_package_manager_info : "");

	Rid list = W("deps.list");
	Rwidget_clear_children(list);
	for (size_t i = 0; i < m_deps.size(); ++i)
	{
		const Dependency& dep = m_deps[i];
		Rid row = Make(kDependencyRow);
		if (row == R_INVALID_ID)
			continue;
		Rwidget_add_child(list, row);
		ui::SetText(Child(row, "name"), dep.name);
		ui::SetVisible(Child(row, "optional"), !dep.required);
		ui::SetText(Child(row, "desc"), dep.description);

		Rid state = Child(row, "state");
		std::string text = Dependencies::StateLabel(dep.state);
		if (dep.state == DepState::Installed && !dep.version.empty())
			text += " " + dep.version;
		else if (dep.state == DepState::Missing && !dep.version.empty())
			text += " (" + dep.version + " trouvé)";
		ui::SetText(state, text);
		SetTone(state, StateTone(dep.state));

		Rid install_one = Child(row, "install");
		const bool can_install = dep.state == DepState::Missing || dep.state == DepState::Failed;
		ui::SetVisible(install_one, can_install);
		ui::SetEnabled(install_one, !Busy() && (m_package_manager || !dep.download_url.empty()));
		OnClick(install_one, [this, i] { if (!Busy()) InstallDependencies({ i }); });

		Rid site = Child(row, "site");
		ui::SetTooltip(site, dep.url);
		const std::string url = dep.url;
		OnClick(site, [url] { Process::OpenUrl(url); });
	}
}

void Launcher::BuildModels()
{
	const bool ollama = OllamaInstalled();
	ui::SetVisible(W("models.no_ollama"), !ollama);
	ui::SetVisible(W("models.body"), ollama);
	if (!ollama)
		return;

	Rid server = W("models.server");
	ui::SetText(server, m_server_running ? "Serveur Ollama actif (localhost:11434)"
	                    : m_models_loaded ? "Serveur Ollama non contacté" : "Recherche des modèles...");
	SetTone(server, m_server_running ? "success" : "dim");
	Rid refresh = W("models.refresh");
	ui::SetText(refresh, m_server_running ? "Actualiser" : "Démarrer / actualiser");
	ui::SetEnabled(refresh, !Busy());

	size_t selected = 0;
	for (const ModelInfo& model : m_models)
		selected += model.selected && !model.installed;
	Rid pull = W("models.pull");
	ui::SetText(pull, selected == 0 ? "Aucun modèle sélectionné" : "Télécharger la sélection (" + std::to_string(selected) + ")");
	ui::SetEnabled(pull, !Busy() && selected > 0);
	ui::SetEnabled(W("models.custom.pull"), !Busy());

	m_model_bars.clear();
	Rid list = W("models.list");
	Rwidget_clear_children(list);
	for (size_t i = 0; i < m_models.size(); ++i)
	{
		const ModelInfo& model = m_models[i];
		Rid row = Make(kModelRow);
		if (row == R_INVALID_ID)
			continue;
		Rwidget_add_child(list, row);

		Rid check = Child(row, "select");
		ui::SetChecked(check, model.selected);
		ui::SetEnabled(check, !model.installed && !Busy());
		ui::OnToggle(check, [this, i](bool on) {
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			if (i < m_models.size())
				m_models[i].selected = on;
			Changed();
		});

		ui::SetText(Child(row, "name"), model.tag);
		ui::SetVisible(Child(row, "recommended"), model.recommended);
		ui::SetText(Child(row, "desc"), model.description + " VRAM conseillée : " + model.vram + ".");
		ui::SetText(Child(row, "size"), model.size);

		Rid state = Child(row, "state");
		Rid bar = Child(row, "progress");
		Rid remove = Child(row, "remove");
		if (model.progress >= 0.0f)
		{
			ui::SetText(state, "Téléchargement...");
			SetTone(state, "warning");
			ui::SetVisible(bar, true);
			Rprogress_bar_set_value(bar, model.progress);
			m_model_bars[model.tag] = bar;
		}
		else if (model.installed)
		{
			ui::SetText(state, "Installé");
			SetTone(state, "success");
			ui::SetVisible(remove, true);
			ui::SetEnabled(remove, !Busy());
			const std::string tag = model.tag;
			OnClick(remove, [this, tag] { if (!Busy()) RemoveModel(tag); });
		}
		else
		{
			ui::SetText(state, "Non installé");
			SetTone(state, "dim");
		}
	}

	Rid others = W("models.others");
	Rwidget_clear_children(others);
	ui::SetVisible(W("models.others.title"), !m_other_models.empty());
	for (const std::string& name : m_other_models)
	{
		Rid row = Make(kOtherModelRow);
		if (row == R_INVALID_ID)
			continue;
		Rwidget_add_child(others, row);
		ui::SetText(Child(row, "name"), name);
		Rid remove = Child(row, "remove");
		ui::SetEnabled(remove, !Busy());
		OnClick(remove, [this, name] { if (!Busy()) RemoveModel(name); });
	}
}

void Launcher::BuildEngine()
{
	const bool deps_ok = RequiredInstalled();
	ui::SetVisible(W("engine.deps_warning"), !deps_ok);
	ui::SetEnabled(W("engine.refresh"), !Busy());
	ui::SetEnabled(W("engine.add_local"), !Busy());
	Rid error = W("engine.error");
	ui::SetVisible(error, !m_releases_error.empty());
	ui::SetText(error, m_releases_error);

	// Versions locales
	Rid local_list = W("engine.local");
	Rwidget_clear_children(local_list);
	ui::SetVisible(W("engine.local.section"), !m_local_engines.empty());
	for (const LocalEngine& local : m_local_engines)
	{
		Rid card = Make(kLocalEngineCard);
		if (card == R_INVALID_ID)
			continue;
		Rwidget_add_child(local_list, card);
		ui::SetText(Child(card, "name"), local.name);
		Rid state = Child(card, "state");
		ui::SetText(state, local.present ? (local.version.empty() ? "version inconnue" : "Lynx " + local.version)
		                                 : "LynxEditor.exe introuvable");
		SetTone(state, local.present ? "dim" : "error");
		std::error_code ec;
		const bool sdk = std::filesystem::is_regular_file(Engine::SdkConfigDirectory(local.tag) / "LynxConfig.cmake", ec);
		ui::SetText(Child(card, "info"), local.folder.string() +
		            (sdk ? "" : "   (pas de SDK : les projets C++ ne la trouvent pas avec find_package)"));

		const std::string tag = local.tag;
		const std::filesystem::path folder = local.folder;
		Rid launch = Child(card, "launch");
		ui::SetEnabled(launch, local.present);
		OnClick(launch, [this, tag] { Engine::Launch(tag, [this](const std::string& l) { LogLocked(l); }); });
		OnClick(Child(card, "folder"), [folder] { Process::OpenFolder(folder); });
		Rid remove = Child(card, "remove");
		ui::SetEnabled(remove, !Busy());
		OnClick(remove, [this, tag] { if (!Busy()) RemoveLocalEngine(tag); });
	}

	// Versions publiées
	m_release_bars.clear();
	Rid list = W("engine.list");
	Rwidget_clear_children(list);
	Rid empty = W("engine.empty");
	ui::SetVisible(empty, m_releases.empty());
	ui::SetText(empty, m_releases_loaded ? "Aucune version publiée pour l'instant." : "Lecture des versions...");

	for (size_t i = 0; i < m_releases.size(); ++i)
	{
		const EngineRelease& release = m_releases[i];
		const bool installing = release.progress >= 0.0f;
		Rid card = Make(kReleaseCard);
		if (card == R_INVALID_ID)
			continue;
		Rwidget_add_child(list, card);

		ui::SetText(Child(card, "version"), "Lynx " + release.version);
		Rid kind = Child(card, "kind");
		ui::SetText(kind, release.prerelease ? "Beta" : "Stable");
		SetTone(kind, release.prerelease ? "warning" : "success");
		ui::SetVisible(Child(card, "latest"), i == 0);

		std::string info = release.date;
		if (release.asset_size > 0)
			info += "  |  " + Engine::FormatSize(release.asset_size);
		if (!release.name.empty() && release.name != release.tag)
			info += "  |  " + release.name;
		ui::SetText(Child(card, "info"), info);

		Rid state = Child(card, "state");
		if (installing)
		{
			ui::SetText(state, "Installation...");
			SetTone(state, "warning");
		}
		else if (release.installed)
		{
			ui::SetText(state, "Installée");
			SetTone(state, "success");
		}
		else
		{
			ui::SetText(state, release.asset_url.empty() ? "Pas d'archive Windows" : "Non installée");
			SetTone(state, "dim");
		}

		// Notes de version (Markdown), dépliées à la demande
		Rid notes = Child(card, "notes");
		Rlabel_set_markdown(notes, R_TRUE);
		std::string text = release.body.empty() ? release.name : release.body;
		ui::SetText(notes, text.empty() ? std::string("Pas de notes pour cette version.") : text);
		const std::string tag = release.tag;
		const bool open = m_open_notes.count(tag) > 0;
		ui::SetVisible(notes, open);
		Rid toggle = Child(card, "notes.toggle");
		ui::SetText(toggle, open ? "Masquer les notes" : "Notes de version");
		ui::OnClick(toggle, [this, tag, notes, toggle] {
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			const bool now_open = m_open_notes.count(tag) == 0;
			if (now_open)
				m_open_notes.insert(tag);
			else
				m_open_notes.erase(tag);
			ui::SetVisible(notes, now_open);
			ui::SetText(toggle, now_open ? "Masquer les notes" : "Notes de version");
		});
		const std::string page_url = release.page_url;
		OnClick(Child(card, "page"), [page_url] { Process::OpenUrl(page_url); });

		// Actions
		if (installing)
		{
			Rid bar = Child(card, "progress");
			ui::SetVisible(bar, true);
			ui::SetVisible(Child(card, "spacer"), false);
			Rprogress_bar_set_value(bar, release.progress);
			m_release_bars[tag] = bar;
		}
		else if (release.installed)
		{
			Rid launch = Child(card, "launch");
			Rid folder = Child(card, "folder");
			Rid remove = Child(card, "remove");
			ui::SetVisible(launch, true);
			ui::SetVisible(folder, true);
			ui::SetVisible(remove, true);
			ui::SetEnabled(launch, !Busy());
			ui::SetEnabled(remove, !Busy());
			OnClick(launch, [this, tag] { Engine::Launch(tag, [this](const std::string& l) { LogLocked(l); }); });
			OnClick(folder, [tag] { Process::OpenFolder(Engine::VersionDirectory(tag)); });
			OnClick(remove, [this, i] { if (!Busy() && i < m_releases.size()) UninstallRelease(i); });
		}
		else
		{
			Rid install = Child(card, "install");
			ui::SetVisible(install, true);
			ui::SetEnabled(install, !Busy() && deps_ok && !release.asset_url.empty());
			if (!deps_ok)
				ui::SetTooltip(install, "Installe d'abord les dépendances.");
			OnClick(install, [this, i] {
				if (!Busy() && RequiredInstalled() && i < m_releases.size())
					InstallRelease(i);
			});
		}
	}
}

void Launcher::BuildPlugins()
{
	m_updating = true;   // les combos remplis ici ne déclenchent rien

	ui::SetVisible(W("plugins.no_engine"), m_engines.empty());

	// Version cible
	std::vector<std::string> engine_items;
	for (size_t i = 0; i < m_engines.size(); ++i)
		engine_items.push_back(Engine::IsLocalTag(m_engines[i].tag) ? EngineLabel(m_engines[i].tag)
		                       : m_engines[i].tag + (i == 0 ? " (dernière)" : ""));
	Rid engine_combo = W("plugins.engine");
	if (engine_items != m_engine_items)
	{
		m_engine_items = engine_items;
		Rcombo_box_clear_items(engine_combo);
		for (const std::string& item : engine_items)
			Rcombo_box_add_item(engine_combo, item.c_str());
	}
	for (size_t i = 0; i < m_engines.size(); ++i)
		if (m_engines[i].tag == m_plugin_engine && Rcombo_box_get_selected(engine_combo) != static_cast<int>(i))
			Rcombo_box_set_selected(engine_combo, static_cast<int>(i));
	ui::SetEnabled(engine_combo, !Busy() && !m_engines.empty());
	ui::SetEnabled(W("plugins.refresh"), !Busy());
	ui::SetEnabled(W("plugins.import"), !Busy() && !m_plugin_engine.empty());
	ui::SetEnabled(W("plugins.folder"), !m_plugin_engine.empty());

	// Catégories
	std::vector<std::string> categories;
	for (const CatalogPlugin& c : m_catalog)
		if (!c.category.empty() && std::find(categories.begin(), categories.end(), c.category) == categories.end())
			categories.push_back(c.category);
	for (const InstalledPlugin& p : m_installed_plugins)
		if (!p.category.empty() && std::find(categories.begin(), categories.end(), p.category) == categories.end())
			categories.push_back(p.category);
	std::sort(categories.begin(), categories.end());
	Rid category_combo = W("plugins.category");
	if (categories != m_category_items || Rcombo_box_get_selected(category_combo) < 0)
	{
		m_category_items = categories;
		Rcombo_box_clear_items(category_combo);
		Rcombo_box_add_item(category_combo, "Toutes les catégories");
		for (const std::string& c : categories)
			Rcombo_box_add_item(category_combo, c.c_str());
	}
	int category_index = 0;
	for (size_t i = 0; i < categories.size(); ++i)
		if (categories[i] == m_plugin_category)
			category_index = static_cast<int>(i) + 1;
	if (category_index == 0)
		m_plugin_category.clear();
	if (Rcombo_box_get_selected(category_combo) != category_index)
		Rcombo_box_set_selected(category_combo, category_index);

	m_updating = false;

	Rid error = W("plugins.error");
	ui::SetVisible(error, !m_catalog_error.empty());
	ui::SetText(error, m_catalog_error);

	// Liste : catalogue + plugins installés absents du catalogue (intégrés, importés)
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

	Rid empty = W("plugins.empty");
	ui::SetVisible(empty, entries.empty());
	ui::SetText(empty, !m_catalog_loaded ? "Lecture du catalogue..."
	                   : (needle.empty() && m_plugin_category.empty()) ? "Aucun plugin."
	                   : "Aucun plugin ne correspond à la recherche.");

	m_plugin_bars.clear();
	Rid list = W("plugins.list");
	Rwidget_clear_children(list);
	const std::string engine_version = PluginEngineVersion();
	const std::string engine_label = EngineLabel(m_plugin_engine);

	for (const Entry& entry : entries)
	{
		const CatalogPlugin* catalog = entry.catalog;
		const InstalledPlugin* installed = entry.installed;
		const size_t catalog_index = entry.index;

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

		Rid card = Make(kPluginCard);
		if (card == R_INVALID_ID)
			continue;
		Rwidget_add_child(list, card);

		ui::SetText(Child(card, "name"), name);
		ui::SetText(Child(card, "category"), category);
		Rid badge = Child(card, "badge");
		if (builtin)
		{
			ui::SetText(badge, "Intégré");
			ui::SetVisible(badge, true);
		}
		else if (installed && installed->source == "local")
		{
			ui::SetText(badge, "Local");
			ui::SetVisible(badge, true);
		}
		ui::SetVisible(Child(card, "beta"), best && best->prerelease);

		std::string info;
		if (!author.empty())
			info = "par " + author;
		if (catalog)
			info += (info.empty() ? "" : "  |  ") + catalog->repo;
		ui::SetText(Child(card, "info"), info);
		ui::SetVisible(Child(card, "info"), !info.empty());

		// État
		Rid state = Child(card, "state");
		std::string state_text;
		const char* tone = "dim";
		if (installing)
		{
			state_text = "Installation...";
			tone = "warning";
		}
		else if (update)
		{
			state_text = "Mise à jour : " + (installed->version.empty() ? std::string("?") : installed->version) + " -> " + best->version;
			tone = "warning";
		}
		else if (installed)
		{
			state_text = "Installé " + installed->version;
			tone = "success";
		}
		else if (best)
		{
			state_text = "Non installé  |  " + best->version;
			if (best->asset_size)
				state_text += "  |  " + Engine::FormatSize(best->asset_size);
		}
		else if (catalog && !catalog->error.empty())
		{
			state_text = "Dépôt illisible";
			tone = "error";
			ui::SetTooltip(state, catalog->error);
		}
		else if (catalog && !catalog->releases.empty())
		{
			// des releases, mais aucune pour cette version : la plus ancienne version du moteur demandée
			std::string needed;
			for (const PluginRelease& r : catalog->releases)
				if (!r.asset_url.empty() && (m_plugin_prerelease || !r.prerelease) &&
				    (needed.empty() || PluginStore::CompareVersions(r.engine_min, needed) < 0))
					needed = r.engine_min;
			state_text = needed.empty() ? "Aucune release téléchargeable" : "Demande Lynx " + needed + " ou plus";
			tone = "error";
		}
		else
		{
			state_text = engine_version.empty() ? "Aucune version du moteur" : "Aucune release publiée";
		}
		ui::SetText(state, state_text);
		SetTone(state, tone);

		if (older_engine && !builtin)
		{
			Rid older = Child(card, "older");
			ui::SetText(older, "Conçu pour Lynx " + engine_min + " : l'éditeur préviendra à l'activation.");
			ui::SetVisible(older, true);
		}

		Rid desc = Child(card, "desc");
		Rlabel_set_markdown(desc, R_TRUE);
		ui::SetText(desc, description.empty() ? std::string("Pas de description.") : description);

		// Actions
		if (installing)
		{
			Rid bar = Child(card, "progress");
			ui::SetVisible(bar, true);
			Rprogress_bar_set_value(bar, catalog->progress);
			m_plugin_bars[name] = bar;
			continue;
		}
		if (catalog && best && !builtin && (!installed || update))
		{
			Rid install = Child(card, "install");
			ui::SetVisible(install, true);
			ui::SetText(install, update ? "Mettre à jour" : "Installer");
			ui::SetEnabled(install, !Busy());
			ui::SetTooltip(install, best->tag + " (" + best->date + ") dans " + engine_label);
			const std::string tag = best->tag;
			OnClick(install, [this, catalog_index, tag] {
				if (!Busy() && catalog_index < m_catalog.size())
					InstallPlugin(catalog_index, tag);
			});
		}
		if (installed)
		{
			Rid folder = Child(card, "folder");
			ui::SetVisible(folder, true);
			const std::filesystem::path path = installed->folder;
			OnClick(folder, [path] { Process::OpenFolder(path); });
			if (!builtin)
			{
				Rid remove = Child(card, "remove");
				ui::SetVisible(remove, true);
				ui::SetEnabled(remove, !Busy());
				ui::SetTooltip(remove, "Supprimer ce plugin de " + engine_label);
				const InstalledPlugin plugin = *installed;
				OnClick(remove, [this, plugin] { if (!Busy()) RemovePlugin(plugin); });
			}
		}
		if (catalog)
		{
			Rid repo = Child(card, "repo");
			ui::SetVisible(repo, true);
			const std::string url = best && !best->page_url.empty() ? best->page_url : "https://github.com/" + catalog->repo;
			OnClick(repo, [url] { Process::OpenUrl(url); });
		}
	}
}
