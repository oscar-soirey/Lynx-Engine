#pragma once
#include <functional>
#include <string>

namespace Process
{
	// Lance une commande (via le shell) et appelle on_line pour chaque ligne de
	// sortie (stdout + stderr, les '\r' des barres de progression comptent comme
	// des fins de ligne). Bloquant : à appeler depuis un thread de travail.
	// Renvoie le code de sortie, ou -1 si la commande n'a pas pu être lancée.
	int Run(const std::string& command, const std::function<void(const std::string&)>& on_line = {});

	// Lance une commande et renvoie toute sa sortie (exit_code facultatif).
	std::string Capture(const std::string& command, int* exit_code = nullptr);

	// Lance un processus détaché (ex : `ollama serve`), sans attendre.
	bool Spawn(const std::string& command);

	// Windows : relit le PATH dans le registre (machine + utilisateur) pour que
	// les programmes qu'on vient d'installer soient trouvés sans relancer.
	void RefreshPath();

	// Ouvre une URL dans le navigateur.
	void OpenUrl(const std::string& url);
}
