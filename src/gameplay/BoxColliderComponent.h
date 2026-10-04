#pragma once

/**
 * Boite de collision 2D (axes alignes) qui suit l'acteur.
 *
 *     auto& box = actor->AddComponent<lynx::BoxColliderComponent>();
 *     box.size = {1.f, 2.f};
 *     box.on_begin_overlap = [](lynx::BoxColliderComponent& other) { ... };
 *
 *     // deplacement qui s'arrete contre les voxels pleins / les boites bloquantes
 *     const lynx::vec3 moved = box.MoveAndCollide({vx * dt, vy * dt, 0.f});
 *
 * Evenements (pendant le jeu) : on_begin_overlap / on_end_overlap en C++, et les
 * fonctions OnBeginOverlap(other) / OnEndOverlap(other) des scripts de l'acteur.
 *
 * Filtrage : deux boites interagissent si (a.layer & b.mask) et (b.layer & a.mask).
 * Une boite "trigger" ne bloque jamais (evenements seulement).
 * Unites : monde (comme transform.location). Taille multipliee par |scale|.
 */

#include <cstdint>
#include <functional>
#include <vector>

#include "Component.h"

namespace lynx
{
	class LYNX_API BoxColliderComponent : public Component
	{
	public:
		/** Taille (monde, avant la scale de l'acteur). */
		vec2 size{1.f, 1.f};

		/** Decalage du centre par rapport a l'acteur. */
		vec3 offset{0.f};

		/** Evenements de chevauchement seulement, ne bloque jamais. */
		bool trigger = false;

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

		std::function<void(BoxColliderComponent& other)> on_begin_overlap;
		std::function<void(BoxColliderComponent& other)> on_end_overlap;

		// ---------------------------------------------------------------
		// Requetes
		// ---------------------------------------------------------------

		/** Boite monde : centre et taille. */
		void GetWorldBox(float& center_x, float& center_y, float& width, float& height) const;

		/** Les boites (des autres acteurs) qui chevauchent celle-ci. */
		std::vector<BoxColliderComponent*> GetOverlapping() const;

		bool IsOverlapping(const BoxColliderComponent& other) const;

		/** true si les filtres layer / mask des deux boites se correspondent. */
		bool CanInteractWith(const BoxColliderComponent& other) const;

		/** Un voxel bloquant (voxel_flags) touche la boite. */
		bool OverlapsVoxels() const;

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

	protected:
		void Update(float dt) override;
		void Tick(float dt) override;
		void EndPlay() override;

	private:
		bool BlockedAt(float center_x, float center_y, float width, float height) const;

		// Boites chevauchees au tick precedent (entites, jamais de pointeurs).
		std::vector<uint32_t> overlapping_;

		bool blocked_x_ = false;
		bool blocked_y_ = false;
	};
}
