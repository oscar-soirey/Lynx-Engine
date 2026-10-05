#include "BoxColliderComponent.h"

#include "Actor.h"
#include "Private/ECS.h"
#include "../core/Engine.h"
#include "../core/Voxels.h"
#include "../scripting/Private/ScriptSystem.h"

#include <hrl/hrl.h>

#include <algorithm>
#include <cmath>

namespace lynx
{
	namespace
	{
		// Deux boites qui se touchent juste (bord contre bord) ne se chevauchent pas.
		constexpr float kEpsilon = 1e-4f;

		// Une boite tres grande ne teste pas des millions de voxels.
		constexpr long long kMaxVoxelCells = 200000;

		bool BoxesOverlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
		{
			return std::fabs(ax - bx) * 2.f < (aw + bw) - kEpsilon &&
			       std::fabs(ay - by) * 2.f < (ah + bh) - kEpsilon;
		}

		uint32_t SolidVoxelFlags()
		{
			return HRL_VOXEL_COLLISION_LEFT | HRL_VOXEL_COLLISION_RIGHT |
			       HRL_VOXEL_COLLISION_TOP | HRL_VOXEL_COLLISION_BOTTOM;
		}

		bool VoxelBoxBlocked(float cx, float cy, float w, float h, uint32_t flags)
		{
			const uint32_t scene = Engine::GetScene();

			float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;

			if (HRL_WorldToVoxelCoordinates(scene, cx - w * 0.5f, cy - h * 0.5f, &x0, &y0) != HRL_TRUE ||
			    HRL_WorldToVoxelCoordinates(scene, cx + w * 0.5f, cy + h * 0.5f, &x1, &y1) != HRL_TRUE)
			{
				return false;
			}

			if (x1 < x0) std::swap(x0, x1);
			if (y1 < y0) std::swap(y0, y1);

			const int min_x = static_cast<int>(std::floor(x0 + kEpsilon));
			const int max_x = static_cast<int>(std::floor(x1 - kEpsilon));
			const int min_y = static_cast<int>(std::floor(y0 + kEpsilon));
			const int max_y = static_cast<int>(std::floor(y1 - kEpsilon));

			if (max_x < min_x || max_y < min_y)
				return false;

			const long long cells =
				static_cast<long long>(max_x - min_x + 1) * static_cast<long long>(max_y - min_y + 1);

			if (cells > kMaxVoxelCells)
				return false;

			for (int y = min_y; y <= max_y; ++y)
			{
				for (int x = min_x; x <= max_x; ++x)
				{
					if ((voxels::GetFlagsAt(x, y) & flags) != 0u)
						return true;
				}
			}

			return false;
		}

		std::vector<BoxColliderComponent*> AllColliders()
		{
			std::vector<BoxColliderComponent*> out;
			ecs::CollectComponents(out);
			return out;
		}
	}


	void BoxColliderComponent::GetWorldBox(float& center_x, float& center_y, float& width, float& height) const
	{
		Actor* owner = GetOwner();

		if (!owner)
		{
			center_x = center_y = width = height = 0.f;
			return;
		}

		const transform& t = owner->transform;
		const float flip_x = t.scale.x < 0.f ? -1.f : 1.f;
		const float flip_y = t.scale.y < 0.f ? -1.f : 1.f;
		const float abs_x = std::fabs(t.scale.x);
		const float abs_y = std::fabs(t.scale.y);

		center_x = t.location.x + offset.x * abs_x * flip_x;
		center_y = t.location.y + offset.y * abs_y * flip_y;
		width = std::fabs(size.x) * abs_x;
		height = std::fabs(size.y) * abs_y;
	}

	bool BoxColliderComponent::CanInteractWith(const BoxColliderComponent& other) const
	{
		return (layer & other.mask) != 0u && (other.layer & mask) != 0u;
	}

	bool BoxColliderComponent::IsOverlapping(const BoxColliderComponent& other) const
	{
		if (&other == this || other.GetOwner() == GetOwner())
			return false;

		float ax, ay, aw, ah, bx, by, bw, bh;
		GetWorldBox(ax, ay, aw, ah);
		other.GetWorldBox(bx, by, bw, bh);

		return BoxesOverlap(ax, ay, aw, ah, bx, by, bw, bh);
	}

	std::vector<BoxColliderComponent*> BoxColliderComponent::GetOverlapping() const
	{
		std::vector<BoxColliderComponent*> out;

		for (BoxColliderComponent* other : AllColliders())
		{
			if (CanInteractWith(*other) && IsOverlapping(*other))
				out.push_back(other);
		}

		return out;
	}

	bool BoxColliderComponent::OverlapsVoxels() const
	{
		float cx, cy, w, h;
		GetWorldBox(cx, cy, w, h);
		return VoxelBoxBlocked(cx, cy, w, h, voxel_flags ? voxel_flags : SolidVoxelFlags());
	}

	bool BoxColliderComponent::BlockedAt(float cx, float cy, float w, float h) const
	{
		if (trigger)
			return false;

		if (collide_with_voxels &&
		    VoxelBoxBlocked(cx, cy, w, h, voxel_flags ? voxel_flags : SolidVoxelFlags()))
		{
			return true;
		}

		for (BoxColliderComponent* other : AllColliders())
		{
			if (other == this || other->trigger || other->GetOwner() == GetOwner() || !CanInteractWith(*other))
				continue;

			float bx, by, bw, bh;
			other->GetWorldBox(bx, by, bw, bh);

			if (BoxesOverlap(cx, cy, w, h, bx, by, bw, bh))
				return true;
		}

		return false;
	}

