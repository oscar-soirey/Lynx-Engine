#pragma once
#include "Dependencies.h"   // LogFn

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Une version du moteur = une release GitHub de oscar-soirey/Lynx-Engine.
struct EngineRelease
{
	std::string tag;          // v26.0.1
	std::string version;      // 26.0.1
	std::string name;         // titre de la release
	std::string date;         // AAAA-MM-JJ
	std::string body;         // notes de version (markdown brut)
	std::string page_url;     // page de la release sur GitHub
	bool prerelease = false;

	std::string asset_name;   // lynx-w-x64-binaries.zip
	std::string asset_url;
	std::uint64_t asset_size = 0;

	// état
	bool installed = false;
	float progress = -1.0f;   // 0..1 pendant le téléchargement
};

namespace Engine
{
	inline constexpr const char* kRepository = "oscar-soirey/Lynx-Engine";

	// Dossier d'installation : %LOCALAPPDATA%\Lynx\versions\<tag>
	std::filesystem::path VersionsDirectory();
	std::filesystem::path VersionDirectory(const std::string& tag);
	bool IsInstalled(const std::string& tag);

	// Lit les releases via l'API GitHub (curl). Bloquant.
	bool FetchReleases(std::vector<EngineRelease>& out, std::string& error, const LogFn& log);

	// Lit le JSON de l'API GitHub (séparé de FetchReleases pour pouvoir le tester).
	bool ParseReleases(const std::string& json_text, std::vector<EngineRelease>& out, std::string& error, const LogFn& log);

	// Télécharge l'archive Windows de la release et l'extrait. Bloquant.
	bool Install(const EngineRelease& release, const LogFn& log, const std::function<void(float)>& on_progress);
	bool Uninstall(const std::string& tag, const LogFn& log);

	// SDK de la version (headers, libLynx.dll.a, LynxConfig.cmake) : <version>\sdk\cmake.
	// Les versions publiées avant le SDK n'en ont pas (find_package(Lynx) impossible).
	std::filesystem::path SdkConfigDirectory(const std::string& tag);

	// Registre de paquets CMake de l'utilisateur
	// (HKEY_CURRENT_USER\Software\Kitware\CMake\Packages\Lynx) : find_package(Lynx)
	// d'un projet de jeu trouve les versions installées sans -DLynx_DIR.
	void RegisterCMakePackage(const std::string& tag, const LogFn& log);
	void UnregisterCMakePackage(const std::string& tag);
	// Enregistre toutes les versions installées qui ont un SDK (au démarrage du launcher).
	void RegisterInstalledVersions(const LogFn& log);

	// Lance LynxEditor.exe de cette version (dossier de travail = dossier de la version).
	bool Launch(const std::string& tag, const LogFn& log);

	std::string FormatSize(std::uint64_t bytes);
}
