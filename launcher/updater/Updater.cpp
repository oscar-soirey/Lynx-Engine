#include "Updater.h"
#include "../Process.h"

#include <json/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <regex>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace
{
	std::string Quote(const fs::path& path) { return "\"" + path.string() + "\""; }

	std::string Lower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		return text;
	}

	bool EndsWith(const std::string& text, const std::string& suffix)
	{
		return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
	}

	// Lecture tolérante : GitHub renvoie parfois null.
	std::string Str(const Json& object, const char* key)
	{
		auto it = object.find(key);
		return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
	}

	bool Flag(const Json& object, const char* key)
	{
		auto it = object.find(key);
		return it != object.end() && it->is_boolean() && it->get<bool>();
	}

	// Archive de la release : un .zip (ou directement l'.exe), « launcher » dans le nom de préférence.
	const Json* PickAsset(const Json& assets)
	{
		if (!assets.is_array())
			return nullptr;
		const Json* best = nullptr;
		int best_score = -1;
		for (const Json& asset : assets)
		{
			if (!asset.is_object())
				continue;
			const std::string name = Lower(Str(asset, "name"));
			if (!EndsWith(name, ".zip") && !EndsWith(name, ".exe"))
				continue;
			int score = 0;
			if (name.find("launcher") != std::string::npos) score += 2;
			if (name.find("-w-") != std::string::npos || name.find("win") != std::string::npos) score += 1;
			if (score > best_score)
			{
				best = &asset;
				best_score = score;
			}
		}
		return best;
	}

	// Cherche LynxLauncher.exe dans le dossier extrait (l'archive peut contenir un sous-dossier).
	fs::path FindLauncherRoot(const fs::path& extracted)
	{
		std::error_code error;
		for (fs::recursive_directory_iterator it(extracted, error), end; !error && it != end; it.increment(error))
			if (it->is_regular_file(error) && Lower(it->path().filename().string()) == Lower(Updater::kLauncherExe))
				return it->path().parent_path();
		return {};
	}

	// Remplace `target` par `source` : l'ancien fichier est renommé en .old (possible même
	// s'il est en cours d'exécution), puis supprimé plus tard. Annule si la copie échoue.
	bool ReplaceFile(const fs::path& source, const fs::path& target, std::string& error_text)
	{
		std::error_code error;
		fs::create_directories(target.parent_path(), error);

		fs::path backup = target;
		backup += ".old";
		bool had_old = fs::exists(target, error);
		if (had_old)
		{
			fs::remove(backup, error);
			fs::rename(target, backup, error);
			if (error)
			{
				error_text = "Impossible de remplacer " + target.filename().string() + " : " + error.message();
				return false;
			}
		}
		fs::copy_file(source, target, fs::copy_options::overwrite_existing, error);
		if (error)
		{
			error_text = "Impossible de copier " + target.filename().string() + " : " + error.message();
			std::error_code ignored;
			if (had_old)
				fs::rename(backup, target, ignored);   // on remet l'ancien fichier
			return false;
		}
		return true;
	}
}

namespace Updater
{
	std::vector<int> ParseVersion(const std::string& tag)
	{
		std::vector<int> result;
		size_t start = Lower(tag).rfind(kTagPrefix, 0) == 0 ? std::string(kTagPrefix).size() : 0;
		static const std::regex number(R"(\d+)");
		const std::string text = tag.substr(start);
		for (auto it = std::sregex_iterator(text.begin(), text.end(), number); it != std::sregex_iterator(); ++it)
			result.push_back(std::stoi(it->str()));
		return result;
	}

	int Compare(const std::vector<int>& a, const std::vector<int>& b)
	{
		const size_t count = std::max(a.size(), b.size());
		for (size_t i = 0; i < count; ++i)
		{
			int x = i < a.size() ? a[i] : 0;
			int y = i < b.size() ? b[i] : 0;
			if (x != y)
				return x < y ? -1 : 1;
		}
		return 0;
	}

