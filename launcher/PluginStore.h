#pragma once
#include "Dependencies.h"   // LogFn

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Plugins du moteur, installés dans une version : %LOCALAPPDATA%\Lynx\versions\<tag>\plugins\<Nom>
// (c'est le dossier <éditeur>/plugins lu par l'éditeur, voir src/core/Plugins.h).
//
// Catalogue en ligne : registry/plugins.json du dépôt Lynx-Engine, un plugin par entrée :
//   [ { "name": "Weather", "repo": "owner/Lynx-Weather", "description": "...",
//       "category": "Gameplay", "author": "..." } ]
// Chaque plugin a son dépôt ; ses releases sont taguées v<plugin>-<moteur minimal> :
//   v1.2.4-26.0.6 = plugin 1.2.4, fait pour Lynx 26.0.6 (et utilisable sur toute version plus récente).
// Le premier .zip de la release est téléchargé (plugin.json à la racine ou dans un sous-dossier).
//
// Un plugin installé par le launcher a un fichier .lynx-install.json à côté de son plugin.json
// (dépôt, tag, version du moteur visée) : l'éditeur le lit pour prévenir à l'activation.
// Sans ce fichier, c'est un plugin livré avec le moteur (« intégré »).

// Une release d'un plugin (tag v<plugin>-<moteur>).
struct PluginRelease
{
	std::string tag;
	std::string version;        // 1.2.4
	std::string engine_min;     // 26.0.6
	std::string date;           // AAAA-MM-JJ
	std::string body;
	std::string page_url;
	std::string asset_url;
	std::uint64_t asset_size = 0;
	bool prerelease = false;
};

// Une entrée du catalogue (registry/plugins.json) + ses releases.
struct CatalogPlugin
{
	std::string name;
	std::string repo;           // owner/name
	std::string description;
	std::string category;
	std::string author;

	std::vector<PluginRelease> releases;   // plus récentes d'abord
	std::string error;          // releases illisibles
	float progress = -1.0f;     // 0..1 pendant l'installation
};

// Un plugin présent dans une version installée du moteur.
struct InstalledPlugin
{
	std::string name;
	std::string version;
	std::string description;
	std::string category;
	std::string author;
	std::filesystem::path folder;

	bool builtin = false;       // livré avec le moteur (pas de .lynx-install.json)
	std::string source;         // "github" / "local"
	std::string repo;
	std::string tag;
	std::string engine_min;     // version du moteur visée ("" : inconnue)
};

// Une version du moteur installée (cible des plugins).
struct InstalledEngine
{
	std::string tag;            // v26.0.6
	std::string version;        // 26.0.6
};

namespace PluginStore
{
	inline constexpr const char* kRegistryUrl =
		"https://raw.githubusercontent.com/oscar-soirey/Lynx-Engine/main/registry/plugins.json";
	inline constexpr const char* kInstallFile = ".lynx-install.json";

	// Compare deux versions « 26.0.6 » (nombres séparés par des points) : <0, 0, >0.
	int CompareVersions(const std::string& a, const std::string& b);

	// « v1.2.4-26.0.6 » -> 1.2.4 / 26.0.6. false si le tag n'a pas cette forme.
	bool ParseTag(const std::string& tag, std::string& version, std::string& engine_min);

	// Meilleure release pour cette version du moteur : la plus haute version du plugin dont
	// le moteur minimal est <= engine (les pré-releases seulement si allow_prerelease).
	// nullptr : aucune compatible.
	const PluginRelease* BestRelease(const CatalogPlugin& plugin, const std::string& engine, bool allow_prerelease);

	// Versions du moteur installées (dossiers de %LOCALAPPDATA%\Lynx\versions avec LynxEditor.exe),
	// les plus récentes d'abord.
	std::vector<InstalledEngine> ListEngines();

	std::filesystem::path PluginsDirectory(const std::string& engine_tag);

	// Plugins d'une version installée.
	std::vector<InstalledPlugin> ListInstalled(const std::string& engine_tag);

	// Lit registry/plugins.json puis les releases de chaque dépôt (curl). Bloquant.
	bool FetchCatalog(std::vector<CatalogPlugin>& out, std::string& error, const LogFn& log);
	bool ParseRegistry(const std::string& json_text, std::vector<CatalogPlugin>& out, std::string& error);
	bool ParseReleases(const std::string& json_text, std::vector<PluginRelease>& out, std::string& error);

	// Télécharge la release et l'installe dans la version (remplace l'ancienne). Bloquant.
	bool Install(const CatalogPlugin& plugin, const PluginRelease& release, const std::string& engine_tag,
	             const LogFn& log, const std::function<void(float)>& on_progress);

	// Copie un plugin depuis l'ordinateur (dossier contenant plugin.json, ou archive .zip). Bloquant.
	// Renvoie le nom du plugin installé ("" : échec).
	std::string Import(const std::filesystem::path& source, const std::string& engine_tag, const LogFn& log);

	// Dossier qui contient plugin.json : `dir` lui-même ou un sous-dossier (2 niveaux). "" : aucun.
	std::filesystem::path FindPluginRoot(const std::filesystem::path& dir);

	bool Remove(const InstalledPlugin& plugin, const LogFn& log);

#ifdef _WIN32
	// Fenêtre « choisir un dossier » de Windows. "" si annulé.
	std::filesystem::path PickFolder();
#endif
}
