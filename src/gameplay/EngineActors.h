#pragma once

/**
 * Actors of the engine itself : always in Place Actors (no game code needed),
 * saved in the .level like the game's classes, spawnable from C++ / JS /
 * Python (Level.spawn("PointLightActor")), and usable as a base class
 * (C++ : class Torch : public lynx::PointLightActor ; JS : class Torch
 * extends PointLightActor).
 *
 *   Actor                  empty actor (scripts, components added in JS...)
 *   PointLightActor        light in every direction
 *   SpotLightActor         cone of light, turned with the actor rotation
 *   DirectionalLightActor  the sun : parallel rays, turned with the rotation
 *   SkyLightActor          soft ambient light
 *   SpriteActor            an image (decor) : texture, size, flip
 *   SoundActor             a sound (ambience, music...) played at Play
 *   ColliderActor          a collision box : wall / platform (blocks) or zone
 *                          (trigger) ; OnBeginOverlap / OnHit in C++ and JS
 *   Humanoid               a character that walks, jumps and falls (collider,
 *                          gravity, steps) ; no input : see Humanoid.h
 *
 * Their settings are properties (Details, the .level, getProperty /
 * setProperty). In the editor, an icon shows the actors that have no image
 * (lights, sounds, empty actors) so they can be clicked in the viewport ;
 * spot and directional lights also draw their direction. Icons and lines are
 * hidden while playing and in a shipped game.
 */

#include <cstdint>
#include <string>

#include "Actor.h"
#include "BoxColliderComponent.h"
#include "LightComponent.h"
#include "Light2DComponent.h"
#include "FogComponent.h"
#include "../core/Particles.h"
#include "SpriteComponents.h"

namespace lynx
{
	class FactoryObject;
	class SoundSourceComponent;

	/** Icons of the engine actors (one atlas embedded in lynx.dll). */
	enum class EngineActorIcon
	{
		Actor,
		PointLight,
		SpotLight,
		DirectionalLight,
		SkyLight,
		Sound,
		Sprite,
		Collider,
		Count
	};

	/**
	 * Icon drawn over an actor in the editor (a sprite that selects the actor
	 * when clicked). Hidden while playing. Usable by the game's actors too :
	 *     AddComponent<lynx::EditorIconComponent>(lynx::EngineActorIcon::Sound);
	 */
	class LYNX_API EditorIconComponent : public SpriteComponent
	{
	public:
		explicit EditorIconComponent(EngineActorIcon icon = EngineActorIcon::Actor);

		EngineActorIcon icon = EngineActorIcon::Actor;

		/** false : hidden even in the editor (ex : a SpriteActor with an image). */
		bool show = true;

	protected:
		void Update(float dt) override;
		void BeginPlay() override;
		void EndPlay() override;

	private:
		EngineActorIcon applied_icon_ = EngineActorIcon::Count;
		bool playing_ = false;
	};


	/** Base of the light actors : the common settings of a LightComponent. */
	class LYNX_API LightActor : public Actor
	{
	public:
		vec3 color{1.f, 1.f, 1.f};
		float intensity = 1.f;
		bool enabled = true;

		void Init() override;
		void Update(double dt) override;
		void StartGame() override;
		void EndGame() override;

		LightComponent* GetLight() const { return light_; }

	protected:
		LightActor(LightComponent::Type type, EngineActorIcon icon);

		/** Copies the properties into the component (every frame). */
		virtual void ApplyProperties(LightComponent& light);

		/** Editor lines (direction of spots and suns). */
		virtual void DrawEditorHelpers();

		bool playing_ = false;

	private:
		LightComponent::Type type_;
		EngineActorIcon icon_;
		LightComponent* light_ = nullptr;
	};

	/** Light in every direction from the actor. */
	class LYNX_API PointLightActor : public LightActor
	{
	public:
		PointLightActor();

		/** How fast the light fades with the distance (< 0 : renderer default). */
		float attenuation = -1.f;
		bool cast_shadows = false;
		float shadow_strength = 1.f;

