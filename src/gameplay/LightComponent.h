#pragma once

/**
 * Lights attached to an actor. No HRL call needed : set the fields, the
 * component creates, updates and deletes the renderer light itself (the
 * changes are applied at the next frame, or at once with Refresh()).
 *
 *     auto& light = actor->AddComponent<lynx::PointLightComponent>();
 *     light.color = {1.f, 0.8f, 0.6f};
 *     light.intensity = 5.f;
 *
 *     auto& spot = actor->AddComponent<lynx::SpotLightComponent>();
 *     spot.rotation = {0.f, -90.f, 0.f};   // pitch, yaw, roll (degrees)
 *     spot.outer_angle = 35.f;
 *
 *     auto& sun = actor->AddComponent<lynx::DirectionalLightComponent>();
 *     auto& sky = actor->AddComponent<lynx::SkyLightComponent>();
 *
 * Types :
 *   Point       light from the actor (+ offset) in every direction
 *   Spot        cone from the actor (+ offset), along `rotation`
 *   Directional parallel rays along `rotation` (the sun), position ignored
 *   Sky         soft ambient light from every direction, no shadows
 *
 * Direction : `rotation` (pitch = X, yaw = Y, roll = Z, degrees, like the
 * actor transform and the camera). With `use_actor_rotation`, the actor's
 * rotation is added (the rotate gizmo turns the light). yaw -90 = towards -Z
 * (into the level, like the editor camera).
 *
 * The typed components (PointLightComponent...) are LightComponent with the
 * type set : GetComponent<lynx::SpotLightComponent>() finds a spot added as
 * SpotLightComponent, GetComponent<lynx::LightComponent>() one added as
 * LightComponent.
 */

#include <cstdint>

#include "Component.h"

namespace lynx
{
	class LYNX_API LightComponent : public Component
	{
	public:
		enum class Type
		{
			Point,
			Sky,
			Spot,
			Directional
		};

		LightComponent() = default;
		explicit LightComponent(Type light_type) : type(light_type) {}
		~LightComponent() override;

		Type type = Type::Point;

		vec3 color{1.f, 1.f, 1.f};
		float intensity = 1.f;

		/** false : intensity 0 (the light still exists). */
		bool enabled = true;

		// --- Point / spot ----------------------------------------------------

		/** From the actor location. */
		vec3 offset{0.f};

		/** How fast the light fades with the distance ; < 0 : renderer default. */
		float attenuation = -1.f;

		// --- Spot / directional ----------------------------------------------

		/** pitch, yaw, roll in degrees. */
		vec3 rotation{0.f, -90.f, 0.f};

		/** The actor rotation is added to `rotation`. */
		bool use_actor_rotation = true;

		/** Spot cone (degrees from the axis) : full light inside inner, none outside outer. */
		float inner_angle = 20.f;
		float outer_angle = 30.f;

		// --- Shadows (point, spot, directional) ------------------------------

		bool cast_shadows = false;

		/** 0 : no shadow, 1 : full shadow. */
		float shadow_strength = 1.f;

		/** < 0 : renderer default. */
		float shadow_bias = -1.f;

		/** Shadow map size (power of two) ; 0 : renderer default. */
		int shadow_resolution = 0;

		/** Applies the fields now (otherwise : at the next frame). */
		void Refresh();

		/** World direction of a spot / directional light (from the rotation). */
		vec3 GetDirection() const;

		/** World position of the light (actor location + offset). */
		vec3 GetWorldLocation() const;

		/** Renderer id (advanced use) ; 0xFFFFFFFF before the first frame. */
		uint32_t GetLightId() const { return light_; }

	protected:
		void OnAttach() override;
		void Update(float dt) override;

	private:
		void Sync(bool force);

		uint32_t light_ = 0xFFFFFFFFu;
		Type created_type_ = Type::Point;

		// Last values sent to the renderer (no call when nothing changed).
		float last_[24] = {};
		bool has_last_ = false;
	};

	struct LYNX_API PointLightComponent : LightComponent
	{
		PointLightComponent() : LightComponent(Type::Point) {}
	};

	struct LYNX_API SpotLightComponent : LightComponent
	{
		SpotLightComponent() : LightComponent(Type::Spot) {}
	};

	struct LYNX_API DirectionalLightComponent : LightComponent
	{
		DirectionalLightComponent() : LightComponent(Type::Directional)
		{
			rotation = {-45.f, -60.f, 0.f};
		}
	};

	struct LYNX_API SkyLightComponent : LightComponent
	{
		SkyLightComponent() : LightComponent(Type::Sky)
		{
			intensity = 0.4f;
		}
	};
}
