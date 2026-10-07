#include "PluginStore.h"
#include "Engine.h"
#include "Process.h"

#include <json/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>

#ifdef _WIN32
	#include <windows.h>
	#include <shobjidl.h>
#endif

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace
{
	std::string Quote(const fs::path& path)
	{
		return "\"" + path.string() + "\"";
	}

	std::string Str(const Json& object, const char* key)
	{
		if (!object.is_object())
			return {};
		auto it = object.find(key);
		return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
	}

	Json ReadJson(const fs::path& file)
	{
		std::ifstream in(file, std::ios::binary);
		if (!in)
			return Json();
		return Json::parse(in, nullptr, false);
	}

	bool WriteJson(const fs::path& file, const Json& json)
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out << json.dump(2) << "\n";
		return static_cast<bool>(out);
	}

	// Dossier de travail pour les téléchargements / extractions (même disque que les versions :
	// le déplacement final est un simple renommage).
	fs::path WorkDirectory()
	{
		return Engine::VersionsDirectory() / ".plugin-download";
	}

	// Nom du dossier d'un plugin : "name" de son plugin.json, sinon `fallback`.
	std::string PluginName(const fs::path& root, const std::string& fallback)
	{
		std::string name = Str(ReadJson(root / "plugin.json"), "name");
		if (name.empty())
			name = fallback;
		// pas de séparateurs ni de caractères interdits dans un nom de dossier Windows
		name.erase(std::remove_if(name.begin(), name.end(), [](char c) {
			return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
		}), name.end());
		return name;
	}

	bool IsBuiltin(const fs::path& folder)
	{
		std::error_code error;
		return fs::is_regular_file(folder / "plugin.json", error) &&
		       !fs::exists(folder / PluginStore::kInstallFile, error);
	}

	// Place `root` (dossier contenant plugin.json) dans plugins\<name> de la version.
	// move : renommer (dossier temporaire), sinon copier (dossier de l'utilisateur).
	bool PlaceFolder(const fs::path& root, const std::string& name, const std::string& engine_tag,
	                 bool move, const LogFn& log)
	{
		const fs::path target = PluginStore::PluginsDirectory(engine_tag) / name;
		std::error_code error;

		if (IsBuiltin(target))
		{
			log("Échec : « " + name + " » est un plugin intégré à Lynx " + engine_tag + ", il n'est pas remplacé.");
			return false;
		}
		if (fs::equivalent(root, target, error))
		{
			log("Ce plugin est déjà dans " + target.string() + ".");
			return true;
		}

		fs::create_directories(target.parent_path(), error);
		fs::remove_all(target, error);
		if (error)
		{
			log("Échec : impossible de remplacer " + target.string() + " : " + error.message() +
			    " (l'éditeur de cette version est peut-être ouvert).");
			return false;
		}

		error.clear();
		if (move)
			fs::rename(root, target, error);
		if (!move || error)
		{
			error.clear();
			fs::copy(root, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
		}
		if (error)
		{
			log("Échec de la copie vers " + target.string() + " : " + error.message());
			return false;
		}
		return true;
	}

	bool Download(const std::string& url, const fs::path& file, const LogFn& log,
	              const std::function<void(float)>& on_progress)
	{
		static const std::regex percent(R"((\d{1,3}(?:\.\d)?)%)");
		int code = Process::Run(
			"curl -L --fail -# -H \"User-Agent: LynxLauncher\" -o " + Quote(file) + " \"" + url + "\"",
			[&](const std::string& line) {
				std::smatch match;
				if (std::regex_search(line, match, percent))
					on_progress(std::stof(match.str(1)) / 100.0f * 0.85f);
				else if (line.find_first_not_of(" #") != std::string::npos)
					log("  " + line);
			});
		if (code != 0)
		{
			log("Échec du téléchargement (curl code " + std::to_string(code) + ").");
			return false;
		}
		return true;
	}

	bool Extract(const fs::path& archive, const fs::path& dir, const LogFn& log)
	{
		std::error_code error;
		fs::remove_all(dir, error);
		fs::create_directories(dir, error);
		// tar.exe (bsdtar) de Windows 10/11 lit les .zip
		int code = Process::Run("tar -xf " + Quote(archive) + " -C " + Quote(dir),
		                        [&](const std::string& line) { log("  " + line); });
		if (code != 0)
		{
			log("Échec de l'extraction de " + archive.filename().string() + ".");
			return false;
		}
		return true;
	}

	std::string Lower(std::string text)
	{
		for (char& c : text)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return text;
	}
}

