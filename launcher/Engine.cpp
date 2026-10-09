#include "Engine.h"
#include "Process.h"

#include <json/json.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <regex>

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#endif

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace
{
	const char* kEditorExecutable = "LynxEditor.exe";   // les releases sont Windows uniquement

	std::string Quote(const fs::path& path)
	{
		return "\"" + path.string() + "\"";
	}

	// Lecture tolérante : GitHub renvoie parfois null (body, name...).
	std::string Str(const Json& object, const char* key)
	{
		auto it = object.find(key);
		return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
	}

	// Choisit l'archive Windows de la release (les « Source code » ne sont pas des assets).
	// `assets` doit être une référence vers le JSON de la release (pas une copie temporaire).
	const Json* PickAsset(const Json& assets)
	{
		if (!assets.is_array())
			return nullptr;
		const Json* fallback = nullptr;
		for (const Json& asset : assets)
		{
			if (!asset.is_object())
				continue;
			std::string name = Str(asset, "name");
			if (name.size() < 4 || name.substr(name.size() - 4) != ".zip")
				continue;
			if (name.find("-w-") != std::string::npos || name.find("win") != std::string::npos)
				return &asset;
			if (!fallback)
				fallback = &asset;
		}
		return fallback;
	}
}

namespace Engine
{
	fs::path VersionsDirectory()
	{
#ifdef _WIN32
		const char* base = std::getenv("LOCALAPPDATA");
		fs::path root = base ? fs::path(base) : fs::temp_directory_path();
		return root / "Lynx" / "versions";
#else
		const char* home = std::getenv("HOME");
		return fs::path(home ? home : ".") / ".local" / "share" / "Lynx" / "versions";
#endif
	}

	fs::path VersionDirectory(const std::string& tag)
	{
		return VersionsDirectory() / tag;
	}

	bool IsInstalled(const std::string& tag)
	{
		std::error_code error;
		return fs::is_regular_file(VersionDirectory(tag) / kEditorExecutable, error);
	}

	bool FetchReleases(std::vector<EngineRelease>& out, std::string& error, const LogFn& log)
	{
		std::string url = std::string("https://api.github.com/repos/") + kRepository + "/releases?per_page=100";
		log("> curl " + url);
		int code = 0;
		std::string json_text = Process::Capture(
			"curl -sSL --fail -H \"Accept: application/vnd.github+json\" -H \"User-Agent: LynxLauncher\" \"" + url + "\"",
			&code);
		if (code != 0)
		{
			error = "Impossible de contacter GitHub (curl code " + std::to_string(code) + ").";
			log(error + " " + json_text);
			return false;
		}

		return ParseReleases(json_text, out, error, log);
	}

	bool ParseReleases(const std::string& json_text, std::vector<EngineRelease>& out, std::string& error, const LogFn& log)
	{
		try
		{
			const Json root = Json::parse(json_text);
			if (!root.is_array())
			{
				error = "Réponse GitHub inattendue : " +
				        (root.is_object() ? Str(root, "message") : std::string(root.type_name()));
				log(error);
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

				// Le dépôt contient aussi les releases du launcher (« launcher-v-0.0.2 ») :
				// on ne garde que les versions du moteur, « v » suivi d'un chiffre (v26.0.1).
				const std::string tag = Str(item, "tag_name");
				if (tag.size() < 2 || tag[0] != 'v' || !std::isdigit(static_cast<unsigned char>(tag[1])))
					continue;

				EngineRelease release;
				release.tag = tag;
				release.version = release.tag;
				release.version.erase(0, 1);
				release.name = Str(item, "name");
				release.body = Str(item, "body");
				release.page_url = Str(item, "html_url");
				auto prerelease = item.find("prerelease");
				release.prerelease = prerelease != item.end() && prerelease->is_boolean() && prerelease->get<bool>();
				release.date = Str(item, "published_at").substr(0, 10);

				auto assets = item.find("assets");
				if (assets != item.end())
					if (const Json* asset = PickAsset(*assets))
					{
						release.asset_name = Str(*asset, "name");
						release.asset_url = Str(*asset, "browser_download_url");
						auto size = asset->find("size");
						if (size != asset->end() && size->is_number_unsigned())
							release.asset_size = size->get<std::uint64_t>();
					}
				release.installed = IsInstalled(release.tag);
				out.push_back(std::move(release));
			}
			log(std::to_string(out.size()) + " version(s) trouvée(s) sur GitHub.");
			return true;
		}
		catch (const std::exception& e)
		{
			error = std::string("Réponse GitHub illisible : ") + e.what();
			log(error);
			return false;
		}
	}

