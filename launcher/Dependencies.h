#pragma once
#include <functional>
#include <string>
#include <vector>

using LogFn = std::function<void(const std::string&)>;

enum class DepState { Unknown, Checking, Missing, Installed, Installing, Failed };

struct Dependency
{
	std::string name;          // affiché
	std::string description;   // à quoi ça sert dans Lynx
	std::string winget_id;     // Windows : identifiant winget
	std::string linux_install; // Linux : commande d'installation (dev)
	std::string url;           // page officielle (installation manuelle)
	std::vector<std::string> check_commands; // la première qui réussit gagne
	std::string required_prefix; // ex "3.14" : version exigée (vide = toutes)
	bool required = true;        // false : optionnel (pas dans « Tout installer », ne bloque pas le moteur)

	// Outil téléchargé au lieu de winget : archive .zip extraite dans tool_dir,
	// détecté par la présence de tool_file (ex : Tracy, %LOCALAPPDATA%\Lynx\tools\tracy).
	std::string download_url;
	std::string tool_dir;
	std::string tool_file;
	std::string tool_version;    // affichée quand l'outil est présent

	// état (mis à jour par le thread de travail, lu par l'UI)
	DepState state = DepState::Unknown;
	std::string version;
};

namespace Dependencies
{
	std::vector<Dependency> CreateList();

	// Détecte la dépendance et remplit state/version. Bloquant.
	void Check(Dependency& dep);

	// Installe la dépendance (winget sous Windows). Bloquant. Renvoie true si
	// la dépendance est détectée après l'installation.
	bool Install(Dependency& dep, const LogFn& log);

	// Le gestionnaire de paquets est-il disponible (winget) ?
	bool PackageManagerAvailable(std::string* details = nullptr);

	const char* StateLabel(DepState state);
}
