#pragma once

/**
 * Classe de base de tous les composants.
 *
 * Pour creer un composant (moteur OU jeu), il suffit d'heriter de Component :
 *
 *     struct HealthComponent : lynx::Component
 *     {
 *         float hp = 100.f;
 *
 *     protected:
 *         void BeginPlay() override { hp = 100.f; }
 *         void Tick(float dt) override
 *         {
 *             if (hp <= 0.f)
 *                 lynx::Engine::Get()->GetCurrentLevel()->DestroyActor(GetOwner());
 *         }
 *     };
 *
 *     auto& health = actor->AddComponent<HealthComponent>();
 *
 * Pas d'enregistrement a faire : le type est identifie automatiquement.
 * Le stockage se fait dans un registre EnTT cote moteur (jamais expose ici).
 *
 * Cycle de vie (appele par le moteur) :
 *   OnAttach()      juste apres l'ajout a l'acteur (GetOwner() est valide)
 *   BeginPlay()     au lancement du jeu, ou au tick suivant si ajoute en jeu
 *   Update(dt)      chaque frame, meme hors jeu (editeur)
 *   Tick(dt)        chaque tick de jeu (si tick_enabled)
 *   EndPlay()       arret du jeu, retrait du composant ou destruction de l'acteur
 *   ~Destructeur    a la suppression (l'acteur proprietaire peut etre deja
 *                   partiellement detruit : ne pas l'utiliser ici)
 */

#include <cstdint>
#include <typeinfo>

#include "../core/Common.h"

namespace lynx
{
	class Actor;

	class LYNX_API Component
	{
	public:
		Component() = default;
		virtual ~Component() = default;

		Component(const Component&) = delete;
		Component& operator=(const Component&) = delete;

		/** Acteur proprietaire (nullptr si le composant n'est pas attache). */
		Actor* GetOwner() const;

		/** Vrai entre BeginPlay et EndPlay. */
		bool HasBegunPlay() const { return begun_play_; }

		/** Si faux, Tick n'est pas appele (BeginPlay/EndPlay le sont). */
		bool tick_enabled = true;

	protected:
		virtual void OnAttach() {}
		virtual void BeginPlay() {}
		virtual void Update(float dt) {}
		virtual void Tick(float dt) {}
		virtual void EndPlay() {}

	private:
		friend class Actor;
		friend struct ComponentAccess; // moteur uniquement

		uint32_t entity_ = 0xFFFFFFFFu;
		bool begun_play_ = false;
		bool pending_destroy_ = false;
	};

	namespace detail
	{
		/** Hash stable d'un nom de type (identique dans le moteur et le jeu). */
		LYNX_API uint32_t HashComponentType(const char* type_name);

		template<typename T>
		uint32_t ComponentTypeId()
		{
			static const uint32_t id = HashComponentType(typeid(T).name());
			return id;
		}
	}
}
