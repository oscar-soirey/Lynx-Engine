#pragma once

/**
 * Interface interne du systeme de script, appelee par Engine / Actor.
 * Le cycle de vie des scripts (BeginPlay/Update/EndPlay) passe par
 * ScriptComponent, comme tous les composants (voir ecs:: dans ECS.h).
 * Pas de header QuickJS ici : tout est dans ScriptSystem.cpp.
 */

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lynx
{
	class Actor;
	class PlayerController;
	class UserWidget;
}

namespace lynx::scripting
{
	/** Cree le runtime JS (sinon cree a la demande au premier script). */
	void Init();

	/**
	 * Libere toutes les instances de script et le runtime JS.
	 * A appeler apres la destruction du niveau, avant la fin du programme.
	 */
	void Shutdown();

	/**
	 * Avant la creation d'un niveau (Engine::CreateLevel) : charge les classes
	 * JavaScript (n'importe ou dans assets/) la premiere fois et les enregistre dans la
	 * factory, et cree les constructeurs JS des classes C++ (Actor, Pawn...).
	 */
	void PrepareClasses();

	/** Appele par ~Actor : oublie l'objet JS associe a l'entite. */
	void OnEntityDestroyed(uint32_t entity);

	/**
	 * Appelle `function(other)` dans les scripts de `self` (ex : evenements
	 * OnBeginOverlap de BoxColliderComponent). `other` peut etre nullptr.
	 * @return true si au moins un script definissait la fonction.
	 */
	bool CallActorEvent(Actor* self, const char* function, Actor* other);


	// ---- Joueurs et widgets (GameplayBindings.cpp) -------------------------

	/**
	 * Appelle `function(player)` (OnPossessed, OnUnpossessed, ProcessInput)
	 * dans la classe JS et les scripts de `self`.
	 */
	bool CallPlayerEvent(Actor* self, const char* function, PlayerController* player);

	/**
	 * Appelle `function()` (classe JS et scripts de `self`) et retourne sa
	 * valeur comme booleen (false si elle n'existe pas). Conditions des graphes.
	 */
	bool CallActorPredicate(Actor* self, const char* function);

	// ---- Behavior Trees (AIBindings.cpp) -----------------------------------

	enum class AINodeKind { Task, Decorator, Service };

	/** Classe JS "class X extends BTTask / BTDecorator / BTService". */
	bool IsAIClass(const std::string& name, AINodeKind kind);

	/**
	 * Instance de la classe JS pour un noeud de l'arbre de `owner`
	 * (this.owner, this.blackboard, this.params). 0 : classe inconnue.
	 */
	uint32_t CreateAINode(AINodeKind kind, const std::string& class_name, Actor* owner,
	                      const std::vector<std::pair<std::string, std::string>>& params);
	void DestroyAINode(uint32_t handle);

	/**
	 * Appelle handle.method(dt). Resultat : 0 succes (true, undefined,
	 * "success"), 1 echec (false, "failure"), 2 en cours ("running").
	 * `found` : la methode existe.
	 */
	int CallAINode(uint32_t handle, const char* method, float dt, bool* found = nullptr);

	/** Fonction JS de l'acteur comme tache (meme resultat que CallAINode). */
	int CallActorTask(Actor* self, const char* function, float dt, bool* found = nullptr);

	/** Le joueur va etre detruit : son objet JS ne le designe plus. */
	void OnPlayerDestroyed(PlayerController* player);

	/** Le widget (id : Widget::GetUniqueId) est detruit : son objet JS est oublie. */
	void OnWidgetDestroyed(uint64_t widget_id);

	/** `name` est une classe JS "class X extends UserWidget". */
	bool IsWidgetClass(const std::string& name);

	enum class WidgetEvent { Construct, Tick, Destruct };

	/**
	 * Construct() / Tick(dt) / Destruct() de la classe JS du UserWidget (sa
	 * classe vient du fichier .widget ou de UI.create). Construct lie l'objet
	 * a sa classe JS si besoin.
	 */
	void OnUserWidgetEvent(UserWidget& widget, WidgetEvent event, float dt);
}
