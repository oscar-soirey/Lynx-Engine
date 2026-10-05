#include "Launcher.h"
#include "Process.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cstdlib>

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
		float h = ImGui::GetTextLineHeight();
		ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5, p.y + h * 0.5f), 4.5f,
		                                           ImGui::ColorConvertFloat4ToU32(color));
		ImGui::Dummy(ImVec2(14, h));
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
		m_other_models.clear();
		for (ModelInfo& model : m_models)
			model.installed = false;
		for (const std::string& name : installed)
		{
			bool known = false;
			for (ModelInfo& model : m_models)
				if (Models::SameModel(model.tag, name))
					model.installed = known = true;
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

	float log_height = m_show_log ? 190.0f : 34.0f;
	ImGui::BeginGroup();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
	ImGui::BeginChild("##content", ImVec2(0, -log_height), ImGuiChildFlags_AlwaysUseWindowPadding);
	switch (m_page)
	{
	case Page::Dependencies: DrawDependencies(); break;
	case Page::Models:       DrawModels();       break;
	case Page::Engine:       DrawEngine();       break;
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();
	DrawLog();
	ImGui::EndGroup();

	ImGui::End();
}

void Launcher::DrawSidebar()
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.08f, 0.095f, 1));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 20));
	ImGui::BeginChild("##sidebar", ImVec2(220, 0), ImGuiChildFlags_AlwaysUseWindowPadding);

	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);   // ImGui 1.92 : taille dynamique
	ImGui::TextColored(kAccent, "LYNX");
	ImGui::PopFont();
	ImGui::TextColored(kMuted, "Launcher");
	ImGui::Dummy(ImVec2(0, 18));

	auto nav = [this](const char* label, Page page, bool enabled, const char* badge, ImVec4 badge_color) {
		bool active = m_page == page;
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.17f, 0.18f, 0.21f, 1));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.14f, 0.15f, 0.18f, 1));
		ImGui::BeginDisabled(!enabled);
		ImVec2 pos = ImGui::GetCursorScreenPos();
		if (ImGui::Selectable(label, active, 0, ImVec2(0, 30)))
			m_page = page;
		if (active)
			ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + 3, pos.y + 30),
			                                         ImGui::ColorConvertFloat4ToU32(kAccent));
		if (badge)
		{
			ImVec2 size = ImGui::CalcTextSize(badge);
			ImGui::GetWindowDrawList()->AddText(
				ImVec2(pos.x + ImGui::GetContentRegionAvail().x - size.x - 6, pos.y + (30 - size.y) * 0.5f),
				ImGui::ColorConvertFloat4ToU32(badge_color), badge);
		}
		ImGui::EndDisabled();
		ImGui::PopStyleColor(2);
	};

	int installed = 0;
	for (const Dependency& dep : m_deps)
		installed += dep.state == DepState::Installed;
	std::string dep_badge = std::to_string(installed) + "/" + std::to_string(m_deps.size());
	bool ready = RequiredInstalled();

	int models = 0;
	for (const ModelInfo& model : m_models)
		models += model.installed;
	models += static_cast<int>(m_other_models.size());
	std::string model_badge = std::to_string(models);

	nav("   Dépendances", Page::Dependencies, true, dep_badge.c_str(), ready ? kGreen : kYellow);
	nav("   Modèles IA", Page::Models, OllamaInstalled(), model_badge.c_str(), kMuted);
	nav("   Moteur", Page::Engine, true, ready ? nullptr : "verrouillé", kMuted);

	// bas de la barre : état général
	float bottom = ImGui::GetWindowHeight() - 70;
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
		ImGui::TextUnformatted(ready ? "Prêt" : "Dépendances manquantes");
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
		ImGui::TextColored(kRed, "%s", m_package_manager_info.c_str());
		ImGui::Spacing();
	}

	std::vector<size_t> missing;
	for (size_t i = 0; i < m_deps.size(); ++i)
		if (m_deps[i].state == DepState::Missing || m_deps[i].state == DepState::Failed)
			missing.push_back(i);

	ImGui::BeginDisabled(Busy() || missing.empty() || !m_package_manager);
	std::string install_label = missing.empty() ? "Tout est installé"
	                                            : "Tout installer (" + std::to_string(missing.size()) + ")";
	if (AccentButton(install_label.c_str(), ImVec2(200, 32)))
		InstallDependencies(missing);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(Busy());
	if (ImGui::Button("Revérifier", ImVec2(110, 32)))
		CheckAll();
	ImGui::EndDisabled();
	if (!m_package_manager_info.empty() && m_package_manager)
	{
		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
		ImGui::TextColored(kMuted, "via %s", m_package_manager_info.c_str());
	}
	ImGui::Dummy(ImVec2(0, 10));

	ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
	if (ImGui::BeginTable("##deps", 4, flags))
	{
		ImGui::TableSetupColumn("Outil", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("État", ImGuiTableColumnFlags_WidthFixed, 190);
		ImGui::TableSetupColumn("##action", ImGuiTableColumnFlags_WidthFixed, 110);
		ImGui::TableSetupColumn("##link", ImGuiTableColumnFlags_WidthFixed, 40);
		ImGui::TableHeadersRow();

		for (size_t i = 0; i < m_deps.size(); ++i)
		{
			Dependency& dep = m_deps[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::TableNextRow(0, 52);

			ImGui::TableNextColumn();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
			ImGui::TextUnformatted(dep.name.c_str());
			ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
			ImGui::TextWrapped("%s", dep.description.c_str());
			ImGui::PopStyleColor();

			ImGui::TableNextColumn();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);
			StatusDot(StateColor(dep.state));
			if (dep.state == DepState::Installed)
				ImGui::Text("%s %s", Dependencies::StateLabel(dep.state), dep.version.c_str());
			else if (dep.state == DepState::Missing && !dep.version.empty())
				ImGui::TextColored(kRed, "%s (%s trouvé)", Dependencies::StateLabel(dep.state), dep.version.c_str());
			else
				ImGui::TextUnformatted(Dependencies::StateLabel(dep.state));

			ImGui::TableNextColumn();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
			if (dep.state == DepState::Missing || dep.state == DepState::Failed)
			{
				ImGui::BeginDisabled(Busy() || !m_package_manager);
				if (ImGui::Button("Installer", ImVec2(100, 0)))
					InstallDependencies({ i });
				ImGui::EndDisabled();
			}

			ImGui::TableNextColumn();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
			if (ImGui::SmallButton("web"))
				Process::OpenUrl(dep.url);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", dep.url.c_str());

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::Dummy(ImVec2(0, 8));
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
		ImGui::TextColored(kYellow, "Ollama n'est pas installé : installe-le depuis l'onglet Dépendances.");
		return;
	}

	// état du serveur
	StatusDot(m_server_running ? kGreen : kMuted);
	ImGui::TextUnformatted(m_server_running ? "Serveur Ollama actif (localhost:11434)"
	                                        : "Serveur Ollama non contacté");
	ImGui::SameLine();
	ImGui::BeginDisabled(Busy());
	if (ImGui::SmallButton(m_server_running ? "Actualiser" : "Démarrer / actualiser"))
		RefreshModels();
	ImGui::EndDisabled();
	ImGui::Dummy(ImVec2(0, 6));

	// première ouverture : on interroge Ollama une fois
	static bool first = true;
	if (first && !Busy())
	{
		first = false;
		RefreshModels();
	}

	std::vector<std::string> selected;
	for (const ModelInfo& model : m_models)
		if (model.selected && !model.installed)
			selected.push_back(model.tag);

	ImGui::BeginDisabled(Busy() || selected.empty());
	std::string pull_label = selected.empty() ? "Aucun modèle sélectionné"
	                                          : "Télécharger la sélection (" + std::to_string(selected.size()) + ")";
	if (AccentButton(pull_label.c_str(), ImVec2(240, 32)))
		PullModels(selected);
	ImGui::EndDisabled();
	ImGui::Dummy(ImVec2(0, 8));

	ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
	if (ImGui::BeginTable("##models", 5, flags))
	{
		ImGui::TableSetupColumn("##sel", ImGuiTableColumnFlags_WidthFixed, 28);
		ImGui::TableSetupColumn("Modèle", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Taille", ImGuiTableColumnFlags_WidthFixed, 70);
		ImGui::TableSetupColumn("VRAM conseillée", ImGuiTableColumnFlags_WidthFixed, 110);
		ImGui::TableSetupColumn("État", ImGuiTableColumnFlags_WidthFixed, 170);
		ImGui::TableHeadersRow();

		for (size_t i = 0; i < m_models.size(); ++i)
		{
			ModelInfo& model = m_models[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::TableNextRow(0, 50);

			ImGui::TableNextColumn();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
			ImGui::BeginDisabled(model.installed || Busy());
			ImGui::Checkbox("##s", &model.selected);
			ImGui::EndDisabled();

			ImGui::TableNextColumn();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
			ImGui::TextUnformatted(model.tag.c_str());
			if (model.recommended)
			{
				ImGui::SameLine();
				ImGui::TextColored(kAccent, "conseillé");
			}
			ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
			ImGui::TextWrapped("%s", model.description.c_str());
			ImGui::PopStyleColor();

			ImGui::TableNextColumn();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);
			ImGui::TextUnformatted(model.size.c_str());

			ImGui::TableNextColumn();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);
			ImGui::TextUnformatted(model.vram.c_str());

			ImGui::TableNextColumn();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
			if (model.progress >= 0.0f)
			{
				char overlay[16];
				snprintf(overlay, sizeof(overlay), "%d %%", int(model.progress * 100));
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
				ImGui::ProgressBar(model.progress, ImVec2(-1, 0), overlay);
				ImGui::PopStyleColor();
			}
			else if (model.installed)
			{
				StatusDot(kGreen);
				ImGui::TextUnformatted("Installé");
				ImGui::SameLine();
				ImGui::BeginDisabled(Busy());
				if (ImGui::SmallButton("Supprimer"))
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

	ImGui::Dummy(ImVec2(0, 10));
	ImGui::TextUnformatted("Autre modèle Ollama");
	ImGui::SetNextItemWidth(260);
	ImGui::InputTextWithHint("##custom", "ex : llama3.1:8b", m_custom_model, sizeof(m_custom_model));
	ImGui::SameLine();
	ImGui::BeginDisabled(Busy() || m_custom_model[0] == '\0');
	if (ImGui::Button("Télécharger"))
	{
		PullModels({ m_custom_model });
		m_custom_model[0] = '\0';
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::SmallButton("Catalogue Ollama"))
		Process::OpenUrl("https://ollama.com/library");

	if (!m_other_models.empty())
	{
		ImGui::Dummy(ImVec2(0, 6));
		ImGui::TextColored(kMuted, "Autres modèles installés :");
		for (const std::string& name : m_other_models)
		{
			ImGui::PushID(name.c_str());
			ImGui::BulletText("%s", name.c_str());
			ImGui::SameLine();
			ImGui::BeginDisabled(Busy());
			if (ImGui::SmallButton("Supprimer"))
				RemoveModel(name);
			ImGui::EndDisabled();
			ImGui::PopID();
		}
	}
}

void Launcher::DrawEngine()
{
	SectionTitle("Moteur",
	             "Installation d'une version de l'éditeur Lynx (LynxEditor, LynxRuntime).");

	if (!RequiredInstalled())
	{
		StatusDot(kYellow);
		ImGui::TextUnformatted("Installe d'abord toutes les dépendances pour débloquer cette étape.");
		ImGui::Spacing();
		for (const Dependency& dep : m_deps)
			if (dep.required && dep.state != DepState::Installed)
				ImGui::BulletText("%s", dep.name.c_str());
		ImGui::Spacing();
		if (ImGui::Button("Aller aux dépendances"))
			m_page = Page::Dependencies;
		return;
	}

	StatusDot(kGreen);
	ImGui::TextUnformatted("Toutes les dépendances sont installées.");
	ImGui::Dummy(ImVec2(0, 10));

	// Emplacement prévu pour la liste des releases GitHub.
	ImGui::BeginDisabled();
	static int version = 0;
	const char* versions[] = { "(les versions apparaîtront ici)" };
	ImGui::SetNextItemWidth(300);
	ImGui::Combo("Version", &version, versions, 1);
	AccentButton("Installer cette version", ImVec2(220, 32));
	ImGui::EndDisabled();
	ImGui::Spacing();
	ImGui::TextColored(kMuted, "Bientôt : les versions seront récupérées depuis les releases GitHub.");
}

void Launcher::DrawLog()
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.065f, 0.075f, 1));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 8));
	ImGui::BeginChild("##logpanel", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);

	if (ImGui::SmallButton(m_show_log ? "v Journal" : "> Journal"))
		m_show_log = !m_show_log;
	ImGui::SameLine();
	if (!m_task_label.empty())
		ImGui::TextColored(kYellow, "%s", m_task_label.c_str());
	else
		ImGui::TextColored(kMuted, "%d lignes", static_cast<int>(m_log.size()));
	ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 140);
	if (ImGui::SmallButton("Copier"))
	{
		std::string all;
		for (const std::string& line : m_log)
			all += line + "\n";
		ImGui::SetClipboardText(all.c_str());
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Effacer"))
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
