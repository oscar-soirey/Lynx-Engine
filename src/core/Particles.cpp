#include "Particles.h"

#include "Engine.h"
#include "Filesystem.h"

#include <hrl/hrl.h>
#include <json/json.hpp>

#include <algorithm>
#include <iostream>
#include <unordered_map>

namespace lynx::particles
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr uint32_t kInvalid = 0xFFFFFFFFu;

		// ---------------------------------------------------------------------
		// JSON helpers
		// ---------------------------------------------------------------------

		Json Vec(const vec2& v) { return Json::array({ v.x, v.y }); }
		Json Vec(const vec3& v) { return Json::array({ v.x, v.y, v.z }); }
		Json Vec(const vec4& v) { return Json::array({ v.x, v.y, v.z, v.w }); }
		Json Rng(const Range& r) { return Json::array({ r.min, r.max }); }

		float F(const Json& j, const char* key, float fallback)
		{
			const auto it = j.find(key);
			return it != j.end() && it->is_number() ? it->get<float>() : fallback;
		}

		int I(const Json& j, const char* key, int fallback)
		{
			const auto it = j.find(key);
			return it != j.end() && it->is_number() ? it->get<int>() : fallback;
		}

		bool B(const Json& j, const char* key, bool fallback)
		{
			const auto it = j.find(key);
			return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
		}

		std::string S(const Json& j, const char* key, const std::string& fallback)
		{
			const auto it = j.find(key);
			return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
		}

		void ReadFloats(const Json& j, const char* key, float* out, int count)
		{
			const auto it = j.find(key);
			if (it == j.end() || !it->is_array())
				return;
			for (int i = 0; i < count && i < static_cast<int>(it->size()); ++i)
				if ((*it)[i].is_number())
					out[i] = (*it)[i].get<float>();
		}

		void Read(const Json& j, const char* key, vec2& v) { ReadFloats(j, key, &v.x, 2); }
		void Read(const Json& j, const char* key, vec3& v) { ReadFloats(j, key, &v.x, 3); }
		void Read(const Json& j, const char* key, Range& r) { ReadFloats(j, key, &r.min, 2); }

		template <class E>
		const char* EnumName(E value, const std::vector<std::pair<E, const char*>>& names)
		{
			for (const auto& [e, n] : names)
				if (e == value)
					return n;
			return names.front().second;
		}

		template <class E>
		E EnumValue(const std::string& text, const std::vector<std::pair<E, const char*>>& names, E fallback)
		{
			for (const auto& [e, n] : names)
				if (text == n)
					return e;
			return fallback;
		}

		const std::vector<std::pair<Shape, const char*>> kShapes = {
			{ Shape::Point, "point" }, { Shape::Sphere, "sphere" }, { Shape::Box, "box" },
			{ Shape::Cylinder, "cylinder" }, { Shape::Cone, "cone" },
		};
		const std::vector<std::pair<Blend, const char*>> kBlends = {
			{ Blend::Alpha, "alpha" }, { Blend::Additive, "additive" }, { Blend::Multiply, "multiply" },
		};
		const std::vector<std::pair<Render, const char*>> kRenders = {
			{ Render::Billboard, "billboard" }, { Render::Stretched, "stretched" },
		};
		const std::vector<std::pair<Space, const char*>> kSpaces = {
			{ Space::World, "world" }, { Space::Local, "local" },
		};

		Json EmitterToJson(const EmitterDesc& e)
		{
			Json bursts = Json::array();
			for (const Burst& b : e.bursts)
				bursts.push_back({ {"time", b.time}, {"count", b.count} });
			Json color = Json::array();
			for (const ColorKey& k : e.color)
				color.push_back({ {"time", k.time}, {"color", Vec(k.color)} });
			Json size = Json::array();
			for (const FloatKey& k : e.size_curve)
				size.push_back({ {"time", k.time}, {"value", k.value} });
			Json rot = Json::array();
			for (const FloatKey& k : e.rotation_curve)
				rot.push_back({ {"time", k.time}, {"value", k.value} });

			return {
				{"name", e.name}, {"enabled", e.enabled},
				{"required", { {"texture", e.texture}, {"blend", EnumName(e.blend, kBlends)},
				               {"render", EnumName(e.render, kRenders)}, {"space", EnumName(e.space, kSpaces)},
				               {"size", Vec(e.size)}, {"stretch", e.stretch}, {"max_particles", e.max_particles} }},
				{"spawn", { {"rate", e.spawn_rate}, {"bursts", bursts} }},
				{"lifetime", Rng(e.lifetime)},
				{"location", { {"shape", EnumName(e.shape, kShapes)}, {"radius", e.shape_radius},
				               {"size", Vec(e.shape_size)}, {"angle", e.shape_angle}, {"offset", Vec(e.offset)},
				               {"rotation", Vec(e.shape_rotation)} }},
				{"velocity", { {"min", Vec(e.velocity_min)}, {"max", Vec(e.velocity_max)}, {"speed", Rng(e.speed)} }},
				{"rotation", { {"enabled", e.rotation_module}, {"min", Vec(e.rotation_min)}, {"max", Vec(e.rotation_max)},
				               {"angular_min", Vec(e.angular_min)}, {"angular_max", Vec(e.angular_max)} }},
				{"forces", { {"enabled", e.forces_module}, {"gravity", Vec(e.gravity)}, {"drag", e.drag},
				             {"force", Vec(e.force)}, {"noise_strength", e.noise_strength},
				             {"noise_frequency", e.noise_frequency}, {"noise_scroll", e.noise_scroll} }},
				{"color_over_life", { {"enabled", e.color_module}, {"keys", color} }},
				{"size_over_life", { {"enabled", e.size_module}, {"keys", size} }},
				{"rotation_over_life", { {"enabled", e.rotation_curve_module}, {"keys", rot} }},
				{"collision", { {"enabled", e.collision}, {"restitution", e.restitution}, {"friction", e.friction} }},
			};
		}

		EmitterDesc EmitterFromJson(const Json& j)
		{
			EmitterDesc e;
			e.name = S(j, "name", e.name);
			e.enabled = B(j, "enabled", e.enabled);

			const Json req = j.value("required", Json::object());
			e.texture = S(req, "texture", e.texture);
			e.blend = EnumValue(S(req, "blend", ""), kBlends, e.blend);
			e.render = EnumValue(S(req, "render", ""), kRenders, e.render);
			e.space = EnumValue(S(req, "space", ""), kSpaces, e.space);
			Read(req, "size", e.size);
			e.stretch = F(req, "stretch", e.stretch);
			e.max_particles = I(req, "max_particles", e.max_particles);

			const Json spawn = j.value("spawn", Json::object());
			e.spawn_rate = F(spawn, "rate", e.spawn_rate);
			e.bursts.clear();
			for (const Json& b : spawn.value("bursts", Json::array()))
				e.bursts.push_back({ F(b, "time", 0.f), I(b, "count", 10) });

			Read(j, "lifetime", e.lifetime);

			const Json loc = j.value("location", Json::object());
			e.shape = EnumValue(S(loc, "shape", ""), kShapes, e.shape);
			e.shape_radius = F(loc, "radius", e.shape_radius);
			Read(loc, "size", e.shape_size);
			e.shape_angle = F(loc, "angle", e.shape_angle);
			Read(loc, "offset", e.offset);
			Read(loc, "rotation", e.shape_rotation);

			const Json vel = j.value("velocity", Json::object());
			Read(vel, "min", e.velocity_min);
			Read(vel, "max", e.velocity_max);
			Read(vel, "speed", e.speed);

			const Json rot = j.value("rotation", Json::object());
			e.rotation_module = B(rot, "enabled", e.rotation_module);
			Read(rot, "min", e.rotation_min);
			Read(rot, "max", e.rotation_max);
			Read(rot, "angular_min", e.angular_min);
			Read(rot, "angular_max", e.angular_max);

			const Json forces = j.value("forces", Json::object());
			e.forces_module = B(forces, "enabled", e.forces_module);
			Read(forces, "gravity", e.gravity);
			e.drag = F(forces, "drag", e.drag);
			Read(forces, "force", e.force);
			e.noise_strength = F(forces, "noise_strength", e.noise_strength);
			e.noise_frequency = F(forces, "noise_frequency", e.noise_frequency);
			e.noise_scroll = F(forces, "noise_scroll", e.noise_scroll);

			const Json col = j.value("color_over_life", Json::object());
			e.color_module = B(col, "enabled", e.color_module);
			if (col.contains("keys") && col["keys"].is_array())
			{
				e.color.clear();
				for (const Json& k : col["keys"])
				{
					ColorKey key;
					key.time = F(k, "time", 0.f);
					ReadFloats(k, "color", &key.color.x, 4);
					e.color.push_back(key);
				}
			}

			auto read_curve = [](const Json& section, std::vector<FloatKey>& out)
			{
				if (!section.contains("keys") || !section["keys"].is_array())
					return;
				out.clear();
				for (const Json& k : section["keys"])
					out.push_back({ F(k, "time", 0.f), F(k, "value", 1.f) });
			};
			const Json size = j.value("size_over_life", Json::object());
			e.size_module = B(size, "enabled", e.size_module);
			read_curve(size, e.size_curve);
			const Json rc = j.value("rotation_over_life", Json::object());
			e.rotation_curve_module = B(rc, "enabled", e.rotation_curve_module);
			read_curve(rc, e.rotation_curve);

			const Json coll = j.value("collision", Json::object());
			e.collision = B(coll, "enabled", e.collision);
			e.restitution = F(coll, "restitution", e.restitution);
			e.friction = F(coll, "friction", e.friction);
			return e;
		}

		// ---------------------------------------------------------------------
		// Renderer
		// ---------------------------------------------------------------------

		std::unordered_map<std::string, uint32_t> g_textures;

		uint32_t Texture(const std::string& path)
		{
			if (path.empty())
				return kInvalid;
			const auto it = g_textures.find(path);
			if (it != g_textures.end() && HRL_IsValidTexture(it->second))
				return it->second;
			const std::vector<std::uint8_t> data = fs::ReadBinary(path);
			uint32_t id = kInvalid;
			if (!data.empty())
				id = HRL_CreateTexture(reinterpret_cast<const char*>(data.data()), data.size());
			if (id == kInvalid)
				std::cout << "[PARTICLES] texture not found : " << path << "\n";
			g_textures[path] = id;
			return id;
		}

		HRL_EVFXSpawnShape ToHRL(Shape s)
		{
			switch (s)
			{
				case Shape::Point:    return HRL_VFX_SHAPE_POINT;
				case Shape::Box:      return HRL_VFX_SHAPE_BOX;
				case Shape::Cylinder: return HRL_VFX_SHAPE_CYLINDER;
				case Shape::Cone:     return HRL_VFX_SHAPE_CONE;
				default:              return HRL_VFX_SHAPE_SPHERE;
			}
		}

		HRL_EVFXBlendMode ToHRL(Blend b)
		{
			switch (b)
			{
				case Blend::Alpha:    return HRL_VFX_BLEND_ALPHA;
				case Blend::Multiply: return HRL_VFX_BLEND_MULTIPLY;
				default:              return HRL_VFX_BLEND_ADDITIVE;
			}
		}

		void BuildEmitter(uint32_t system, uint32_t scene, const EmitterDesc& e)
		{
			const uint32_t em = HRL_CreateVFXEmitter(system);
			if (em == kInvalid)
				return;

			HRL_SetVFXEmitterEnabled(em, e.enabled ? HRL_TRUE : HRL_FALSE);
			HRL_SetVFXEmitterMaxParticles(em, static_cast<HRL_uint>(std::clamp(e.max_particles, 1, 100000)));
			HRL_SetVFXEmitterRenderMode(em, e.render == Render::Stretched ? HRL_VFX_RENDER_STRETCHED_BILLBOARD
			                                                             : HRL_VFX_RENDER_BILLBOARD);
			HRL_SetVFXEmitterBlendMode(em, ToHRL(e.blend));
			HRL_SetVFXEmitterSimulationSpace(em, e.space == Space::Local ? HRL_VFX_SIMULATION_LOCAL
			                                                             : HRL_VFX_SIMULATION_WORLD);
			HRL_SetVFXEmitterParticleSize(em, e.size.x, e.size.y);
			HRL_SetVFXEmitterStretch(em, e.stretch);
			const uint32_t texture = Texture(e.texture);
			if (texture != kInvalid)
				HRL_SetVFXEmitterTexture(em, texture);

			HRL_SetVFXEmitterSpawnRate(em, std::max(0.f, e.spawn_rate));
			HRL_ClearVFXBursts(em);
			for (const Burst& b : e.bursts)
				HRL_AddVFXBurst(em, std::max(0.f, b.time), static_cast<HRL_uint>(std::max(0, b.count)));

			HRL_SetVFXEmitterLifetime(em, std::max(0.01f, e.lifetime.min), std::max(e.lifetime.min, e.lifetime.max));

			HRL_SetVFXEmitterSpawnShape(em, ToHRL(e.shape));
			HRL_SetVFXEmitterShapeRadius(em, std::max(0.f, e.shape_radius));
			HRL_SetVFXEmitterShapeSize(em, e.shape_size.x, e.shape_size.y, e.shape_size.z);
			HRL_SetVFXEmitterShapeAngle(em, e.shape_angle);
			HRL_SetVFXEmitterPosition(em, e.offset.x, e.offset.y, e.offset.z);
			HRL_SetVFXEmitterRotation(em, e.shape_rotation.x, e.shape_rotation.y, e.shape_rotation.z);

			HRL_SetVFXEmitterInitialVelocity(em, e.velocity_min.x, e.velocity_min.y, e.velocity_min.z,
			                                 e.velocity_max.x, e.velocity_max.y, e.velocity_max.z);
			HRL_SetVFXEmitterInitialSpeed(em, e.speed.min, std::max(e.speed.min, e.speed.max));

			if (e.rotation_module)
			{
				HRL_SetVFXEmitterInitialRotation(em, e.rotation_min.x, e.rotation_min.y, e.rotation_min.z,
				                                 e.rotation_max.x, e.rotation_max.y, e.rotation_max.z);
				HRL_SetVFXEmitterAngularVelocity(em, e.angular_min.x, e.angular_min.y, e.angular_min.z,
				                                 e.angular_max.x, e.angular_max.y, e.angular_max.z);
			}

			if (e.forces_module)
			{
				HRL_SetVFXGravity(em, e.gravity.x, e.gravity.y, e.gravity.z);
				HRL_SetVFXDrag(em, std::max(0.f, e.drag));
				HRL_SetVFXForce(em, e.force.x, e.force.y, e.force.z);
				HRL_SetVFXNoise(em, std::max(0.f, e.noise_strength), e.noise_frequency, e.noise_scroll);
			}
			else
			{
				HRL_SetVFXGravity(em, 0.f, 0.f, 0.f);
				HRL_SetVFXDrag(em, 0.f);
			}

			if (e.color_module && !e.color.empty())
			{
				const uint32_t curve = HRL_CreateVFXColorCurve(em);
				if (curve != kInvalid)
				{
					std::vector<ColorKey> keys = e.color;
					std::sort(keys.begin(), keys.end(), [](const ColorKey& a, const ColorKey& b) { return a.time < b.time; });
					for (const ColorKey& k : keys)
						HRL_AddVFXColorKey(curve, std::clamp(k.time, 0.f, 1.f), k.color.x, k.color.y, k.color.z, k.color.w);
					HRL_SetVFXEmitterColorCurve(em, curve);
				}
			}

			auto float_curve = [em](const std::vector<FloatKey>& source, void (*set)(HRL_id, HRL_id))
			{
				if (source.empty())
					return;
				const uint32_t curve = HRL_CreateVFXFloatCurve(em);
				if (curve == kInvalid)
					return;
				std::vector<FloatKey> keys = source;
				std::sort(keys.begin(), keys.end(), [](const FloatKey& a, const FloatKey& b) { return a.time < b.time; });
				for (const FloatKey& k : keys)
					HRL_AddVFXFloatKey(curve, std::clamp(k.time, 0.f, 1.f), k.value);
				set(em, curve);
			};
			if (e.size_module)
				float_curve(e.size_curve, &HRL_SetVFXEmitterSizeCurve);
			if (e.rotation_curve_module)
				float_curve(e.rotation_curve, &HRL_SetVFXEmitterRotationCurve);

			HRL_SetVFXCollisionEnabled(em, e.collision ? HRL_TRUE : HRL_FALSE);
			if (e.collision)
			{
				HRL_SetVFXCollisionRestitution(em, e.restitution);
				HRL_SetVFXCollisionFriction(em, e.friction);
				HRL_SetVFXCollisionScene(em, scene, HRL_TRUE);
			}
		}

		// ---------------------------------------------------------------------
		// Assets and one-shot effects
		// ---------------------------------------------------------------------

		struct CachedAsset
		{
			SystemDesc desc;
			bool ok = false;
		};
		std::unordered_map<std::string, CachedAsset> g_cache;
		uint32_t g_version = 1;

		struct Spawned
		{
			uint32_t system = kInvalid;
			float remaining = 0.f;
		};
		std::vector<Spawned> g_spawned;
	}

	// =========================================================================

	bool FromJson(const std::string& text, SystemDesc& out, std::string& error)
	{
		const Json root = Json::parse(text, nullptr, false, true);
		if (!root.is_object())
		{
			error = "invalid JSON";
			return false;
		}
		SystemDesc desc;
		desc.duration = F(root, "duration", desc.duration);
		desc.looping = B(root, "looping", desc.looping);
		for (const Json& e : root.value("emitters", Json::array()))
			if (e.is_object())
				desc.emitters.push_back(EmitterFromJson(e));
		out = std::move(desc);
		return true;
	}

	std::string ToJson(const SystemDesc& desc)
	{
		Json emitters = Json::array();
		for (const EmitterDesc& e : desc.emitters)
			emitters.push_back(EmitterToJson(e));
		const Json root = { {"duration", desc.duration}, {"looping", desc.looping}, {"emitters", emitters} };
		return root.dump(2) + "\n";
	}

	std::string NewTemplate()
	{
		SystemDesc desc;
		EmitterDesc flames;
		flames.name = "Flames";
		desc.emitters.push_back(flames);

		EmitterDesc sparks;
		sparks.name = "Sparks";
		sparks.size = vec2(0.15f, 0.15f);
		sparks.spawn_rate = 8.f;
		sparks.render = Render::Stretched;
		sparks.stretch = 0.08f;
		sparks.velocity_min = vec3(-2.f, 4.f, 0.f);
		sparks.velocity_max = vec3(2.f, 8.f, 0.f);
		sparks.gravity = vec3(0.f, -6.f, 0.f);
		sparks.color = { { 0.f, {1.f, 0.9f, 0.5f, 1.f} }, { 1.f, {1.f, 0.4f, 0.1f, 0.f} } };
		sparks.size_module = false;
		desc.emitters.push_back(sparks);
		return ToJson(desc);
	}

	bool LoadAsset(const std::string& path, SystemDesc& out)
	{
		const auto it = g_cache.find(path);
		if (it != g_cache.end())
		{
			out = it->second.desc;
			return it->second.ok;
		}
		CachedAsset cached;
		const std::vector<std::uint8_t> data = fs::ReadBinary(path);
		std::string error;
		if (data.empty())
			std::cout << "[PARTICLES] " << path << " : not found\n";
		else if (!FromJson(std::string(data.begin(), data.end()), cached.desc, error))
			std::cout << "[PARTICLES] " << path << " : " << error << "\n";
		else
			cached.ok = true;
		out = cached.desc;
		const bool ok = cached.ok;
		g_cache[path] = std::move(cached);
		return ok;
	}

	void NotifyAssetChanged(const std::string& path)
	{
		g_cache.erase(path);
		++g_version;
	}

	uint32_t GetAssetVersion()
	{
		return g_version;
	}

	// ---- Instance -------------------------------------------------------------

	Instance::~Instance()
	{
		Destroy();
	}

	bool Instance::Create(const SystemDesc& desc, uint32_t scene)
	{
		Destroy();
		if (scene == kInvalid)
			scene = Engine::GetScene();
		if (scene == kInvalid || !HRL_IsValidScene(scene))
			return false;
		system_ = HRL_CreateVFXSystem(scene);
		if (system_ == kInvalid)
			return false;
		HRL_SetVFXSystemDuration(system_, std::max(0.01f, desc.duration));
		HRL_SetVFXSystemLooping(system_, desc.looping ? HRL_TRUE : HRL_FALSE);
		for (const EmitterDesc& e : desc.emitters)
			BuildEmitter(system_, scene, e);
		HRL_PlayVFXSystem(system_);
		return true;
	}

	void Instance::Destroy()
	{
		if (system_ != kInvalid && HRL_IsValidVFXSystem(system_))
			HRL_DeleteVFXSystem(system_);
		system_ = kInvalid;
	}

	bool Instance::IsValid() const
	{
		return system_ != kInvalid && HRL_IsValidVFXSystem(system_);
	}

	void Instance::SetTransform(const vec3& location, const vec3& rotation, const vec3& scale)
	{
		if (!IsValid())
			return;
		HRL_SetVFXSystemPosition(system_, location.x, location.y, location.z);
		HRL_SetVFXSystemRotation(system_, rotation.x, rotation.y, rotation.z);
		HRL_SetVFXSystemScale(system_, scale.x, scale.y, scale.z);
	}

	uint32_t Instance::Release()
	{
		const uint32_t id = system_;
		system_ = kInvalid;
		return id;
	}

	void Instance::Play()    {if (IsValid()) HRL_PlayVFXSystem(system_); }
	void Instance::Stop()    { if (IsValid()) HRL_StopVFXSystem(system_); }
	void Instance::Pause()   { if (IsValid()) HRL_PauseVFXSystem(system_); }
	void Instance::Restart()
	{
		if (!IsValid())
			return;
		HRL_ResetVFXSystem(system_);
		HRL_PlayVFXSystem(system_);
	}
	void Instance::SetTimeScale(float scale) { if (IsValid()) HRL_SetVFXSystemTimeScale(system_, std::max(0.f, scale)); }
	void Instance::SetLooping(bool looping) { if (IsValid()) HRL_SetVFXSystemLooping(system_, looping ? HRL_TRUE : HRL_FALSE); }

	// ---- One-shot ---------------------------------------------------------------

	bool Spawn(const std::string& path, const vec3& location, const vec3& rotation, float scale)
	{
		SystemDesc desc;
		if (!LoadAsset(path, desc))
			return false;
		desc.looping = false;

		Instance instance;
		if (!instance.Create(desc))
			return false;
		instance.SetTransform(location, rotation, vec3(scale));

		float longest = 0.f;
		for (const EmitterDesc& e : desc.emitters)
			longest = std::max(longest, e.lifetime.max);

		Spawned s;
		s.system = instance.GetId();
		s.remaining = desc.duration + longest + 0.25f;
		g_spawned.push_back(s);

		instance.Release();   // Tick deletes it at the end
		return true;
	}

	void Tick(float dt)
	{
		for (Spawned& s : g_spawned)
			s.remaining -= dt;
		g_spawned.erase(std::remove_if(g_spawned.begin(), g_spawned.end(), [](const Spawned& s)
		{
			if (s.remaining > 0.f && HRL_IsValidVFXSystem(s.system))
				return false;
			if (HRL_IsValidVFXSystem(s.system))
				HRL_DeleteVFXSystem(s.system);
			return true;
		}), g_spawned.end());
	}

	void ClearSpawned()
	{
		for (const Spawned& s : g_spawned)
			if (HRL_IsValidVFXSystem(s.system))
				HRL_DeleteVFXSystem(s.system);
		g_spawned.clear();
	}

	void Shutdown()
	{
		g_spawned.clear();
		g_textures.clear();
	}
}
