#pragma once

/**
 * 2D light : lights the level seen from the front (sprites, voxels, decor),
 * with shadows cast by the voxels. Drawn by the 2D lighting pass
 * (core/Lighting2D.h), which must be enabled for the project :
 * window "2D Lighting" in the editor, or Lighting2D.set("enabled", true).
 *
 *     auto& torch = actor->AddComponent<lynx::Light2DComponent>();
 *     torch.color = {1.f, 0.7f, 0.4f};
 *     torch.radius = 24.f;          // voxels
 *     torch.cast_shadows = true;
 *
 *     // cone (flashlight) : 60 degrees wide, towards +X turned by the actor
 *     torch.cone_angle = 60.f;
 *     torch.direction = 0.f;        // degrees, 0 = +X, 90 = +Y
 *
 * JS : actor.addComponent("Light2D") / actor.getComponent("Light2D"),
 *      fields color, intensity, radius, falloff, offset, enabled, castShadows,
 *      shadowStrength, sourceRadius, coneAngle, coneSoftness, direction,
 *      useActorRotation, flicker.
 * Engine actor : Light2DActor (Place Actors), same settings as properties.
 */

#include <cstdint>
#include <vector>

#include "Component.h"

namespace lynx
{
	class LYNX_API Light2DComponent : public Component
	{
	public:
		Light2DComponent();
		~Light2DComponent() override;

		vec3 color{1.f, 0.85f, 0.65f};
		float intensity = 1.5f;

		/** Reach of the light, in voxels. */
		float radius = 20.f;

		/** Shape of the fade : 1 linear, 2 smooth (default), 4 tight around the source. */
		float falloff = 2.f;

		/** From the actor location (world units). */
		vec3 offset{0.f};

		bool enabled = true;

		// --- Shadows ---------------------------------------------------------

		/** Voxels that block (solid collision) cast shadows. */
		bool cast_shadows = true;

		/** 0 : no shadow, 1 : black shadow (ambient light still lights it). */
		float shadow_strength = 1.f;

		/** Size of the source in voxels : 0 hard shadows, 1-3 soft edges (costs more). */
		float source_radius = 0.f;

		// --- Cone ------------------------------------------------------------

		/** Width of the cone in degrees ; 360 (or more) : light in every direction. */
		float cone_angle = 360.f;

		/** 0 : sharp edge of the cone, 1 : very soft. */
		float cone_softness = 0.3f;

		/** Direction of the cone, degrees (0 = +X, 90 = +Y). */
		float direction = 0.f;

		/** The actor rotation (Z, roll in 2D) is added to `direction`. */
		bool use_actor_rotation = true;

		// --- Animation -------------------------------------------------------

		/** 0 : steady ; 0.1-0.3 : torch / fire flicker of the intensity. */
		float flicker = 0.f;

		/** World position of the light (actor location + offset). */
		vec3 GetWorldLocation() const;

		/** Direction of the cone in degrees (with the actor rotation). */
		float GetWorldDirection() const;

		/** Every Light2DComponent alive (read by the 2D lighting pass). */
		static const std::vector<Light2DComponent*>& GetAll();

		/** Position / direction copied in LateUpdate (valid without the owner). */
		vec3 cached_location{0.f};
		float cached_direction = 0.f;
		bool has_cache = false;

	protected:
		void OnAttach() override;
		void Update(float dt) override;
		void LateUpdate(float dt) override;

	private:
		void Cache();
	};
}
