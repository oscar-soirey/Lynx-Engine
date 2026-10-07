#pragma once

/**
 * Acces interne (moteur uniquement) au registre ECS.
 * Pas de header EnTT ici : le registre est entierement dans Actor.cpp.
 */

#include <cstdint>
#include <vector>

#include "../Component.h"

namespace lynx
{
	class Actor;
}

namespace lynx::ecs
{
	constexpr uint32_t kNullEntity = 0xFFFFFFFFu;

	uint32_t CreateEntity(Actor* owner);

	/** Detruit les composants (sans EndPlay) puis l'entite. */
	void DestroyEntity(uint32_t entity);

	/** Met a jour l'acteur proprietaire (apres un deplacement d'Actor). */
	void RebindEntity(uint32_t entity, Actor* owner);

	/** @return nullptr si l'entite n'existe plus (acteur detruit). */
	Actor* GetActor(uint32_t entity);

	/**
	 * Copie dans `out` les acteurs qui ont un composant de ce type, dans
	 * l'ordre du niveau. On travaille toujours sur une copie : le code appele
	 * peut ajouter / retirer des composants pendant le parcours.
	 */
	void CollectActorsWith(uint32_t type_id, std::vector<Actor*>& out);

	/** Version typee de CollectActorsWith, qui retourne les composants. */
	template<typename T>
	void CollectComponents(std::vector<T*>& out);

	// ---- Cycle de vie, appele par Engine / Level ---------------------------

	/** Lancement du jeu : BeginPlay de tous les composants. */
	void BeginPlay();

	/** Arret du jeu : EndPlay de tous les composants demarres. */
	void EndPlay();

	/** Chaque frame, meme hors jeu. */
	void Update(float dt);

	/** Chaque tick de jeu : BeginPlay des nouveaux composants, puis Tick. */
	void Tick(float dt);

	/** Chaque frame, apres le gameplay : synchronisation finale vers le rendu. */
	void LateUpdate(float dt);

	/** Juste avant la destruction d'un acteur par le niveau (EndPlay). */
	void OnActorDestroyed(Actor* actor);

	/** Vrai entre BeginPlay() et EndPlay(). */
	bool IsPlaying();
}

// ---- implementation du template --------------------------------------------
#include "../Actor.h"

namespace lynx::ecs
{
	template<typename T>
	void CollectComponents(std::vector<T*>& out)
	{
		std::vector<Actor*> actors;
		CollectActorsWith(detail::ComponentTypeId<T>(), actors);
		out.clear();
		for (Actor* a : actors)
		{
			if (T* c = a->GetComponent<T>())
				out.push_back(c);
		}
	}
}