	protected:
		void ApplyProperties(LightComponent& light) override;
	};

	/** Cone of light along the actor rotation (pitch, yaw : -90 yaw = into the level). */
	class LYNX_API SpotLightActor : public LightActor
	{
	public:
		SpotLightActor();

		float attenuation = -1.f;
		float inner_angle = 20.f;
		float outer_angle = 30.f;
		bool cast_shadows = false;
		float shadow_strength = 1.f;

	protected:
		void ApplyProperties(LightComponent& light) override;
		void DrawEditorHelpers() override;
	};

	/** The sun : parallel rays along the actor rotation (its position does not matter). */
	class LYNX_API DirectionalLightActor : public LightActor
	{
	public:
		DirectionalLightActor();

		bool cast_shadows = false;
		float shadow_strength = 1.f;

	protected:
		void ApplyProperties(LightComponent& light) override;
		void DrawEditorHelpers() override;
	};

	/** Soft light from every direction (ambient), no shadows. */
	class LYNX_API SkyLightActor : public LightActor
	{
	public:
		SkyLightActor();
	};

	/**
	 * 2D light (core/Lighting2D.h) : lights the level seen from the front, with
	 * shadows cast by the voxels. Enable the 2D lighting of the project first
	 * (window 2D Lighting). Radius in voxels ; cone_angle < 360 : flashlight
	 * along `direction` + the actor rotation (Z).
	 */
	class LYNX_API Light2DActor : public Actor
	{
	public:
		Light2DActor();

		vec3 color{1.f, 0.85f, 0.65f};
		float intensity = 1.5f;
		float radius = 20.f;
		float falloff = 2.f;
		bool enabled = true;
		bool cast_shadows = true;
		float shadow_strength = 1.f;
		float source_radius = 0.f;
		float cone_angle = 360.f;
		float cone_softness = 0.3f;
		float direction = 0.f;
		float flicker = 0.f;

		void Init() override;
		void Update(double dt) override;
		void StartGame() override;
		void EndGame() override;

		Light2DComponent* GetLight() const { return light_; }

	private:
		void ApplyProperties();
		void DrawEditorHelpers();

		Light2DComponent* light_ = nullptr;
		bool playing_ = false;
	};


	/**
	 * Distance fog of the whole scene (FogComponent). One at a time : the last
	 * enabled one wins. mode : "linear" (start -> end), "exponential", "exp2".
	 */
	class LYNX_API FogActor : public Actor
	{
	public:
		FogActor();

		bool enabled = true;
		std::string mode = "exponential";
		vec3 color{0.62f, 0.68f, 0.78f};
		float density = 0.015f;
		float start = 40.f;
		float end = 160.f;

		void Init() override;
		void Update(double dt) override;

		FogComponent* GetFog() const { return fog_; }

	private:
		FogComponent* fog_ = nullptr;
	};

	/**
	 * Volumetric fog (VolumetricFogComponent) : a ball of mist around the actor
	 * (radius, world units ; drawn as a circle in the editor), or the whole
	 * scene with `global` (one at a time).
	 */
	class LYNX_API VolumetricFogActor : public Actor
	{
	public:
		VolumetricFogActor();

		bool enabled = true;
		bool global = false;
		vec3 color{0.75f, 0.78f, 0.85f};
		float density = 0.3f;
		float radius = 16.f;
		int steps = 24;

		void Init() override;
		void Update(double dt) override;
		void StartGame() override;
		void EndGame() override;

		VolumetricFogComponent* GetFog() const { return fog_; }

	private:
		VolumetricFogComponent* fog_ = nullptr;
		bool playing_ = false;
	};

	/**
	 * A particle system (.vfx, Particle Editor) at the actor : it follows the
	 * actor (location, rotation, scale). Plays in the editor too. JS one-shot
	 * effects : Particles.spawn(path, position).
	 */
	class LYNX_API ParticleActor : public Actor
	{
	public:
		ParticleActor();
		~ParticleActor() override;

