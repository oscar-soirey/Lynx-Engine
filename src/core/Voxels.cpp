#include "Voxels.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <hrl/hrl.h>
#include <json/json.hpp>

#include "Engine.h"
#include "Filesystem.h"
#include "Utils.h"

namespace lynx::voxels
{
	namespace
	{
		std::vector<VoxelType> g_types;

		// Game flags declared in voxels.json : name -> mask.
		std::vector<std::pair<std::string, uint32_t>> g_flags;

		std::unordered_map<std::string, DestroyedEventFn> g_events;
		std::unordered_set<std::string> g_warned;

		std::string g_error;

		const uint32_t kCollisionSolid =
			HRL_VOXEL_COLLISION_LEFT |
			HRL_VOXEL_COLLISION_RIGHT |
			HRL_VOXEL_COLLISION_TOP |
			HRL_VOXEL_COLLISION_BOTTOM;

		const uint32_t kCollisionAll =
			kCollisionSolid | HRL_VOXEL_COLLISION_INSIDE;

		// Game flags start at this bit (the scene keeps the collision flags
		// in the low bits ; same bits as the old GAME_VOXEL_* defines).
		constexpr int kFirstGameFlagBit = 5;

		uint32_t CollisionFlag(const std::string& name)
		{
			if (name == "LEFT")   return HRL_VOXEL_COLLISION_LEFT;
			if (name == "RIGHT")  return HRL_VOXEL_COLLISION_RIGHT;
			if (name == "TOP")    return HRL_VOXEL_COLLISION_TOP;
			if (name == "BOTTOM") return HRL_VOXEL_COLLISION_BOTTOM;
			if (name == "INSIDE") return HRL_VOXEL_COLLISION_INSIDE;
			return 0;
		}

		void WarnOnce(const std::string& key, const std::string& message)
		{
			if (g_warned.insert(key).second)
				std::fprintf(stderr, "[VOXELS] %s\n", message.c_str());
		}

		// Mask of a game flag, created on first use while loading.
		bool GameFlagMask(
			const std::string& name,
			std::vector<std::pair<std::string, uint32_t>>& flags,
			uint32_t& mask,
			std::string& error)
		{
			for (const auto& [flag_name, flag_mask] : flags)
			{
				if (flag_name == name)
				{
					mask = flag_mask;
					return true;
				}
			}

			// First free bit : not a collision flag, not another game flag.
			uint32_t used = kCollisionAll;

			for (const auto& f : flags)
				used |= f.second;

			int bit = kFirstGameFlagBit;

			while (bit < 32 && (used & (1u << bit)) != 0u)
				++bit;

			if (bit >= 32)
			{
				error = "too many flags (max " + std::to_string(32 - kFirstGameFlagBit) + ")";
				return false;
			}

			mask = 1u << bit;
			flags.emplace_back(name, mask);
			return true;
		}