namespace PluginStore
{
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
			// saute le séparateur (ou tout caractère non numérique)
			if (i < a.size()) ++i;
			if (j < b.size()) ++j;
		}
		return 0;
	}

	bool ParseTag(const std::string& tag, std::string& version, std::string& engine_min)
	{
		static const std::regex pattern(R"(^[vV]?(\d+(?:\.\d+)*)-[vV]?(\d+(?:\.\d+)*)$)");
		std::smatch match;
		if (!std::regex_match(tag, match, pattern))
			return false;
		version = match.str(1);
		engine_min = match.str(2);
		return true;
	}

	const PluginRelease* BestRelease(const CatalogPlugin& plugin, const std::string& engine, bool allow_prerelease)
	{
		const PluginRelease* best = nullptr;
		for (const PluginRelease& r : plugin.releases)
		{
			if (r.prerelease && !allow_prerelease)
				continue;
			if (r.asset_url.empty() || CompareVersions(r.engine_min, engine) > 0)
				continue;
			if (!best)
			{
				best = &r;
				continue;
			}
			int v = CompareVersions(r.version, best->version);
			// même version du plugin : celle faite pour le moteur le plus proche
			if (v > 0 || (v == 0 && CompareVersions(r.engine_min, best->engine_min) > 0))
				best = &r;
		}
		return best;
	}

	std::vector<InstalledEngine> ListEngines()
	{
		std::vector<InstalledEngine> out;
		std::error_code error;
		for (const auto& entry : fs::directory_iterator(Engine::VersionsDirectory(), error))
		{
			std::error_code e;
			if (!entry.is_directory(e))
				continue;
			std::string tag = entry.path().filename().string();
			if (tag.empty() || tag[0] == '.' || !Engine::IsInstalled(tag))
				continue;
			InstalledEngine engine;
			engine.tag = tag;
			engine.version = (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
			out.push_back(std::move(engine));
		}
		std::sort(out.begin(), out.end(), [](const InstalledEngine& a, const InstalledEngine& b) {
			return CompareVersions(a.version, b.version) > 0;
		});
		return out;
	}

	fs::path PluginsDirectory(const std::string& engine_tag)
	{
		return Engine::VersionDirectory(engine_tag) / "plugins";
	}

	std::vector<InstalledPlugin> ListInstalled(const std::string& engine_tag)
	{
		std::vector<InstalledPlugin> out;
		std::error_code error;
		for (const auto& entry : fs::directory_iterator(PluginsDirectory(engine_tag), error))
		{
			std::error_code e;
			if (!entry.is_directory(e) || !fs::is_regular_file(entry.path() / "plugin.json", e))
				continue;

			const Json desc = ReadJson(entry.path() / "plugin.json");
			InstalledPlugin p;
			p.folder = entry.path();
			p.name = Str(desc, "name");
			if (p.name.empty())
				p.name = entry.path().filename().string();
			p.version = Str(desc, "version");
			p.description = Str(desc, "description");
			p.category = Str(desc, "category");
			p.author = Str(desc, "author");

			const fs::path install = entry.path() / kInstallFile;
			if (fs::is_regular_file(install, e))
			{
				const Json info = ReadJson(install);
				p.source = Str(info, "source");
				p.repo = Str(info, "repo");
				p.tag = Str(info, "tag");
				p.engine_min = Str(info, "engine_min");
				if (!Str(info, "version").empty())
					p.version = Str(info, "version");
			}
			else
				p.builtin = true;
			if (p.engine_min.empty())
				p.engine_min = Str(desc, "engine_min");

			out.push_back(std::move(p));
		}
		std::sort(out.begin(), out.end(), [](const InstalledPlugin& a, const InstalledPlugin& b) {
			return Lower(a.name) < Lower(b.name);
		});
		return out;
	}

	bool ParseRegistry(const std::string& json_text, std::vector<CatalogPlugin>& out, std::string& error)
	{
		const Json root = Json::parse(json_text, nullptr, false);
		// [ {...} ] ou { "plugins": [ {...} ] }
		const Json* list = &root;
		if (root.is_object() && root.contains("plugins"))
			list = &root["plugins"];
		if (!list->is_array())
		{
			error = "registry/plugins.json illisible (une liste de plugins est attendue).";
			return false;
		}
		out.clear();
		for (const Json& item : *list)
		{
			CatalogPlugin p;
			p.name = Str(item, "name");
			p.repo = Str(item, "repo");
			// accepte aussi une URL complète https://github.com/owner/name
			const std::string prefix = "https://github.com/";
			if (p.repo.rfind(prefix, 0) == 0)
				p.repo = p.repo.substr(prefix.size());
			while (!p.repo.empty() && p.repo.back() == '/')
				p.repo.pop_back();
			if (p.repo.size() > 4 && p.repo.substr(p.repo.size() - 4) == ".git")
				p.repo.resize(p.repo.size() - 4);
			if (p.name.empty() || p.repo.find('/') == std::string::npos)
				continue;
			p.description = Str(item, "description");
			p.category = Str(item, "category");
			p.author = Str(item, "author");
			out.push_back(std::move(p));
		}
		std::sort(out.begin(), out.end(), [](const CatalogPlugin& a, const CatalogPlugin& b) {
			return Lower(a.name) < Lower(b.name);
		});
		return true;
	}

	bool ParseReleases(const std::string& json_text, std::vector<PluginRelease>& out, std::string& error)
	{
		const Json root = Json::parse(json_text, nullptr, false);
		if (!root.is_array())
		{
			error = root.is_object() ? Str(root, "message") : std::string("réponse GitHub illisible");
			if (error.empty())
				error = "réponse GitHub inattendue";
			return false;
		}
		out.clear();
		for (const Json& item : root)
		{
			if (!item.is_object())
				continue;
			auto draft = item.find("draft");
			if (draft != item.end() && draft->is_boolean() && draft->get<bool>())
				continue;

			PluginRelease r;
			r.tag = Str(item, "tag_name");
			if (!ParseTag(r.tag, r.version, r.engine_min))
				continue;   // tag d'une autre forme : ignoré
			r.date = Str(item, "published_at").substr(0, 10);
			r.body = Str(item, "body");
			r.page_url = Str(item, "html_url");
			auto pre = item.find("prerelease");
			r.prerelease = pre != item.end() && pre->is_boolean() && pre->get<bool>();

			// premier .zip de la release
			auto assets = item.find("assets");
			if (assets != item.end() && assets->is_array())
				for (const Json& asset : *assets)
				{
					const std::string name = Lower(Str(asset, "name"));
					if (name.size() < 4 || name.substr(name.size() - 4) != ".zip")
						continue;
					r.asset_url = Str(asset, "browser_download_url");
					auto size = asset.find("size");
					if (size != asset.end() && size->is_number_unsigned())
						r.asset_size = size->get<std::uint64_t>();
					break;
				}
			out.push_back(std::move(r));
		}
		std::sort(out.begin(), out.end(), [](const PluginRelease& a, const PluginRelease& b) {
			int v = CompareVersions(a.version, b.version);
			return v != 0 ? v > 0 : CompareVersions(a.engine_min, b.engine_min) > 0;
		});
		return true;
	}

	bool FetchCatalog(std::vector<CatalogPlugin>& out, std::string& error, const LogFn& log)
	{
		log("> curl " + std::string(kRegistryUrl));
		int code = 0;
		std::string text = Process::Capture(
			std::string("curl -sSL --fail -H \"User-Agent: LynxLauncher\" \"") + kRegistryUrl + "\"", &code);
		if (code != 0)
		{
			error = "Impossible de lire le catalogue des plugins (curl code " + std::to_string(code) + ").";
			log(error + " " + text);
			return false;
		}
		if (!ParseRegistry(text, out, error))
		{
			log(error);
			return false;
		}
		log(std::to_string(out.size()) + " plugin(s) dans le catalogue.");

		// Les releases de chaque dépôt. L'API GitHub sans compte est limitée à 60 requêtes / heure.
		for (CatalogPlugin& p : out)
		{
			const std::string url = "https://api.github.com/repos/" + p.repo + "/releases?per_page=100";
			int c = 0;
			std::string json = Process::Capture(
				"curl -sSL -H \"Accept: application/vnd.github+json\" -H \"User-Agent: LynxLauncher\" \"" + url + "\"", &c);
			std::string e;
			if (c != 0)
				p.error = "dépôt injoignable (curl code " + std::to_string(c) + ")";
			else if (!ParseReleases(json, p.releases, e))
				p.error = e;
			if (!p.error.empty())
				log("  " + p.name + " (" + p.repo + ") : " + p.error);
			else
				log("  " + p.name + " : " + std::to_string(p.releases.size()) + " release(s)");
		}
		return true;
	}

	fs::path FindPluginRoot(const fs::path& dir)
	{
		std::error_code error;
		if (fs::is_regular_file(dir / "plugin.json", error))
			return dir;
		// un niveau, puis deux (archive « Plugin-1.2.4/Plugin/plugin.json »)
		std::vector<fs::path> level;
		for (const auto& entry : fs::directory_iterator(dir, error))
		{
			std::error_code e;
			if (entry.is_directory(e))
				level.push_back(entry.path());
		}
		for (const fs::path& sub : level)
			if (fs::is_regular_file(sub / "plugin.json", error))
				return sub;
		for (const fs::path& sub : level)
			for (const auto& entry : fs::directory_iterator(sub, error))
			{
				std::error_code e;
				if (entry.is_directory(e) && fs::is_regular_file(entry.path() / "plugin.json", e))
					return entry.path();
			}
		return {};
	}

	bool Install(const CatalogPlugin& plugin, const PluginRelease& release, const std::string& engine_tag,
	             const LogFn& log, const std::function<void(float)>& on_progress)
	{
		if (release.asset_url.empty())
		{
			log("Échec : la release " + release.tag + " de " + plugin.name + " n'a pas d'archive .zip.");
			return false;
		}

		std::error_code error;
		const fs::path work = WorkDirectory();
		const fs::path archive = work / (plugin.name + ".zip");
		const fs::path extract = work / plugin.name;
		fs::create_directories(work, error);

		log("== Installation de " + plugin.name + " " + release.version + " dans Lynx " + engine_tag + " ==");
		on_progress(0.0f);
		bool ok = Download(release.asset_url, archive, log, on_progress);
		if (ok)
		{
			on_progress(0.9f);
			ok = Extract(archive, extract, log);
		}
		fs::path root;
		if (ok)
		{
			root = FindPluginRoot(extract);
			if (root.empty())
			{
				log("Échec : pas de plugin.json dans l'archive.");
				ok = false;
			}
		}
		if (ok)
		{
			Json info = {
				{ "source", "github" },
				{ "repo", plugin.repo },
				{ "tag", release.tag },
				{ "version", release.version },
				{ "engine_min", release.engine_min },
			};
			WriteJson(root / kInstallFile, info);
			ok = PlaceFolder(root, PluginName(root, plugin.name), engine_tag, true, log);
		}

		fs::remove(archive, error);
		fs::remove_all(extract, error);
		if (ok)
		{
			on_progress(1.0f);
			log(plugin.name + " " + release.version + " installé. Active-le dans l'éditeur : Options > Plugins.");
		}
		return ok;
	}

	std::string Import(const fs::path& source, const std::string& engine_tag, const LogFn& log)
	{
		std::error_code error;
		log("== Import de " + source.string() + " dans Lynx " + engine_tag + " ==");

		fs::path root;
		fs::path extract;
		bool move = false;
		if (fs::is_regular_file(source, error) && Lower(source.extension().string()) == ".zip")
		{
			extract = WorkDirectory() / ("import-" + source.stem().string());
			fs::create_directories(WorkDirectory(), error);
			if (!Extract(source, extract, log))
				return {};
			root = FindPluginRoot(extract);
			move = true;
		}
		else if (fs::is_directory(source, error))
			root = FindPluginRoot(source);
		else
		{
			log("Échec : " + source.string() + " n'est ni un dossier ni une archive .zip.");
			return {};
		}

		if (root.empty())
		{
			log("Échec : aucun plugin.json dans " + source.string() + ".");
			if (!extract.empty())
				fs::remove_all(extract, error);
			return {};
		}

		const std::string name = PluginName(root, root.filename().string());
		const Json desc = ReadJson(root / "plugin.json");
		Json info = {
			{ "source", "local" },
			{ "path", source.string() },
			{ "version", Str(desc, "version") },
			{ "engine_min", Str(desc, "engine_min") },
		};

		bool ok;
		if (move)
		{
			WriteJson(root / kInstallFile, info);
			ok = PlaceFolder(root, name, engine_tag, true, log);
		}
		else
		{
			// on ne modifie pas le dossier de l'utilisateur : copie, puis fichier d'installation
			ok = PlaceFolder(root, name, engine_tag, false, log);
			if (ok)
				WriteJson(PluginsDirectory(engine_tag) / name / kInstallFile, info);
		}
		if (!extract.empty())
			fs::remove_all(extract, error);

		if (!ok)
			return {};
		log(name + " importé. Active-le dans l'éditeur : Options > Plugins.");
		return name;
	}

	bool Remove(const InstalledPlugin& plugin, const LogFn& log)
	{
		if (plugin.builtin)
		{
			log("Échec : " + plugin.name + " est intégré au moteur.");
			return false;
		}
		std::error_code error;
		fs::remove_all(plugin.folder, error);
		if (error)
		{
			log("Échec : impossible de supprimer " + plugin.folder.string() + " : " + error.message() +
			    " (l'éditeur est peut-être ouvert).");
			return false;
		}
		log(plugin.name + " retiré.");
		return true;
	}

#ifdef _WIN32
	fs::path PickFolder()
	{
		fs::path result;
		const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		IFileOpenDialog* dialog = nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
		                               IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog))))
		{
			DWORD options = 0;
			dialog->GetOptions(&options);
			dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
			dialog->SetTitle(L"Dossier du plugin (contenant plugin.json)");
			if (SUCCEEDED(dialog->Show(GetActiveWindow())))
			{
				IShellItem* item = nullptr;
				if (SUCCEEDED(dialog->GetResult(&item)))
				{
					PWSTR path = nullptr;
					if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
					{
						result = fs::path(path);
						CoTaskMemFree(path);
					}
					item->Release();
				}
			}
			dialog->Release();
		}
		if (SUCCEEDED(init))
			CoUninitialize();
		return result;
	}
#endif
}