	bool Install(const EngineRelease& release, const LogFn& log, const std::function<void(float)>& on_progress)
	{
		if (release.asset_url.empty())
		{
			log("Cette version n'a pas d'archive Windows à télécharger.");
			return false;
		}

		fs::path target = VersionDirectory(release.tag);
		fs::path archive = VersionsDirectory() / (release.tag + ".zip");
		std::error_code error;
		fs::create_directories(VersionsDirectory(), error);
		fs::remove_all(target, error);
		fs::create_directories(target, error);
		if (error)
		{
			log("Impossible de créer " + target.string() + " : " + error.message());
			return false;
		}

		log("== Téléchargement de Lynx " + release.version + " (" + FormatSize(release.asset_size) + ") ==");
		static const std::regex percent(R"((\d{1,3}(?:\.\d)?)%)");
		int code = Process::Run(
			"curl -L --fail -# -o " + Quote(archive) + " \"" + release.asset_url + "\"",
			[&](const std::string& line) {
				std::smatch match;
				if (std::regex_search(line, match, percent))
					on_progress(std::stof(match.str(1)) / 100.0f * 0.9f);   // 90 % : téléchargement
				else if (line.find_first_not_of(" #") != std::string::npos)
					log("  " + line);
			});
		if (code != 0)
		{
			log("Échec du téléchargement (curl code " + std::to_string(code) + ").");
			fs::remove(archive, error);
			fs::remove_all(target, error);
			return false;
		}

		log("Extraction dans " + target.string());
		on_progress(0.95f);
		// tar.exe (bsdtar) est fourni avec Windows 10/11 et lit les .zip
		code = Process::Run("tar -xf " + Quote(archive) + " -C " + Quote(target),
		                    [&](const std::string& line) { log("  " + line); });
		fs::remove(archive, error);
		if (code != 0 || !IsInstalled(release.tag))
		{
			log("Échec de l'extraction : LynxEditor.exe introuvable dans l'archive.");
			return false;
		}
		on_progress(1.0f);
		RegisterCMakePackage(release.tag, log);
		log("Lynx " + release.version + " installé.");
		return true;
	}

	bool Uninstall(const std::string& tag, const LogFn& log)
	{
		UnregisterCMakePackage(tag);
		std::error_code error;
		fs::remove_all(VersionDirectory(tag), error);
		if (error)
		{
			log("Impossible de supprimer " + VersionDirectory(tag).string() + " : " + error.message()
			    + " (l'éditeur est peut-être encore ouvert).");
			return false;
		}
		log("Version " + tag + " désinstallée.");
		return true;
	}

	fs::path SdkConfigDirectory(const std::string& tag)
	{
		return VersionDirectory(tag) / "sdk" / "cmake";
	}

	namespace
	{
		constexpr const wchar_t* kCMakeRegistryKey = L"Software\\Kitware\\CMake\\Packages\\Lynx";

		// Nom de la valeur dans le registre CMake (libre, une par version)
		std::wstring RegistryValueName(const std::string& tag)
		{
			return L"LynxLauncher-" + fs::path(tag).wstring();
		}
	}

	void RegisterCMakePackage(const std::string& tag, const LogFn& log)
	{
		std::error_code error;
		const fs::path config = SdkConfigDirectory(tag);
		if (!fs::is_regular_file(config / "LynxConfig.cmake", error))
		{
			log("  (Lynx " + tag + " n'a pas de SDK : les projets C++ ne pourront pas utiliser cette version.)");
			return;
		}
#ifdef _WIN32
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, kCMakeRegistryKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
		{
			log("Impossible d'enregistrer Lynx " + tag + " pour CMake (registre).");
			return;
		}
		const std::wstring value = config.wstring();
		RegSetValueExW(key, RegistryValueName(tag).c_str(), 0, REG_SZ,
		               reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
		RegCloseKey(key);
#endif
	}

	void UnregisterCMakePackage(const std::string& tag)
	{
#ifdef _WIN32
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kCMakeRegistryKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS)
		{
			RegDeleteValueW(key, RegistryValueName(tag).c_str());
			RegCloseKey(key);
		}
#else
		(void)tag;
#endif
	}

	void RegisterInstalledVersions(const LogFn& log)
	{
		std::error_code error;
		for (fs::directory_iterator it(VersionsDirectory(), error), end; !error && it != end; it.increment(error))
		{
			const std::string tag = it->path().filename().string();
			if (it->is_directory(error) && fs::is_regular_file(SdkConfigDirectory(tag) / "LynxConfig.cmake", error))
				RegisterCMakePackage(tag, log);
		}
	}

	bool Launch(const std::string& tag, const LogFn& log)
	{
		fs::path dir = VersionDirectory(tag);
		fs::path exe = dir / kEditorExecutable;
		log("> " + exe.string());
		if (!Process::Launch(exe, dir))
		{
			log("Impossible de lancer " + exe.string());
			return false;
		}
		return true;
	}

	std::string FormatSize(std::uint64_t bytes)
	{
		char buffer[32];
		if (bytes >= 1024ull * 1024 * 1024)
			std::snprintf(buffer, sizeof(buffer), "%.1f Go", bytes / (1024.0 * 1024 * 1024));
		else if (bytes >= 1024ull * 1024)
			std::snprintf(buffer, sizeof(buffer), "%.1f Mo", bytes / (1024.0 * 1024));
		else
			std::snprintf(buffer, sizeof(buffer), "%.0f Ko", bytes / 1024.0);
		return buffer;
	}
}