		bool ParseType(
			const nlohmann::json& j,
			int index,
			std::vector<std::pair<std::string, uint32_t>>& flags,
			VoxelType& out,
			std::string& error)
		{
			const std::string where = "voxel #" + std::to_string(index + 1);

			if (!j.is_object())
			{
				error = where + " is not an object";
				return false;
			}

			out = VoxelType{};
			out.name = j.value("name", std::string("Voxel ") + std::to_string(index + 1));

			const std::string color = j.value("color", std::string("ffffff"));

			if (!HexToColor(color, out.color))
			{
				error = where + " (" + out.name + ") : bad color \"" + color + "\"";
				return false;
			}

			// Collision : "SOLID" (default), "NONE" or a list of sides.
			out.flags = kCollisionSolid;

			if (j.contains("collision"))
			{
				const auto& c = j["collision"];

				if (c.is_string())
				{
					const std::string value = c.get<std::string>();

					if (value == "SOLID")
						out.flags = kCollisionSolid;
					else if (value == "NONE")
						out.flags = 0;
					else if (const uint32_t one = CollisionFlag(value))
						out.flags = one;
					else
					{
						error = where + " (" + out.name + ") : unknown collision \"" + value + "\"";
						return false;
					}
				}
				else if (c.is_array())
				{
					out.flags = 0;

					for (const auto& side : c)
					{
						const std::string value = side.is_string() ? side.get<std::string>() : "";
						const uint32_t mask = CollisionFlag(value);

						if (!mask)
						{
							error = where + " (" + out.name + ") : unknown collision \"" + value + "\"";
							return false;
						}

						out.flags |= mask;
					}
				}
			}

			// Game flags.
			if (j.contains("flags") && j["flags"].is_array())
			{
				for (const auto& f : j["flags"])
				{
					if (!f.is_string() || f.get<std::string>().empty())
						continue;

					uint32_t mask = 0;

					if (!GameFlagMask(f.get<std::string>(), flags, mask, error))
					{
						error = where + " (" + out.name + ") : " + error;
						return false;
					}

					out.flags |= mask;
				}
			}

			// Emissive : a color, or true (= white).
			if (j.contains("emissive"))
			{
				const auto& e = j["emissive"];

				if (e.is_boolean())
				{
					out.emissive = e.get<bool>();
				}
				else if (e.is_string())
				{
					float rgba[4];

					if (!HexToColor(e.get<std::string>(), rgba))
					{
						error = where + " (" + out.name + ") : bad emissive color";
						return false;
					}

					out.emissive = true;
					out.emissive_color[0] = rgba[0];
					out.emissive_color[1] = rgba[1];
					out.emissive_color[2] = rgba[2];
				}
			}

			out.indestructible = j.value("indestructible", false);
			out.on_destroyed = j.value("on_destroyed", std::string());

			// Physics : a preset, then the values written (they win).
			if (j.contains("physics") && j["physics"].is_object())
			{
				const auto& p = j["physics"];
				VoxelPhysicsProps& ph = out.physics;
				const std::string preset = p.value("preset", std::string());
				for (const auto& [name, props] : GetPhysicsPresets())
					if (name == preset)
						ph = props;
				ph.preset = preset;

				const std::string behavior = p.value("behavior", std::string(BehaviorName(ph.behavior)));
				if (behavior == "powder") ph.behavior = VoxelBehavior::Powder;
				else if (behavior == "liquid") ph.behavior = VoxelBehavior::Liquid;
				else if (behavior == "gas") ph.behavior = VoxelBehavior::Gas;
				else ph.behavior = VoxelBehavior::Static;

				ph.density = p.value("density", ph.density);
				ph.dispersion = p.value("dispersion", ph.dispersion);
				ph.friction = p.value("friction", ph.friction);
				ph.bounciness = p.value("bounciness", ph.bounciness);
				ph.drag = p.value("drag", ph.drag);
				ph.buoyancy = p.value("buoyancy", ph.buoyancy);
				ph.damage = p.value("damage", ph.damage);
				ph.contact_events = p.value("contact_events", ph.contact_events);
			}

			return true;
		}
	}


	bool LoadFile(const char* asset_path)
	{
		const char* path = asset_path ? asset_path : "voxels.json";
		const std::vector<std::uint8_t> data = fs::ReadBinary(path);

		if (data.empty())
		{
			g_error = std::string("assets/") + path + " not found or empty";
			std::fprintf(stderr, "[VOXELS] %s\n", g_error.c_str());
			return false;
		}

		const nlohmann::json doc = nlohmann::json::parse(
			data.begin(), data.end(), nullptr, false, true /* // comments */);

		if (doc.is_discarded() || !doc.is_object() || !doc.contains("voxels") || !doc["voxels"].is_array())
		{
			g_error = std::string("assets/") + path + " : invalid JSON or no \"voxels\" list";
			std::fprintf(stderr, "[VOXELS] %s\n", g_error.c_str());
			return false;
		}

		std::vector<VoxelType> types;
		std::vector<std::pair<std::string, uint32_t>> flags;

		int index = 0;

		for (const auto& entry : doc["voxels"])
		{
			if (types.size() >= 255)
			{
				g_error = "more than 255 voxel types";
				std::fprintf(stderr, "[VOXELS] %s\n", g_error.c_str());
				return false;
			}

			VoxelType type;
			std::string error;

			if (!ParseType(entry, index, flags, type, error))
			{
				g_error = std::string("assets/") + path + " : " + error;
				std::fprintf(stderr, "[VOXELS] %s\n", g_error.c_str());
				return false;
			}

			types.push_back(std::move(type));
			++index;
		}

		g_types = std::move(types);
		g_flags = std::move(flags);
		g_error.clear();
		g_warned.clear();

		std::printf("[VOXELS] %d voxel types loaded from assets/%s\n",
		            static_cast<int>(g_types.size()), path);

		return true;
	}


