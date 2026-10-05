#include "Models.h"
#include "Process.h"

#include <chrono>
#include <regex>
#include <sstream>
#include <thread>

namespace Models
{
	std::vector<ModelInfo> Catalog()
	{
		return {
			{ "qwen2.5-coder:7b",  "~4.7 Go", "8 Go",
			  "Modèle conseillé par la doc de l'éditeur : bon compromis vitesse / qualité pour générer les scripts lynx_editor.", true },
			{ "qwen2.5-coder:3b",  "~1.9 Go", "4 Go",
			  "Petit et rapide, pour les machines modestes. Moins fiable sur les scripts longs." },
			{ "qwen2.5-coder:14b", "~9.0 Go", "12 Go",
			  "Plus précis que le 7b, demande une carte graphique confortable." },
			{ "qwen3-coder:30b",   "~19 Go",  "24 Go",
			  "Modèle utilisé par l'évaluation de Lynxie (lynxie_eval). Le meilleur résultat, mais lourd." },
			{ "qwen3:8b",          "~5.2 Go", "8 Go",
			  "Modèle généraliste avec raisonnement, utile pour discuter du projet." },
		};
	}

	bool SameModel(const std::string& a, const std::string& b)
	{
		auto normalize = [](std::string s) {
			if (s.find(':') == std::string::npos)
				s += ":latest";
			return s;
		};
		return normalize(a) == normalize(b);
	}

	bool ServerRunning()
	{
		int code = 0;
		Process::Capture("ollama list", &code);
		return code == 0;
	}

	bool EnsureServer(const LogFn& log)
	{
		Process::RefreshPath();
		if (ServerRunning())
			return true;
		log("Démarrage du serveur Ollama (ollama serve)...");
		if (!Process::Spawn("ollama serve"))
		{
			log("Impossible de lancer `ollama serve`.");
			return false;
		}
		for (int i = 0; i < 20; ++i)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			if (ServerRunning())
			{
				log("Serveur Ollama prêt.");
				return true;
			}
		}
		log("Le serveur Ollama ne répond pas sur localhost:11434.");
		return false;
	}

	std::vector<std::string> ListInstalled()
	{
		std::vector<std::string> models;
		int code = 0;
		std::string output = Process::Capture("ollama list", &code);
		if (code != 0)
			return models;
		std::istringstream stream(output);
		std::string line;
		bool header = true;
		while (std::getline(stream, line))
		{
			if (header) { header = false; continue; }   // NAME ID SIZE MODIFIED
			std::istringstream fields(line);
			std::string name;
			if (fields >> name)
				models.push_back(name);
		}
		return models;
	}

	bool Pull(const std::string& tag, const LogFn& log, const std::function<void(float)>& on_progress)
	{
		if (!EnsureServer(log))
			return false;
		log("== Téléchargement de " + tag + " ==");
		static const std::regex percent(R"((\d{1,3})%)");
		std::string last_status;
		int code = Process::Run("ollama pull " + tag, [&](const std::string& raw) {
			// retire les séquences ANSI (curseur, effacement de ligne)
			static const std::regex ansi(R"(\x1B\[[0-9;?]*[A-Za-z])");
			std::string line = std::regex_replace(raw, ansi, "");
			if (line.find_first_not_of(' ') == std::string::npos)
				return;
			std::smatch match;
			if (std::regex_search(line, match, percent))
			{
				on_progress(std::stoi(match.str(1)) / 100.0f);
				return;   // pas de spam dans le journal
			}
			if (line != last_status)
			{
				last_status = line;
				log("  " + line);
			}
		});
		if (code != 0)
		{
			log("Échec du téléchargement de " + tag + " (code " + std::to_string(code) + ").");
			return false;
		}
		on_progress(1.0f);
		log(tag + " installé.");
		return true;
	}

	bool Remove(const std::string& tag, const LogFn& log)
	{
		if (!EnsureServer(log))
			return false;
		log("> ollama rm " + tag);
		int code = Process::Run("ollama rm " + tag, [&](const std::string& line) { log("  " + line); });
		return code == 0;
	}
}
