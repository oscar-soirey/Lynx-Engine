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

	/** Appele par ~Actor : oublie l'objet JS associe a l'entite. */
	void OnEntityDestroyed(uint32_t entity);
}
