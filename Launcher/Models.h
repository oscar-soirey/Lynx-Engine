#pragma once
#include "Dependencies.h"

#include <string>
#include <vector>

struct ModelInfo
{
	std::string tag;          // nom Ollama (ex : qwen2.5-coder:7b)
	std::string size;         // taille approximative du téléchargement
	std::string vram;         // mémoire conseillée
	std::string description;
	bool recommended = false;

	// état
	bool installed = false;
	bool selected = false;    // case à cocher dans l'UI
	float progress = -1.0f;   // 0..1 pendant le téléchargement
};

namespace Models
{
	std::vector<ModelInfo> Catalog();

	// Le serveur Ollama répond-il ? (`ollama list` réussit)
	bool ServerRunning();

	// Démarre `ollama serve` si besoin et attend qu'il réponde (quelques secondes).
	bool EnsureServer(const LogFn& log);

	// Noms des modèles installés (sortie de `ollama list`).
	std::vector<std::string> ListInstalled();

	// `ollama pull` ; on_progress reçoit 0..1. Bloquant.
	bool Pull(const std::string& tag, const LogFn& log, const std::function<void(float)>& on_progress);

	// `ollama rm`.
	bool Remove(const std::string& tag, const LogFn& log);

	// Compare un tag du catalogue à un nom renvoyé par Ollama (":latest" implicite).
	bool SameModel(const std::string& a, const std::string& b);
}