	vec3 BoxColliderComponent::MoveAndCollide(const vec3& delta)
	{
		blocked_x_ = blocked_y_ = false;

		Actor* owner = GetOwner();

		if (!owner)
			return vec3(0.f);

		float cx, cy, w, h;
		GetWorldBox(cx, cy, w, h);

		// Deja coince dans quelque chose : on laisse sortir.
		const bool stuck = BlockedAt(cx, cy, w, h);

		// Pas maximal : un deplacement plus long ne doit pas traverser un mur
		// fin. Moitie de la boite, et au plus un demi voxel.
		float step = 0.5f * std::max(std::min(w, h), 1e-3f);
		{
			float vx0 = 0.f, vy0 = 0.f, vx1 = 0.f, vy1 = 0.f;
			const uint32_t scene = Engine::GetScene();

			if (HRL_WorldToVoxelCoordinates(scene, 0.f, 0.f, &vx0, &vy0) == HRL_TRUE &&
			    HRL_WorldToVoxelCoordinates(scene, 1.f, 0.f, &vx1, &vy1) == HRL_TRUE)
			{
				const float voxels_per_unit = std::fabs(vx1 - vx0);

				if (voxels_per_unit > 1e-6f)
					step = std::min(step, 0.5f / voxels_per_unit);
			}
		}

		// Avance sur un axe par pas ; au premier pas bloque, recherche
		// dichotomique de la plus grande avance possible dans ce pas.
		auto solve_axis = [&](float move, bool axis_x, bool& blocked) -> float
		{
			if (move == 0.f || stuck)
				return move;

			auto free_at = [&](float m)
			{
				return axis_x ? !BlockedAt(cx + m, cy, w, h) : !BlockedAt(cx, cy + m, w, h);
			};

			const float dir = move > 0.f ? 1.f : -1.f;
			const float total = std::fabs(move);
			float done = 0.f;

			// Limite de securite (deplacements enormes).
			for (int i = 0; i < 4096 && done < total; ++i)
			{
				const float next = std::min(done + step, total);

				if (free_at(dir * next))
				{
					done = next;
					continue;
				}

				blocked = true;

				float lo = done;
				float hi = next;

				for (int j = 0; j < 12; ++j)
				{
					const float mid = (lo + hi) * 0.5f;

					if (free_at(dir * mid))
						lo = mid;
					else
						hi = mid;
				}

				return dir * lo;
			}

			return dir * done;
		};

		vec3 applied;
		applied.x = solve_axis(delta.x, true, blocked_x_);
		cx += applied.x;
		applied.y = solve_axis(delta.y, false, blocked_y_);
		applied.z = delta.z;

		owner->transform.location += applied;
		return applied;
	}

	void BoxColliderComponent::Tick(float)
	{
		Actor* owner = GetOwner();

		if (!owner)
			return;

		std::vector<uint32_t> now;

		for (BoxColliderComponent* other : GetOverlapping())
		{
			if (Actor* other_owner = other->GetOwner())
				now.push_back(other_owner->GetEntity());
		}

		std::sort(now.begin(), now.end());

		const uint32_t self_entity = owner->GetEntity();

		// Les callbacks peuvent detruire / modifier des acteurs : chaque autre
		// boite est retrouvee par son entite, juste avant son evenement.
		auto fire = [&](uint32_t entity, bool begin)
		{
			Actor* self = ecs::GetActor(self_entity);
			Actor* other_actor = ecs::GetActor(entity);

			if (!self)
				return;

			BoxColliderComponent* other =
				other_actor ? other_actor->GetComponent<BoxColliderComponent>() : nullptr;

			if (other)
			{
				const auto& callback = begin ? on_begin_overlap : on_end_overlap;

				if (callback)
				{
					// copie : le callback peut se remplacer lui-meme
					auto copy = callback;
					copy(*other);
				}
			}

			scripting::CallActorEvent(self, begin ? "OnBeginOverlap" : "OnEndOverlap", other_actor);
		};

		const std::vector<uint32_t> before = overlapping_;
		overlapping_ = now;

		for (uint32_t e : now)
		{
			if (!std::binary_search(before.begin(), before.end(), e))
				fire(e, true);
		}

		for (uint32_t e : before)
		{
			if (!std::binary_search(now.begin(), now.end(), e))
				fire(e, false);
		}
	}

	void BoxColliderComponent::EndPlay()
	{
		overlapping_.clear();
	}

	void BoxColliderComponent::Update(float)
	{
		if (!debug_draw)
			return;

		float cx, cy, w, h;
		GetWorldBox(cx, cy, w, h);

		Actor* owner = GetOwner();
		const float z = (owner ? owner->transform.location.z : 0.f) + offset.z;

		const float xs[] = { cx - w * 0.5f, cx + w * 0.5f, cx + w * 0.5f, cx - w * 0.5f };
		const float ys[] = { cy - h * 0.5f, cy - h * 0.5f, cy + h * 0.5f, cy + h * 0.5f };
		const float zs[] = { z, z, z, z };

		// vert : trigger, rouge : bloquant
		HRL_DrawDebugPolygon(
			Engine::GetScene(),
			HRL_DEBUG_HOLLOW,
			xs, ys, zs, 4,
			trigger ? 0.2f : 1.f,
			trigger ? 1.f : 0.2f,
			0.2f
		);
	}
}