	const std::string& GetLoadError()
	{
		return g_error;
	}


	void ApplyToScene(uint32_t scene)
	{
		for (size_t i = 0; i < g_types.size(); ++i)
		{
			const VoxelType& t = g_types[i];
			const int type = static_cast<int>(i) + 1;

			HRL_SetVoxelTypeColor(scene, type, t.color[0], t.color[1], t.color[2], t.color[3]);
			HRL_SetVoxelTypeCollisionFlags(scene, type, t.flags);

			// Not emissive : black (a type edited in the editor may have been emissive before).
			if (t.emissive)
			{
				HRL_SetVoxelTypeEmissiveColor(
					scene, type, t.emissive_color[0], t.emissive_color[1], t.emissive_color[2]);
			}
			else
			{
				HRL_SetVoxelTypeEmissiveColor(scene, type, 0.f, 0.f, 0.f);
			}
		}
	}


	int GetTypeCount()
	{
		return static_cast<int>(g_types.size());
	}


	const VoxelType* GetType(uint8_t type)
	{
		if (type == 0 || type > g_types.size())
			return nullptr;

		return &g_types[type - 1];
	}


	// -------------------------------------------------------------------------
	// Editing
	// -------------------------------------------------------------------------

	namespace
	{
		std::string ColorHex(const float* rgb)
		{
			char text[8];
			auto channel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
			std::snprintf(text, sizeof(text), "%02x%02x%02x", channel(rgb[0]), channel(rgb[1]), channel(rgb[2]));
			return text;
		}
	}

	uint32_t GetCollisionMask()
	{
		return kCollisionAll;
	}

	uint32_t GetSolidMask()
	{
		return kCollisionSolid;
	}

	std::vector<std::string> GetFlagNames()
	{
		std::vector<std::string> names;
		for (const auto& f : g_flags)
			names.push_back(f.first);
		return names;
	}

	uint32_t DeclareFlag(const char* name)
	{
		if (!name || !*name)
			return 0;
		if (const uint32_t collision = CollisionFlag(name))
			return collision;
		uint32_t mask = 0;
		std::string error;
		if (!GameFlagMask(name, g_flags, mask, error))
			return 0;
		return mask;
	}

	bool SetType(uint8_t type, const VoxelType& value)
	{
		if (type == 0 || type > g_types.size())
			return false;
		g_types[type - 1] = value;
		return true;
	}

	uint8_t AddType(const VoxelType& value)
	{
		if (g_types.size() >= 255)
			return 0;
		g_types.push_back(value);
		return static_cast<uint8_t>(g_types.size());
	}

	bool RemoveLastType()
	{
		if (g_types.empty())
			return false;
		g_types.pop_back();
		return true;
	}

	const char* BehaviorName(VoxelBehavior behavior)
	{
		switch (behavior)
		{
		case VoxelBehavior::Powder: return "powder";
		case VoxelBehavior::Liquid: return "liquid";
		case VoxelBehavior::Gas: return "gas";
		default: return "static";
		}
	}

