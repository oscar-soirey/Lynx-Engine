#pragma once

/**
 * Particle systems (.vfx assets), drawn by the VFX of the renderer. Made in the
 * Particle Editor (Content Browser > New file > Particle System, double-click).
 *
 * A system has emitters (Unreal Cascade-like) ; each emitter has modules :
 *   Required            texture, blend (alpha / additive / multiply), render
 *                       (billboard / stretched), simulation space, size, max particles
 *   Spawn               particles per second, bursts (time, count)
 *   Lifetime            min / max seconds
 *   Initial Location    shape (point, sphere, box, cylinder, cone) + offset
 *   Initial Velocity    min / max vector, speed along the shape
 *   Initial Rotation    min / max, angular velocity
 *   Forces              gravity, drag, constant force, noise (turbulence)
 *   Color Over Life     gradient (RGBA keys)
 *   Size Over Life      curve (multiplies the size)
 *   Rotation Over Life  curve
 *   Collision           with the scene (restitution, friction)
 *
 * In the game : the engine actor ParticleActor (property `system` : the .vfx),
 * or a one-shot effect :
 *     lynx::particles::Spawn("fx/explosion.vfx", location);    // JS : Particles.spawn(path, position)
 *
 * The renderer is never visible here : ids are plain integers.
 */

#include <cstdint>
#include <string>
#include <vector>

#include "Common.h"

namespace lynx::particles
{
	enum class Shape { Point, Sphere, Box, Cylinder, Cone };
	enum class Blend { Alpha, Additive, Multiply };
	enum class Render { Billboard, Stretched };
	enum class Space { World, Local };

	struct Range
	{
		float min = 0.f;
		float max = 0.f;
	};

	struct Burst
	{
		float time = 0.f;   // seconds from the start of the system
		int count = 10;
	};

	struct ColorKey
	{
		float time = 0.f;   // 0..1 of the particle life
		vec4 color{1.f, 1.f, 1.f, 1.f};
	};

	struct FloatKey
	{
		float time = 0.f;   // 0..1 of the particle life
		float value = 1.f;
	};

	struct EmitterDesc
	{
		std::string name = "Emitter";
		bool enabled = true;

		// Required
		std::string texture;                 // asset path ("" : plain square)
		Blend blend = Blend::Additive;
		Render render = Render::Billboard;
		Space space = Space::World;
		vec2 size{0.6f, 0.6f};
		float stretch = 1.f;                 // Stretched : length per speed
		int max_particles = 500;

		// Spawn
		float spawn_rate = 30.f;             // per second
		std::vector<Burst> bursts;

		// Lifetime
		Range lifetime{0.8f, 1.6f};

		// Initial Location
		Shape shape = Shape::Sphere;
		float shape_radius = 0.5f;
		vec3 shape_size{1.f, 1.f, 1.f};      // Box
		float shape_angle = 25.f;            // Cone (degrees)
		vec3 offset{0.f};
		vec3 shape_rotation{0.f};            // pitch, yaw, roll

		// Initial Velocity
		vec3 velocity_min{-1.f, 2.f, 0.f};
		vec3 velocity_max{1.f, 4.f, 0.f};
		Range speed{0.f, 0.f};               // along the shape (outwards / cone axis)

		// Initial Rotation
		bool rotation_module = false;
		vec3 rotation_min{0.f};
		vec3 rotation_max{0.f, 0.f, 360.f};
		vec3 angular_min{0.f};
		vec3 angular_max{0.f};

		// Forces
		bool forces_module = true;
		vec3 gravity{0.f, -3.f, 0.f};
		float drag = 0.f;
		vec3 force{0.f};
		float noise_strength = 0.f;
		float noise_frequency = 1.f;
		float noise_scroll = 0.5f;

		// Color Over Life
		bool color_module = true;
		std::vector<ColorKey> color{ { 0.f, {1.f, 0.85f, 0.4f, 1.f} }, { 1.f, {1.f, 0.25f, 0.05f, 0.f} } };

		// Size Over Life
		bool size_module = true;
		std::vector<FloatKey> size_curve{ { 0.f, 1.f }, { 1.f, 0.3f } };

		// Rotation Over Life
		bool rotation_curve_module = false;
		std::vector<FloatKey> rotation_curve{ { 0.f, 0.f }, { 1.f, 180.f } };

		// Collision
		bool collision = false;
		float restitution = 0.3f;
		float friction = 0.2f;
	};

	struct SystemDesc
	{
		float duration = 2.f;     // seconds of one cycle (bursts, one-shot end)
		bool looping = true;
		std::vector<EmitterDesc> emitters;
	};

	// -------------------------------------------------------------------------
	// Assets
	// -------------------------------------------------------------------------

	LYNX_API bool FromJson(const std::string& text, SystemDesc& out, std::string& error);
	LYNX_API std::string ToJson(const SystemDesc& desc);

	/** Content of a new .vfx (a small fire). */
	LYNX_API std::string NewTemplate();

	/** Reads assets/<path> (cached). false : missing / invalid (error printed). */
	LYNX_API bool LoadAsset(const std::string& path, SystemDesc& out);

	/** The asset changed (editor Save) : the cache and the actors that use it reload. */
	LYNX_API void NotifyAssetChanged(const std::string& path);

	/** Incremented by NotifyAssetChanged (actors compare it to rebuild). */
	LYNX_API uint32_t GetAssetVersion();

	// -------------------------------------------------------------------------
	// A system in a scene
	// -------------------------------------------------------------------------

	class LYNX_API Instance
	{
	public:
		Instance() = default;
		~Instance();
		Instance(const Instance&) = delete;
		Instance& operator=(const Instance&) = delete;

		/** Builds the system in `scene` (0xFFFFFFFF : the scene of the engine). Replaces the previous one. */
		bool Create(const SystemDesc& desc, uint32_t scene = 0xFFFFFFFFu);
		void Destroy();
		bool IsValid() const;

		void SetTransform(const vec3& location, const vec3& rotation = vec3(0.f), const vec3& scale = vec3(1.f));
		void Play();
		void Stop();      // no new particles ; the living ones finish
		void Pause();
		void Restart();   // from the start (bursts again)
		void SetTimeScale(float scale);
		void SetLooping(bool looping);

		/** Renderer id (advanced use) ; 0xFFFFFFFF : none. */
		uint32_t GetId() const { return system_; }

		/** Gives up the system (not deleted with the instance) ; returns its id. */
		uint32_t Release();

	private:
		uint32_t system_ = 0xFFFFFFFFu;
	};

	// -------------------------------------------------------------------------
	// One-shot effects
	// -------------------------------------------------------------------------

	/** Plays the asset once at a position (not looping), then deletes it. false : asset not found. */
	LYNX_API bool Spawn(const std::string& path, const vec3& location, const vec3& rotation = vec3(0.f),
	                    float scale = 1.f);

	/** Every frame (Engine::ProgressOneFrame) : removes the finished one-shot effects. */
	LYNX_API void Tick(float dt);

	/** Removes every one-shot effect (end of the game). */
	LYNX_API void ClearSpawned();

	/** HRL_Shutdown : forgets textures and effects (their ids die with the renderer). */
	LYNX_API void Shutdown();
}
