#include "Dependencies.h"
#include "Process.h"

#include <cstdlib>
#include <filesystem>
#include <regex>

namespace
{
	std::string ExtractVersion(const std::string& output)
	{
		static const std::regex version(R"((\d+)\.(\d+)(\.\d+)?)");
		std::smatch match;
		if (std::regex_search(output, match, version))
			return match.str(0);
		return {};
	}
}

namespace Dependencies
{
	std::vector<Dependency> CreateList()
	{
		std::vector<Dependency> list;

		{
			Dependency d;
			d.name = "Python 3.14";
			d.description = "Scripts de l'éditeur (commands/*.py), module lynx_editor et serveur MCP lynx_mcp.py.";
			d.winget_id = "Python.Python.3.14";
			d.linux_install = "sudo -n apt-get install -y python3.14";
			d.url = "https://www.python.org/downloads/";
#ifdef _WIN32
			d.check_commands = { "py -3.14 --version", "python --version" };
#else
			d.check_commands = { "python3.14 --version", "python3 --version" };
#endif
			d.required_prefix = "3.14";
			list.push_back(d);
		}
		{
			Dependency d;
			d.name = "CMake";
			d.description = "Génération des projets du moteur et des jeux (find_package(Lynx)).";
			d.winget_id = "Kitware.CMake";
			d.linux_install = "sudo -n apt-get install -y cmake";
			d.url = "https://cmake.org/download/";
			d.check_commands = { "cmake --version" };
			list.push_back(d);
		}
		{
			Dependency d;
			d.name = "MinGW-w64 (GCC)";
			d.description = "Compilateur du moteur et des jeux : un jeu doit être compilé avec le même MinGW que le moteur.";
			d.winget_id = "BrechtSanders.WinLibs.POSIX.UCRT";
			d.linux_install = "sudo -n apt-get install -y g++ make";
			d.url = "https://winlibs.com/";
#ifdef _WIN32
			d.check_commands = { "g++ --version", "mingw32-make --version" };
#else
			d.check_commands = { "g++ --version" };
#endif
			list.push_back(d);
		}
		{
			Dependency d;
			d.name = "Git";
			d.description = "Fenêtre Git de l'éditeur et récupération des versions du moteur.";
			d.winget_id = "Git.Git";
			d.linux_install = "sudo -n apt-get install -y git";
			d.url = "https://git-scm.com/downloads";
			d.check_commands = { "git --version" };
			list.push_back(d);
		}
		{
			Dependency d;
			d.name = "Ollama";
			d.description = "Serveur de modèles locaux pour Lynxie, l'assistant IA de l'éditeur (localhost:11434).";
			d.winget_id = "Ollama.Ollama";
			d.linux_install = "curl -fsSL https://ollama.com/install.sh | sh";
			d.url = "https://ollama.com/download";
			d.check_commands = { "ollama --version" };
			list.push_back(d);
		}
		{
			Dependency d;
			d.name = "Tracy profiler 0.14.1";
			d.description = "Profiler externe : se connecte au moteur (fenêtre Profiler > Open Tracy). "
			                "Même version que le client compilé dans lynx.dll.";
			d.download_url = "https://github.com/wolfpld/tracy/releases/download/v0.14.1/windows-0.14.1.zip";
			d.tool_dir = "tools/tracy";
			d.tool_file = "tracy-profiler.exe";
			d.tool_version = "0.14.1";
			d.url = "https://github.com/wolfpld/tracy/releases/tag/v0.14.1";
			d.required = false;
			list.push_back(d);
		}
		return list;
	}

	// %LOCALAPPDATA%\Lynx\<tool_dir>
	static std::filesystem::path ToolDirectory(const Dependency& dep)
	{
#ifdef _WIN32
		const char* base = std::getenv("LOCALAPPDATA");
#else
		const char* base = std::getenv("HOME");
#endif
		return std::filesystem::path(base ? base : ".") / "Lynx" / dep.tool_dir;
	}