	std::string ReadInstalledTag(const fs::path& directory)
	{
		std::ifstream file(directory / kVersionFile);
		std::string tag;
		std::getline(file, tag);
		while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.back())))
			tag.pop_back();
		return tag;
	}

	bool FetchLatest(Release& out, std::string& error)
	{
		const std::string url = std::string("https://api.github.com/repos/") + kRepository + "/releases?per_page=100";
		int code = 0;
		const std::string json_text = Process::Capture(
			"curl -sSL --fail --connect-timeout 5 --max-time 15 -H \"Accept: application/vnd.github+json\" "
			"-H \"User-Agent: LynxUpdater\" \"" + url + "\"", &code);
		if (code != 0)
		{
			error = "Impossible de contacter GitHub (curl code " + std::to_string(code) + ").";
			return false;
		}

		try
		{
			const Json root = Json::parse(json_text);
			if (!root.is_array())
			{
				error = "Réponse GitHub inattendue : " + (root.is_object() ? Str(root, "message") : std::string(root.type_name()));
				return false;
			}

			bool found = false;
			for (const Json& item : root)
			{
				if (!item.is_object() || Flag(item, "draft") || Flag(item, "prerelease"))
					continue;
				const std::string tag = Str(item, "tag_name");
				if (Lower(tag).rfind(kTagPrefix, 0) != 0)   // doit commencer par « launcher »
					continue;

				Release release;
				release.tag = tag;
				release.version = ParseVersion(tag);
				if (release.version.empty())
					continue;
				auto assets = item.find("assets");
				if (assets == item.end())
					continue;
				const Json* asset = PickAsset(*assets);
				if (!asset)
					continue;
				release.asset_name = Str(*asset, "name");
				release.asset_url = Str(*asset, "browser_download_url");
				auto size = asset->find("size");
				if (size != asset->end() && size->is_number_unsigned())
					release.asset_size = size->get<std::uint64_t>();
				if (release.asset_url.empty())
					continue;

				if (!found || Compare(release.version, out.version) > 0)
				{
					out = std::move(release);
					found = true;
				}
			}
			if (!found)
				error = "Aucune release du launcher (tag « launcher... ») trouvée sur GitHub.";
			return found;
		}
		catch (const std::exception& e)
		{
			error = std::string("Réponse GitHub illisible : ") + e.what();
			return false;
		}
	}

	bool Install(const Release& release, const fs::path& directory, const std::string& self_name,
	             const StatusFn& status, const ProgressFn& progress, std::string& error)
	{
		const fs::path work = directory / "launcher_update";
		const fs::path extracted = work / "extracted";
		const fs::path archive = work / release.asset_name;
		std::error_code ec;
		fs::remove_all(work, ec);
		fs::create_directories(extracted, ec);
		if (ec)
		{
			error = "Impossible de créer " + work.string() + " : " + ec.message();
			return false;
		}
		auto cleanup = [&] { std::error_code ignored; fs::remove_all(work, ignored); };

		// 1. Téléchargement (0 -> 85 %)
		status("Téléchargement du launcher " + release.tag + " (" + FormatSize(release.asset_size) + ")...");
		static const std::regex percent(R"((\d{1,3}(?:\.\d)?)%)");
		int code = Process::Run(
			"curl -L --fail -# -o " + Quote(archive) + " \"" + release.asset_url + "\"",
			[&](const std::string& line) {
				std::smatch match;
				if (std::regex_search(line, match, percent))
					progress(std::stof(match.str(1)) / 100.0f * 0.85f);
			});
		if (code != 0)
		{
			error = "Échec du téléchargement (curl code " + std::to_string(code) + ").";
			cleanup();
			return false;
		}

		// 2. Extraction (zip) ou fichier unique (exe)
		status("Extraction...");
		progress(0.88f);
		if (EndsWith(Lower(release.asset_name), ".zip"))
		{
			// tar.exe (bsdtar) est fourni avec Windows 10/11 et lit les .zip
			code = Process::Run("tar -xf " + Quote(archive) + " -C " + Quote(extracted));
			if (code != 0)
			{
				error = "Échec de l'extraction (tar code " + std::to_string(code) + ").";
				cleanup();
				return false;
			}
		}
		else
		{
			fs::copy_file(archive, extracted / kLauncherExe, fs::copy_options::overwrite_existing, ec);
			if (ec)
			{
				error = "Impossible de préparer " + std::string(kLauncherExe) + " : " + ec.message();
				cleanup();
				return false;
			}
		}

		const fs::path root = FindLauncherRoot(extracted);
		if (root.empty())
		{
			error = std::string(kLauncherExe) + " introuvable dans l'archive de la release.";
			cleanup();
			return false;
		}

		// 3. Copie à côté de l'updater (l'updater lui-même n'est jamais touché)
		status("Installation...");
		progress(0.92f);
		std::vector<fs::path> installed;   // pour retirer les .old à la fin
		for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_regular_file(ec))
				continue;
			const fs::path relative = fs::relative(it->path(), root, ec);
			if (Lower(relative.filename().string()) == Lower(self_name))
				continue;
			const fs::path target = directory / relative;
			if (!ReplaceFile(it->path(), target, error))
			{
				// on remet les fichiers déjà remplacés
				for (const fs::path& done : installed)
				{
					fs::path backup = done;
					backup += ".old";
					std::error_code ignored;
					if (fs::exists(backup, ignored))
					{
						fs::remove(done, ignored);
						fs::rename(backup, done, ignored);
					}
				}
				cleanup();
				return false;
			}
			installed.push_back(target);
		}

		for (const fs::path& done : installed)
		{
			fs::path backup = done;
			backup += ".old";
			std::error_code ignored;
			fs::remove(backup, ignored);   // sans importance si ça échoue : sera écrasé à la prochaine mise à jour
		}

		// 4. Mémorise la version installée
		{
			std::ofstream file(directory / kVersionFile, std::ios::trunc);
			file << release.tag << "\n";
		}
		cleanup();
		progress(1.0f);
		return true;
	}

	std::string FormatSize(std::uint64_t bytes)
	{
		char buffer[32];
		if (bytes >= 1024ull * 1024)
			std::snprintf(buffer, sizeof(buffer), "%.1f Mo", bytes / (1024.0 * 1024));
		else
			std::snprintf(buffer, sizeof(buffer), "%.0f Ko", bytes / 1024.0);
		return buffer;
	}
}
