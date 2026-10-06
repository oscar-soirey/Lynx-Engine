#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Mise à jour du launcher : une release GitHub de oscar-soirey/Lynx-Engine dont
// le tag commence par « launcher » (ex : launcher-v-0.0.2).
// Le moteur utilise les tags « v26.0.1 » : les deux ne se mélangent pas.
namespace Updater
{
	inline constexpr const char* kRepository       = "oscar-soirey/Lynx-Engine";
	inline constexpr const char* kTagPrefix        = "launcher";
	inline constexpr const char* kLauncherExe      = "LynxLauncher.exe";
	inline constexpr const char* kVersionFile      = "launcher.version";   // contient le tag installé

	struct Release
	{
		std::string tag;                 // launcher-v-0.0.2
		std::vector<int> version;        // {0, 0, 2}
		std::string asset_name;          // .zip (ou .exe) de la release
		std::string asset_url;
		std::uint64_t asset_size = 0;
	};

	using StatusFn   = std::function<void(const std::string&)>;
	using ProgressFn = std::function<void(float)>;   // 0..1

	// « launcher-v-0.0.2 » -> {0, 0, 2} (les nombres qui suivent le préfixe).
	std::vector<int> ParseVersion(const std::string& tag);
	// <0 si a < b, 0 si égales, >0 si a > b.
	int Compare(const std::vector<int>& a, const std::vector<int>& b);

	// Tag écrit dans launcher.version à côté de l'exécutable (vide si absent).
	std::string ReadInstalledTag(const std::filesystem::path& directory);

	// Plus récente release « launcher* » (ni brouillon, ni pré-release). Bloquant.
	bool FetchLatest(Release& out, std::string& error);

	// Télécharge, extrait et copie les fichiers dans `directory`. Bloquant.
	// `self_name` : nom de l'updater (jamais écrasé pendant qu'il tourne).
	bool Install(const Release& release, const std::filesystem::path& directory, const std::string& self_name,
	             const StatusFn& status, const ProgressFn& progress, std::string& error);

	std::string FormatSize(std::uint64_t bytes);
}
