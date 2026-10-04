#pragma once

/**
 * API publique du scripting JavaScript (QuickJS-ng).
 *
 * Les scripts s'attachent aux acteurs via ScriptComponent / Actor::AddScript,
 * ou via la propriete "scripts" dans le fichier de niveau :
 *     <Player object_id_="player" scripts="scripts/Player.js"/>
 *
 * Voir scripting/README.md pour l'API disponible cote JS.
 */

#include <string>
#include <vector>

#include "../core/Common.h"

namespace lynx
{
	/**
	 * Recompile tous les scripts depuis le disque et les re-instancie
	 * (hot reload). EndPlay est appele sur les anciennes instances, BeginPlay
	 * sera rappele au prochain tick si le jeu tourne.
	 */
	LYNX_API void ReloadScripts();

	/**
	 * Execute du code JS dans le contexte global (console de debug, tests...).
	 * Les erreurs sont affichees sur la sortie standard.
	 * @return false si le code a leve une exception.
	 */
	LYNX_API bool ExecuteScript(const char* code, const char* name = "<console>");

	/**
	 * Comme ExecuteScript, mais rend la valeur de la derniere expression en
	 * JSON (`result_json`, "null" si undefined) et le message d'erreur
	 * (`error`, avec la pile) au lieu de les afficher. Commandes de l'editeur.
	 */
	LYNX_API bool EvaluateScript(const char* code, std::string& result_json, std::string& error,
	                             const char* name = "<eval>");

	class Actor;

	/**
	 * Appelle une fonction JS de l'acteur : methode de sa classe JavaScript
	 * (class Player extends Actor) et fonctions de ses scripts attaches.
	 * @return true si au moins l'un des deux la definissait.
	 */
	LYNX_API bool CallScriptFunction(Actor* actor, const char* name, const std::vector<double>& args = {});
}