	void Check(Dependency& dep)
	{
		if (!dep.download_url.empty())
		{
			std::error_code ec;
			const bool present = std::filesystem::is_regular_file(ToolDirectory(dep) / dep.tool_file, ec);
			dep.state = present ? DepState::Installed : DepState::Missing;
			dep.version = present ? dep.tool_version : std::string();
			return;
		}

		Process::RefreshPath();
		dep.version.clear();
		for (const std::string& command : dep.check_commands)
		{
			int code = 0;
			std::string output = Process::Capture(command, &code);
			if (code != 0)
				continue;
			std::string version = ExtractVersion(output);
			if (version.empty())
				continue;
			if (!dep.required_prefix.empty() && version.rfind(dep.required_prefix, 0) != 0)
			{
				// Une autre version est installée : on la garde pour l'affichage.
				if (dep.version.empty())
					dep.version = version;
				continue;
			}
			dep.version = version;
			dep.state = DepState::Installed;
			return;
		}
		dep.state = DepState::Missing;
	}

	bool PackageManagerAvailable(std::string* details)
	{
#ifdef _WIN32
		int code = 0;
		std::string output = Process::Capture("winget --version", &code);
		if (details)
			*details = code == 0 ? "winget " + ExtractVersion(output)
			                     : "winget introuvable : installe « App Installer » depuis le Microsoft Store.";
		return code == 0;
#else
		if (details)
			*details = "Linux : installation via apt / script officiel (mode développement).";
		return true;
#endif
	}

	// Outil en archive : curl puis tar (fournis avec Windows 10 / 11).
	static bool InstallDownload(Dependency& dep, const LogFn& log)
	{
		const std::filesystem::path dir = ToolDirectory(dep);
		const std::filesystem::path archive = dir.parent_path() / (dir.filename().string() + ".zip");
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);

		const std::string download = "curl -L --fail -sS -o \"" + archive.string() + "\" \"" + dep.download_url + "\"";
		log("> " + download);
		int code = Process::Run(download, [&](const std::string& line) { log("  " + line); });
		if (code == 0)
		{
			const std::string extract = "tar -xf \"" + archive.string() + "\" -C \"" + dir.string() + "\"";
			log("> " + extract);
			code = Process::Run(extract, [&](const std::string& line) { log("  " + line); });
		}
		std::filesystem::remove(archive, ec);

		Check(dep);
		if (dep.state == DepState::Installed)
		{
			log(dep.name + " installé dans " + dir.string());
			return true;
		}
		dep.state = DepState::Failed;
		log("Échec de l'installation de " + dep.name + " (code " + std::to_string(code) + "). Téléchargement manuel : " + dep.url);
		return false;
	}

	bool Install(Dependency& dep, const LogFn& log)
	{
		dep.state = DepState::Installing;
		log("== Installation de " + dep.name + " ==");

		if (!dep.download_url.empty())
			return InstallDownload(dep, log);

#ifdef _WIN32
		std::string command = "winget install --id " + dep.winget_id +
			" -e --silent --accept-package-agreements --accept-source-agreements --disable-interactivity";
#else
		std::string command = dep.linux_install;
#endif
		log("> " + command);
		int code = Process::Run(command, [&](const std::string& line) {
			// winget dessine des barres de progression faites d'espaces / blocs
			if (line.find_first_not_of(" \t-\\|/") != std::string::npos)
				log("  " + line);
		});
		log("Code de sortie : " + std::to_string(code));

		// winget renvoie parfois un code non nul alors que c'est déjà installé :
		// on se fie à la détection.
		Check(dep);
		if (dep.state == DepState::Installed)
		{
			log(dep.name + " installé (" + dep.version + ").");
			return true;
		}
		dep.state = DepState::Failed;
		log("Échec : " + dep.name + " n'est pas détecté après l'installation. "
		    "Il faut peut-être relancer le launcher, ou l'installer à la main : " + dep.url);
		return false;
	}

	const char* StateLabel(DepState state)
	{
		switch (state)
		{
		case DepState::Unknown:    return "?";
		case DepState::Checking:   return "Vérification...";
		case DepState::Missing:    return "Manquant";
		case DepState::Installed:  return "Installé";
		case DepState::Installing: return "Installation...";
		case DepState::Failed:     return "Échec";
		}
		return "";
	}
}