	const std::vector<std::pair<std::string, VoxelPhysicsProps>>& GetPhysicsPresets()
	{
		static const std::vector<std::pair<std::string, VoxelPhysicsProps>> presets = []
		{
			using B = VoxelBehavior;
			auto make = [](const char* name, B b, float density, int dispersion, float friction, float bounciness,
			               float drag, float buoyancy, float damage, bool events)
			{
				VoxelPhysicsProps p;
				p.preset = name;
				p.behavior = b;
				p.density = density;
				p.dispersion = dispersion;
				p.friction = friction;
				p.bounciness = bounciness;
				p.drag = drag;
				p.buoyancy = buoyancy;
				p.damage = damage;
				p.contact_events = events;
				return std::make_pair(std::string(name), p);
			};
			return std::vector<std::pair<std::string, VoxelPhysicsProps>>{
				//            name       behavior    dens disp fric  bounce drag buoy dmg  events
				make("Stone",    B::Static, 2.5f, 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.f,  false),
				make("Ice",      B::Static, 0.9f, 0, 0.1f, 0.0f, 0.0f, 0.0f, 0.f,  false),
				make("Rubber",   B::Static, 1.2f, 0, 1.2f, 0.8f, 0.0f, 0.0f, 0.f,  false),
				make("Mud",      B::Static, 1.7f, 0, 2.5f, 0.0f, 0.0f, 0.0f, 0.f,  false),
				make("Spikes",   B::Static, 2.5f, 0, 1.0f, 0.0f, 0.0f, 0.0f, 20.f, true),
				make("Sand",     B::Powder, 1.6f, 1, 0.8f, 0.0f, 0.0f, 0.0f, 0.f,  false),
				make("Gravel",   B::Powder, 2.0f, 1, 1.0f, 0.0f, 0.0f, 0.0f, 0.f,  false),
				make("Snow",     B::Powder, 0.5f, 1, 0.4f, 0.0f, 0.0f, 0.0f, 0.f,  false),
				make("Water",    B::Liquid, 1.0f, 5, 1.0f, 0.0f, 0.5f, 1.1f, 0.f,  true),
				make("Oil",      B::Liquid, 0.8f, 3, 0.3f, 0.0f, 0.7f, 0.9f, 0.f,  true),
				make("Lava",     B::Liquid, 3.0f, 1, 1.0f, 0.0f, 0.9f, 0.6f, 40.f, true),
				make("Acid",     B::Liquid, 1.1f, 4, 1.0f, 0.0f, 0.5f, 1.0f, 15.f, true),
				make("Smoke",    B::Gas,    0.1f, 3, 1.0f, 0.0f, 0.1f, 0.0f, 0.f,  false),
				make("Steam",    B::Gas,    0.2f, 4, 1.0f, 0.0f, 0.1f, 0.0f, 2.f,  true),
				make("Poison gas", B::Gas,  0.3f, 2, 1.0f, 0.0f, 0.1f, 0.0f, 8.f,  true),
			};
		}();
		return presets;
	}

