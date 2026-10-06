#include "VoxelPhysics.h"

#include "Engine.h"
#include "Level.h"
#include "Voxels.h"
#include "Profiler.h"
#include "../gameplay/Actor.h"
#include "../gameplay/BoxColliderComponent.h"
#include "../gameplay/CameraComponent.h"
#include "../gameplay/Interface.h"
#include "../gameplay/PlayerController.h"

#include <hrl/hrl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace lynx::voxel_physics
{
	using voxels::VoxelBehavior;
	using voxels::VoxelPhysicsProps;
	using voxels::VoxelType;

	namespace
	{
		bool g_enabled = true;
		int g_radius = 96;
		float g_step = 1.f / 30.f;
		float g_accumulator = 0.f;
		uint32_t g_frame = 0;
		uint32_t g_random = 0x9E3779B9u;

		struct Focus { float x, y; };
		std::vector<Focus> g_extra_focus;

		// Contacts of the last step : entity -> types touched.
		std::unordered_map<uint32_t, std::vector<uint8_t>> g_contacts;

		uint32_t Random()
		{
			g_random ^= g_random << 13;
			g_random ^= g_random >> 17;
			g_random ^= g_random << 5;
			return g_random;
		}

		struct TypeInfo
		{
			VoxelBehavior behavior = VoxelBehavior::Static;
			float density = 1.f;
			int dispersion = 0;
		};

		std::array<TypeInfo, 256> g_types;
		bool g_any_dynamic = false;

		void RefreshTypes()
		{
			g_any_dynamic = false;
			for (int i = 0; i < 256; ++i)
			{
				TypeInfo info;
				if (const VoxelType* t = voxels::GetType(static_cast<uint8_t>(i)))
				{
					info.behavior = t->physics.behavior;
					info.density = t->physics.density;
					info.dispersion = std::clamp(t->physics.dispersion, 0, 16);
				}
				g_types[i] = info;
				g_any_dynamic = g_any_dynamic || info.behavior != VoxelBehavior::Static;
			}
		}

		uint32_t Scene()
		{
			return Engine::GetScene();
		}

		bool ToVoxel(float wx, float wy, float& vx, float& vy)
		{
			return HRL_WorldToVoxelCoordinates(Scene(), wx, wy, &vx, &vy) == HRL_TRUE;
		}

		// Box of an actor in voxel cells (inclusive). false : no usable box.
		bool ActorCells(const Actor* actor, int& x0, int& y0, int& x1, int& y1, float grow = 0.f)
		{
			float cx = actor->transform.location.x, cy = actor->transform.location.y, w = 1.f, h = 1.f;
			if (auto* box = actor->GetComponent<ColliderComponent>())
				box->GetWorldBox(cx, cy, w, h);
			float ax, ay, bx, by;
			if (!ToVoxel(cx - w * 0.5f - grow, cy - h * 0.5f - grow, ax, ay) ||
			    !ToVoxel(cx + w * 0.5f + grow, cy + h * 0.5f + grow, bx, by))
				return false;
			if (bx < ax) std::swap(ax, bx);
			if (by < ay) std::swap(ay, by);
			x0 = static_cast<int>(std::floor(ax + 1e-4f));
			y0 = static_cast<int>(std::floor(ay + 1e-4f));
			x1 = static_cast<int>(std::floor(bx - 1e-4f));
			y1 = static_cast<int>(std::floor(by - 1e-4f));
			return x1 >= x0 && y1 >= y0 && (x1 - x0 + 1) * (y1 - y0 + 1) <= 16384;
		}

		const VoxelPhysicsProps* Props(uint8_t type)
		{
			const VoxelType* t = voxels::GetType(type);
			return t ? &t->physics : nullptr;
		}

		std::vector<Focus> CollectFocus()
		{
			std::vector<Focus> out = g_extra_focus;
			Engine* engine = Engine::Get();
			Level* level = engine ? engine->GetCurrentLevel() : nullptr;
			if (!engine || !level)
				return out;

			for (PlayerController* player : engine->GetPlayers())
			{
				if (!player)
					continue;
				bool found = false;
				const uint32_t view = player->GetViewCamera();
				for (Actor* a : level->GetActors())
				{
					auto* cam = a ? a->GetComponent<CameraComponent>() : nullptr;
					if (cam && cam->GetCameraId() == view)
					{
						const vec3 p = cam->GetCurrentLocation();
						out.push_back({ p.x, p.y });
						found = true;
						break;
					}
				}
				if (!found)
					if (Actor* pawn = player->GetPossessedActor())
						out.push_back({ pawn->transform.location.x, pawn->transform.location.y });
			}
			return out;
		}

		// ---------------------------------------------------------------------
		// One region : read, move, write back
		// ---------------------------------------------------------------------

		struct Region
		{
			int x0 = 0, y0 = 0, w = 0, h = 0;
			std::vector<uint8_t> cells;
			std::vector<uint8_t> blocked;   // inside a blocking collider
			std::vector<uint8_t> moved;     // moved this step (not twice)
			std::vector<uint8_t> dirty;     // to write back

			int Index(int x, int y) const { return (y - y0) * w + (x - x0); }
			bool Inside(int x, int y) const { return x >= x0 && y >= y0 && x < x0 + w && y < y0 + h; }
		};

		void MarkColliders(Region& r, Level& level)
		{
			for (Actor* a : level.GetActors())
			{
				auto* box = a ? a->GetComponent<ColliderComponent>() : nullptr;
				if (!box || box->trigger)
					continue;
				int x0, y0, x1, y1;
				if (!ActorCells(a, x0, y0, x1, y1))
					continue;
				x0 = std::max(x0, r.x0); y0 = std::max(y0, r.y0);
				x1 = std::min(x1, r.x0 + r.w - 1); y1 = std::min(y1, r.y0 + r.h - 1);
				for (int y = y0; y <= y1; ++y)
					for (int x = x0; x <= x1; ++x)
						r.blocked[r.Index(x, y)] = 1;
			}
		}

		// `type` may go into (x, y) : empty, or a lighter fluid it pushes away.
		bool CanEnter(const Region& r, uint8_t type, int x, int y)
		{
			if (!r.Inside(x, y))
				return false;
			const int i = r.Index(x, y);
			if (r.blocked[i] || r.moved[i])
				return false;
			const uint8_t other = r.cells[i];
			if (other == 0)
				return true;
			const TypeInfo& o = g_types[other];
			if (o.behavior != VoxelBehavior::Liquid && o.behavior != VoxelBehavior::Gas)
				return false;
			return g_types[type].density > o.density;
		}

		void Move(Region& r, int x, int y, int nx, int ny)
		{
			const int a = r.Index(x, y);
			const int b = r.Index(nx, ny);
			std::swap(r.cells[a], r.cells[b]);
			r.moved[b] = 1;
			if (r.cells[a] != 0)
				r.moved[a] = 1;   // the pushed fluid does not move again
			r.dirty[a] = r.dirty[b] = 1;
		}

		// Falling materials (gravity : -y).
		void StepFalling(Region& r, int x, int y)
		{
			const int i = r.Index(x, y);
			const uint8_t type = r.cells[i];
			const TypeInfo& info = g_types[type];

			// Down
			if (CanEnter(r, type, x, y - 1))
			{
				Move(r, x, y, x, y - 1);
				return;
			}

			// Down diagonals (random side first)
			const int side = (Random() & 1u) ? 1 : -1;
			for (int k = 0; k < 2; ++k)
			{
				const int dx = k == 0 ? side : -side;
				if (CanEnter(r, type, x + dx, y - 1) && CanEnter(r, type, x + dx, y))
				{
					Move(r, x, y, x + dx, y - 1);
					return;
				}
			}

			if (info.behavior != VoxelBehavior::Liquid)
				return;

			// Liquid : sideways, up to `dispersion` cells.
			for (int k = 0; k < 2; ++k)
			{
				const int dx = k == 0 ? side : -side;
				int reach = 0;
				for (int s = 1; s <= std::max(1, info.dispersion); ++s)
				{
					if (!CanEnter(r, type, x + dx * s, y))
						break;
					reach = s;
					// Falls as soon as there is a hole below.
					if (CanEnter(r, type, x + dx * s, y - 1))
						break;
				}
				if (reach > 0)
				{
					Move(r, x, y, x + dx * reach, y);
					return;
				}
			}
		}

		// Gases (rise : +y).
		void StepGas(Region& r, int x, int y)
		{
			const uint8_t type = r.cells[r.Index(x, y)];
			const TypeInfo& info = g_types[type];
			// A gas goes into empty cells only (or a lighter gas).
			auto can = [&](int nx, int ny)
			{
				if (!r.Inside(nx, ny))
					return false;
				const int i = r.Index(nx, ny);
				if (r.blocked[i] || r.moved[i])
					return false;
				const uint8_t other = r.cells[i];
				return other == 0 || (g_types[other].behavior == VoxelBehavior::Gas && g_types[other].density > info.density);
			};

			if (can(x, y + 1))
			{
				Move(r, x, y, x, y + 1);
				return;
			}
			const int side = (Random() & 1u) ? 1 : -1;
			for (int k = 0; k < 2; ++k)
			{
				const int dx = k == 0 ? side : -side;
				if (can(x + dx, y + 1))
				{
					Move(r, x, y, x + dx, y + 1);
					return;
				}
			}
			for (int k = 0; k < 2; ++k)
			{
				const int dx = k == 0 ? side : -side;
				int reach = 0;
				for (int s = 1; s <= std::max(1, info.dispersion); ++s)
				{
					if (!can(x + dx * s, y))
						break;
					reach = s;
				}
				if (reach > 0 && (Random() % 3u) == 0)   // drifts slowly
				{
					Move(r, x, y, x + dx * reach, y);
					return;
				}
			}
		}

		void Simulate(Region& r, Level& level)
		{
			const uint32_t scene = Scene();
			const size_t n = static_cast<size_t>(r.w) * static_cast<size_t>(r.h);
			r.cells.resize(n);
			bool any = false;
			for (int y = 0; y < r.h; ++y)
				for (int x = 0; x < r.w; ++x)
				{
					const uint8_t t = static_cast<uint8_t>(HRL_GetVoxelType(scene, r.x0 + x, r.y0 + y));
					r.cells[static_cast<size_t>(y) * r.w + x] = t;
					any = any || g_types[t].behavior != VoxelBehavior::Static;
				}
			if (!any)
				return;

			r.blocked.assign(n, 0);
			r.moved.assign(n, 0);
			r.dirty.assign(n, 0);
			MarkColliders(r, level);

			const bool flip = (g_frame & 1u) != 0;

			// Powders and liquids : bottom row first.
			for (int y = r.y0; y < r.y0 + r.h; ++y)
				for (int k = 0; k < r.w; ++k)
				{
					const int x = flip ? r.x0 + r.w - 1 - k : r.x0 + k;
					const int i = r.Index(x, y);
					if (r.moved[i] || r.blocked[i])
						continue;
					const VoxelBehavior b = g_types[r.cells[i]].behavior;
					if (b == VoxelBehavior::Powder || b == VoxelBehavior::Liquid)
						StepFalling(r, x, y);
				}

			// Gases : top row first.
			for (int y = r.y0 + r.h - 1; y >= r.y0; --y)
				for (int k = 0; k < r.w; ++k)
				{
					const int x = flip ? r.x0 + k : r.x0 + r.w - 1 - k;
					const int i = r.Index(x, y);
					if (r.moved[i] || r.blocked[i])
						continue;
					if (g_types[r.cells[i]].behavior == VoxelBehavior::Gas)
						StepGas(r, x, y);
				}

			bool began = false;
			for (int y = 0; y < r.h; ++y)
				for (int x = 0; x < r.w; ++x)
				{
					const size_t i = static_cast<size_t>(y) * r.w + x;
					if (!r.dirty[i])
						continue;
					if (!began)
					{
						HRL_BeginVoxelEdit(scene);
						began = true;
					}
					HRL_SetVoxelType(scene, r.x0 + x, r.y0 + y, r.cells[i]);
				}
			if (began)
				HRL_EndVoxelEdit(scene);
		}

		// ---------------------------------------------------------------------
		// Contacts : damage and VoxelEvents
		// ---------------------------------------------------------------------

		void Contacts(Level& level, float dt)
		{
			std::unordered_map<uint32_t, std::vector<uint8_t>> now;
			const std::vector<Actor*> actors = level.GetActors();

			for (Actor* a : actors)
			{
				if (!a || !a->GetComponent<ColliderComponent>())
					continue;
				int x0, y0, x1, y1;
				// Grown a little : touching from outside counts (standing on lava).
				if (!ActorCells(a, x0, y0, x1, y1, 0.1f))
					continue;

				std::vector<uint8_t> types;
				for (int y = y0; y <= y1; ++y)
					for (int x = x0; x <= x1; ++x)
					{
						const uint8_t t = voxels::GetTypeAt(x, y);
						if (t == 0 || std::find(types.begin(), types.end(), t) != types.end())
							continue;
						const VoxelPhysicsProps* p = Props(t);
						if (p && (p->damage > 0.f || p->contact_events))
							types.push_back(t);
					}
				if (!types.empty())
					now[a->GetEntity()] = types;
			}

			auto find_actor = [&](uint32_t entity) -> Actor*
			{
				for (Actor* a : level.GetActors())
					if (a && a->GetEntity() == entity)
						return a;
				return nullptr;
			};

			// Begin / damage
			for (const auto& [entity, types] : now)
			{
				const auto& before = g_contacts[entity];
				for (uint8_t t : types)
				{
					Actor* a = find_actor(entity);
					if (!a)
						break;
					const VoxelType* type = voxels::GetType(t);
					if (!type)
						continue;
					if (type->physics.contact_events && std::find(before.begin(), before.end(), t) == before.end())
						interfaces::Call(a, "VoxelEvents", "OnVoxelContact", { type->name, static_cast<int>(t) });
					if (type->physics.damage > 0.f)
						if ((a = find_actor(entity)))
							ApplyDamage(a, type->physics.damage * dt, nullptr);
				}
			}

			// End
			for (const auto& [entity, types] : g_contacts)
			{
				const auto it = now.find(entity);
				for (uint8_t t : types)
				{
					if (it != now.end() && std::find(it->second.begin(), it->second.end(), t) != it->second.end())
						continue;
					const VoxelType* type = voxels::GetType(t);
					Actor* a = find_actor(entity);
					if (a && type && type->physics.contact_events)
						interfaces::Call(a, "VoxelEvents", "OnVoxelContactEnd", { type->name, static_cast<int>(t) });
				}
			}

			g_contacts = std::move(now);
		}

		void Step()
		{
			Engine* engine = Engine::Get();
			Level* level = engine ? engine->GetCurrentLevel() : nullptr;
			if (!level || !HRL_IsValidScene(Scene()))
				return;

			++g_frame;
			RefreshTypes();

			if (g_any_dynamic)
			{
				std::vector<Region> regions;
				for (const Focus& f : CollectFocus())
				{
					float vx, vy;
					if (!ToVoxel(f.x, f.y, vx, vy))
						continue;
					Region r;
					r.x0 = static_cast<int>(std::floor(vx)) - g_radius;
					r.y0 = static_cast<int>(std::floor(vy)) - g_radius;
					r.w = r.h = g_radius * 2 + 1;
					r.x0 = std::max(r.x0, 0);
					r.y0 = std::max(r.y0, 0);

					// Close to another region : one bigger region instead.
					bool merged = false;
					for (Region& o : regions)
					{
						if (r.x0 < o.x0 + o.w + 8 && o.x0 < r.x0 + r.w + 8 && r.y0 < o.y0 + o.h + 8 && o.y0 < r.y0 + r.h + 8)
						{
							const int nx1 = std::max(o.x0 + o.w, r.x0 + r.w), ny1 = std::max(o.y0 + o.h, r.y0 + r.h);
							o.x0 = std::min(o.x0, r.x0);
							o.y0 = std::min(o.y0, r.y0);
							o.w = nx1 - o.x0;
							o.h = ny1 - o.y0;
							merged = true;
							break;
						}
					}
					if (!merged)
						regions.push_back(r);
				}

				for (Region& r : regions)
					Simulate(r, *level);
			}

			Contacts(*level, g_step);
		}
	}

	void SetEnabled(bool enabled) { g_enabled = enabled; }
	bool IsEnabled() { return g_enabled; }
	void SetRadius(int voxels) { g_radius = std::clamp(voxels, 8, 512); }
	int GetRadius() { return g_radius; }
	void SetRate(float steps) { g_step = 1.f / std::clamp(steps, 1.f, 240.f); }

	void AddFocus(float x, float y)
	{
		if (g_extra_focus.size() < 16)
			g_extra_focus.push_back({ x, y });
	}

	void Reset()
	{
		g_contacts.clear();
		g_accumulator = 0.f;
	}

	void Tick(float dt)
	{
		LYNX_PROFILE_SCOPE("Voxel physics");
		if (g_enabled && dt > 0.f)
		{
			g_accumulator = std::min(g_accumulator + dt, g_step * 3.f);
			while (g_accumulator >= g_step)
			{
				g_accumulator -= g_step;
				Step();
			}
		}
		g_extra_focus.clear();
	}

	Medium GetMedium(const Actor* actor)
	{
		Medium m;
		if (!actor || !HRL_IsValidScene(Scene()))
			return m;

		int x0, y0, x1, y1;
		if (!ActorCells(actor, x0, y0, x1, y1))
			return m;

		int total = 0, fluid = 0;
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x)
			{
				++total;
				const VoxelPhysicsProps* p = Props(voxels::GetTypeAt(x, y));
				if (!p || (p->behavior != VoxelBehavior::Liquid && p->behavior != VoxelBehavior::Gas))
					continue;
				++fluid;
				m.drag = std::max(m.drag, p->drag);
				m.buoyancy = std::max(m.buoyancy, p->buoyancy);
			}
		m.fluid_fraction = total > 0 ? static_cast<float>(fluid) / static_cast<float>(total) : 0.f;

		// Ground : the row just below the box.
		const int below = y0 - 1;
		for (int x = x0; x <= x1; ++x)
		{
			const uint8_t t = voxels::GetTypeAt(x, below);
			if (t == 0)
				continue;
			const VoxelPhysicsProps* p = Props(t);
			if (p && (p->behavior == VoxelBehavior::Liquid || p->behavior == VoxelBehavior::Gas))
				continue;
			m.ground_type = t;
			m.ground_friction = p ? p->friction : 1.f;
			m.ground_bounciness = p ? p->bounciness : 0.f;
			break;
		}
		return m;
	}
}
