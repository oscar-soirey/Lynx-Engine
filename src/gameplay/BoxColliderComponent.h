#pragma once

/**
 * Collider : boite de collision 2D (axes alignes) qui suit l'acteur.
 *
 *     auto& box = actor->AddComponent<lynx::ColliderComponent>();
 *     box.size = {1.f, 2.f};
 *     box.trigger = true;      // chevauchement seulement (ne bloque pas)
 *
 * Deux usages :
 *   - CHEVAUCHEMENT (overlap) : quand deux colliders se mettent a se
 *     chevaucher / se separent, chaque acteur recoit
 *         virtual void OnBeginOverlap(Actor* other)   (C++, voir Actor.h)
 *         virtual void OnEndOverlap(Actor* other)
 *     et en JS OnBeginOverlap(other) / OnEndOverlap(other) (methodes de la
 *     classe JS ou fonctions des scripts attaches), plus on_begin_overlap /
 *     on_end_overlap ici.
 *   - BLOCAGE (pas trigger) : un collider bloquant arrete les autres colliders
 *     bloquants et les voxels pleins. Le deplacement passe par
 *     MoveAndCollide(), et la vitesse (VelocityComponent, actor.velocity en JS)
 *     aussi : l'acteur glisse contre les murs, la vitesse s'annule sur l'axe
 *     bloque. Un contact donne OnHit(Actor* other, normal) aux DEUX acteurs
 *     (other = nullptr : un voxel), JS : OnHit(other, normal).
 *     Un collider "movable" qui se retrouve dans un collider bloquant (teleport,
 *     spawn, l'autre qui avance) en est repousse ; movable = false : mur fixe.
 *
 * Filtrage : deux colliders interagissent si (a.layer & b.mask) et (b.layer & a.mask).
 * Unites : monde (comme transform.location). Taille multipliee par |scale|.
 * JS : "Collider" (ancien nom accepte : "BoxCollider").
 */

#include <cstdint>
#include <functional>
#include <vector>

#include "Component.h"

namespace lynx
{
	class LYNX_API ColliderComponent : public Component
	{
	public:
		/** Taille (monde, avant la scale de l'acteur). */
		vec2 size{1.f, 1.f};

		/** Decalage du centre par rapport a l'acteur. */
		vec3 offset{0.f};

		/** Evenements de chevauchement seulement, ne bloque jamais. */
		bool trigger = false;

		/**
		 * Repousse hors des colliders bloquants qu'il chevauche (dans Tick).
		 * false : ne bouge jamais tout seul (mur, plateforme, ColliderActor).
		 */
		bool movable = true;

		/** OnBeginOverlap / OnEndOverlap (false : jamais, ex : un mur). */
		bool generate_overlap_events = true;

		uint32_t layer = 1u;
		uint32_t mask = 0xFFFFFFFFu;

		/** MoveAndCollide / OverlapsVoxels tiennent compte des voxels. */
		bool collide_with_voxels = true;

		/**
		 * Flags de voxel qui bloquent (voir voxels::GetFlagMask). 0 = voxels
		 * pleins ("LEFT" | "RIGHT" | "TOP" | "BOTTOM").
		 */
		uint32_t voxel_flags = 0u;

		/** Dessine la boite (aussi dans l'editeur). */
		bool debug_draw = false;

		std::function<void(ColliderComponent& other)> on_begin_overlap;
		std::function<void(ColliderComponent& other)> on_end_overlap;
		/** Contact bloquant : other = nullptr pour un voxel ; normal : vers cet acteur. */
		std::function<void(ColliderComponent* other, const vec3& normal)> on_hit;

		// ---------------------------------------------------------------
		// Requetes
		// ---------------------------------------------------------------

		/** Boite monde : centre et taille. */
		void GetWorldBox(float& center_x, float& center_y, float& width, float& height) const;

		/** Les boites (des autres acteurs) qui chevauchent celle-ci. */
		std::vector<ColliderComponent*> GetOverlapping() const;

		bool IsOverlapping(const ColliderComponent& other) const;

		/** true si les filtres layer / mask des deux boites se correspondent. */
		bool CanInteractWith(const ColliderComponent& other) const;

		/** Un voxel bloquant (voxel_flags) touche la boite. */
		bool OverlapsVoxels() const;

		/**
		 * La boite deplacee de `delta` (sans bouger l'acteur, sans OnHit)
		 * serait-elle bloquee (voxels / colliders bloquants compatibles) ?
		 * Toujours false pour un trigger. Sondes de sol, de plafond, marches...
		 */
		bool IsBlockedAt(const vec3& delta) const;

		// ---------------------------------------------------------------
		// Deplacement
		// ---------------------------------------------------------------

		/**
		 * Deplace l'acteur de `delta` (X puis Y, Z libre) en s'arretant contre
		 * les voxels bloquants et les boites non-trigger compatibles.
		 * @return le deplacement reellement applique.
		 */
		vec3 MoveAndCollide(const vec3& delta);

		/** Apres MoveAndCollide : bloque sur X / sur Y. */
		bool WasBlockedX() const { return blocked_x_; }
		bool WasBlockedY() const { return blocked_y_; }

		/**
		 * Sort des colliders bloquants chevauches (le plus court chemin, X ou Y).
		 * Appele chaque tick pour les colliders movable.
		 * @return true si l'acteur a ete deplace.
		 */
		bool ResolvePenetration();

	protected:
		void Update(float dt) override;
		void Tick(float dt) override;
		void EndPlay() override;

	private:
		/** `by` : le collider qui bloque (nullptr + `voxel` : un voxel). */
		bool BlockedAt(float center_x, float center_y, float width, float height,
		               ColliderComponent** by = nullptr, bool* voxel = nullptr) const;

		// Boites chevauchees au tick precedent (entites, jamais de pointeurs).
		std::vector<uint32_t> overlapping_;

		bool blocked_x_ = false;
		bool blocked_y_ = false;
	};

	/** Ancien nom (C++). */
	using BoxColliderComponent = ColliderComponent;
}
