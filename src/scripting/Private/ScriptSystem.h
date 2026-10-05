#pragma once

/**
 * Interface interne du systeme de script, appelee par Engine / Actor.
 * Le cycle de vie des scripts (BeginPlay/Update/EndPlay) passe par
 * ScriptComponent, comme tous les composants (voir ecs:: dans ECS.h).
 * Pas de header QuickJS ici : tout est dans ScriptSystem.cpp.
 */

#include <cstdint>

namespace lynx
{
	class Actor;
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
}