	std::string SaveToString()
	{
		nlohmann::ordered_json list = nlohmann::ordered_json::array();

		for (const VoxelType& t : g_types)
		{
			nlohmann::ordered_json j;
			j["name"] = t.name;
			j["color"] = ColorHex(t.color);

			const uint32_t collision = t.flags & kCollisionAll;
			if (collision == kCollisionSolid)
				j["collision"] = "SOLID";
			else if (collision == 0)
				j["collision"] = "NONE";
			else
			{
				nlohmann::ordered_json sides = nlohmann::ordered_json::array();
				for (const char* side : { "LEFT", "RIGHT", "TOP", "BOTTOM", "INSIDE" })
					if (collision & CollisionFlag(side))
						sides.push_back(side);
				j["collision"] = sides;
			}

			nlohmann::ordered_json flags = nlohmann::ordered_json::array();
			for (const auto& [name, mask] : g_flags)
				if (t.flags & mask)
					flags.push_back(name);
			if (!flags.empty())
				j["flags"] = flags;

			if (t.emissive)
			{
				const bool white = t.emissive_color[0] >= 1.f && t.emissive_color[1] >= 1.f && t.emissive_color[2] >= 1.f;
				if (white)
					j["emissive"] = true;
				else
					j["emissive"] = ColorHex(t.emissive_color);
			}
			if (t.indestructible)
				j["indestructible"] = true;
			if (!t.on_destroyed.empty())
				j["on_destroyed"] = t.on_destroyed;

			if (!t.physics.IsDefault())
			{
				const VoxelPhysicsProps& ph = t.physics;
				nlohmann::ordered_json p;
				if (!ph.preset.empty())
					p["preset"] = ph.preset;
				p["behavior"] = BehaviorName(ph.behavior);
				p["density"] = ph.density;
				p["dispersion"] = ph.dispersion;
				p["friction"] = ph.friction;
				p["bounciness"] = ph.bounciness;
				p["drag"] = ph.drag;
				p["buoyancy"] = ph.buoyancy;
				p["damage"] = ph.damage;
				p["contact_events"] = ph.contact_events;
				j["physics"] = p;
			}

			list.push_back(j);
		}

		nlohmann::ordered_json doc;
		doc["voxels"] = list;
		return doc.dump(4) + "\n";
	}


	uint32_t GetFlags(uint8_t type)
	{
		const VoxelType* t = GetType(type);
		return t ? t->flags : 0u;
	}


	bool IsIndestructible(uint8_t type)
	{
		const VoxelType* t = GetType(type);
		return t && t->indestructible;
	}


	uint32_t GetFlagMask(const char* name)
	{
		if (!name)
			return 0;

		const std::string key = name;

		if (const uint32_t collision = CollisionFlag(key))
			return collision;

		for (const auto& [flag_name, mask] : g_flags)
		{
			if (flag_name == key)
				return mask;
		}

		WarnOnce("flag:" + key, "unknown voxel flag \"" + key + "\" (not used in voxels.json)");
		return 0;
	}


	uint8_t GetTypeAt(int voxel_x, int voxel_y)
	{
		return static_cast<uint8_t>(HRL_GetVoxelType(Engine::GetScene(), voxel_x, voxel_y));
	}


	uint32_t GetFlagsAt(int voxel_x, int voxel_y)
	{
		return GetFlags(GetTypeAt(voxel_x, voxel_y));
	}


	uint32_t GetFlagsBelow(float world_x, float world_y)
	{
		float voxel_x = 0.f;
		float voxel_y = 0.f;

		if (HRL_WorldToVoxelCoordinates(Engine::GetScene(), world_x, world_y, &voxel_x, &voxel_y) != HRL_TRUE)
			return 0;

		return GetFlagsAt(
			static_cast<int>(std::floor(voxel_x)),
			static_cast<int>(std::floor(voxel_y)) - 1);
	}


	void RegisterDestroyedEvent(const char* name, DestroyedEventFn fn)
	{
		if (!name || !*name)
			return;

		if (fn)
			g_events[name] = fn;
		else
			g_events.erase(name);
	}


	void ClearDestroyedEvents()
	{
		g_events.clear();
	}


	void QueueDestroyedEvent(std::vector<VoxelEvent>& queue, uint8_t type, int voxel_x, int voxel_y)
	{
		const VoxelType* t = GetType(type);

		if (!t || t->on_destroyed.empty())
			return;

		const auto it = g_events.find(t->on_destroyed);

		if (it == g_events.end())
		{
			WarnOnce("event:" + t->on_destroyed,
			         "voxel event \"" + t->on_destroyed + "\" is not registered by the game");
			return;
		}

		queue.push_back({ it->second, voxel_x, voxel_y });
	}


	void FireDestroyedEvents(const std::vector<VoxelEvent>& queue)
	{
		for (const VoxelEvent& e : queue)
		{
			if (e.fn)
				e.fn(e.x, e.y);
		}
	}
}