		/** Path in assets/ (.vfx). */
		std::string system;
		bool auto_play = true;
		float time_scale = 1.f;
		bool visible = true;

		void Init() override;
		void Update(double dt) override;
		void StartGame() override;

		/** The effect from the start (bursts again). */
		void Restart();
		/** No new particles (the living ones finish). */
		void StopEmitting();

	private:
		void Rebuild();

		particles::Instance* instance_ = nullptr;
		std::string built_path_;
		uint32_t built_version_ = 0;
		bool built_visible_ = true;
		bool stopped_ = false;
	};

	/** An image in the level (decor). */
	class LYNX_API SpriteActor : public Actor
	{
	public:
		SpriteActor();

		/** Path in assets/ (.png). */
		std::string texture;
		vec2 size{1.f, 1.f};
		bool flip_x = false;
		bool flip_y = false;
		bool visible = true;

		void Init() override;
		void Update(double dt) override;

		StaticSpriteComponent* GetSprite() const { return sprite_; }

	private:
		StaticSpriteComponent* sprite_ = nullptr;
		EditorIconComponent* icon_ = nullptr;
	};


	/** A sound at the actor location (or everywhere when not spatial). */
	class LYNX_API SoundActor : public Actor
	{
	public:
		SoundActor();

		/** Path in assets/ (.wav, .mp3, .flac). */
		std::string sound;
		float volume = 1.f;
		float pitch = 1.f;
		bool loop = true;
		bool spatial = true;
		bool play_on_begin = true;
		float max_distance = 100.f;   // voxels

		void Init() override;
		void Update(double dt) override;

		void Play();
		void Stop();

		SoundSourceComponent* GetSource() const { return source_; }

	private:
		SoundSourceComponent* source_ = nullptr;
	};


	/**
	 * A collision box placed in the level (ColliderComponent) :
	 *   - trigger = false : blocks the actors that have a blocking collider
	 *     (walls, platforms, invisible limits) ; they receive OnHit ;
	 *   - trigger = true  : a zone : OnBeginOverlap / OnEndOverlap (checkpoint,
	 *     damage zone, end of the level...).
	 * The box is drawn in the editor (green : trigger, red : blocking), and in
	 * the game with show_in_game. Its events reach its own class too :
	 *     class Checkpoint extends ColliderActor { OnBeginOverlap(other) { ... } }
	 *     class Lava : public lynx::ColliderActor { void OnBeginOverlap(Actor* other) override; };
	 */
	class LYNX_API ColliderActor : public Actor
	{
	public:
		ColliderActor();

		/** World size (before the scale of the actor). */
		vec2 size{4.f, 4.f};
		bool trigger = false;
		/** Pushed out of the blocking colliders (false : never moves by itself). */
		bool movable = false;
		bool generate_overlap_events = true;
		int layer = 1;
		int mask = -1;
		/** Draws the box while playing too. */
		bool show_in_game = false;

		void Init() override;
		void Update(double dt) override;
		void StartGame() override;
		void EndGame() override;

		ColliderComponent* GetCollider() const { return collider_; }

	private:
		ColliderComponent* collider_ = nullptr;
		bool playing_ = false;
	};


	/** Registers the classes above in the engine factory (Engine::Create). */
	LYNX_API void RegisterEngineActors(FactoryObject& factory);

	/** Class registered by the engine (not by the game DLL nor a .js file). */
	LYNX_API bool IsEngineActorClass(const std::string& class_name);

	/**
	 * Icon of an engine class (Place Actors). `uv` : u0, v0, u1, v1 in the
	 * texture (HRL textures are stored bottom-up : draw with v swapped).
	 * @return false for other classes, or before the renderer exists.
	 */
	LYNX_API bool GetEngineActorIcon(const std::string& class_name, uint32_t& texture, float uv[4]);
}
