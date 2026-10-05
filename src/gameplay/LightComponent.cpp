#include "LightComponent.h"

#include "Actor.h"
#include "../core/Engine.h"

#include <hrl/hrl.h>

#include <algorithm>
#include <cmath>

namespace lynx
{
	namespace
	{
		constexpr uint32_t kInvalid = 0xFFFFFFFFu;
		constexpr float kDegToRad = 3.14159265358979f / 180.f;

		HRL_ELightType ToHRL(LightComponent::Type type)
		{
			switch (type)
			{
				case LightComponent::Type::Spot:        return HRL_SPOT_LIGHT;
				case LightComponent::Type::Directional: return HRL_DIRECTIONAL_LIGHT;
				case LightComponent::Type::Sky:         return HRL_SKY_LIGHT;
				default:                                return HRL_POINT_LIGHT;
			}
		}
	}

	LightComponent::~LightComponent()
	{
		if (light_ != kInvalid && HRL_IsValidLight(light_))
			HRL_DeleteLight(light_);
	}

	void LightComponent::OnAttach()
	{
		Sync(true);
	}

	void LightComponent::Update(float)
	{
		Sync(false);
	}

	void LightComponent::Refresh()
	{
		Sync(true);
	}

	vec3 LightComponent::GetWorldLocation() const
	{
		const Actor* owner = GetOwner();
		return owner ? owner->transform.location + offset : offset;
	}

	vec3 LightComponent::GetDirection() const
	{
		vec3 r = rotation;
		if (use_actor_rotation)
		{
			if (const Actor* owner = GetOwner())
				r += owner->transform.rotation;
		}

		// Same convention as the camera : yaw 0 = +X, yaw -90 = -Z, pitch up = +Y.
		const float pitch = r.x * kDegToRad;
		const float yaw = r.y * kDegToRad;
		return vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
	}

	void LightComponent::Sync(bool force)
	{
		// Type changed : a new renderer light.
		if (light_ != kInvalid && type != created_type_)
		{
			if (HRL_IsValidLight(light_))
				HRL_DeleteLight(light_);
			light_ = kInvalid;
		}

		// Deleted by the renderer (HRL_Shutdown, scene recreated...).
		if (light_ != kInvalid && !HRL_IsValidLight(light_))
			light_ = kInvalid;

		if (light_ == kInvalid)
		{
			const uint32_t scene = Engine::GetScene();

			if (!HRL_IsValidScene(scene))
				return;

			light_ = HRL_CreateLight(scene, ToHRL(type));
			created_type_ = type;
			has_last_ = false;

			if (light_ == kInvalid)
				return;
		}

		vec3 r = rotation;
		if (use_actor_rotation)
		{
			if (const Actor* owner = GetOwner())
				r += owner->transform.rotation;
		}
		const vec3 location = GetWorldLocation();
		const float inner = std::clamp(inner_angle, 0.f, 89.f);
		const float outer = std::clamp(std::max(outer_angle, inner), 0.f, 89.9f);

		const float state[24] = {
			color.x, color.y, color.z,
			enabled ? std::max(0.f, intensity) : 0.f,
			attenuation,
			location.x, location.y, location.z,
			r.x, r.y, r.z,
			inner, outer,
			cast_shadows ? 1.f : 0.f,
			shadow_strength, shadow_bias,
			static_cast<float>(shadow_resolution),
		};

		auto changed = [&](int first, int count)
		{
			if (force || !has_last_)
				return true;
			for (int i = first; i < first + count; ++i)
				if (state[i] != last_[i])
					return true;
			return false;
		};

		if (changed(0, 3))
			HRL_SetLightColor(light_, color.x, color.y, color.z);
		if (changed(3, 1))
			HRL_SetLightIntensity(light_, state[3]);

		const bool positional = type == Type::Point || type == Type::Spot;
		const bool directed = type == Type::Spot || type == Type::Directional;

		if (positional && attenuation >= 0.f && changed(4, 1))
			HRL_SetLightAttenuation(light_, attenuation);
		if (positional && changed(5, 3))
			HRL_SetLightLocation(light_, location.x, location.y, location.z);
		if (directed && changed(8, 3))
			HRL_SetLightRotation(light_, r.x, r.y, r.z);
		if (type == Type::Spot && changed(11, 2))
		{
			HRL_SetSpotLightInnerCutoff(light_, inner);
			HRL_SetSpotLightOuterCutoff(light_, outer);
		}

		if (type != Type::Sky)
		{
			const bool shadows_changed = changed(13, 1);
			if (shadows_changed)
				HRL_SetLightCastShadows(light_, cast_shadows ? 1 : 0);
			if (cast_shadows && (shadows_changed || changed(14, 1)))
				HRL_SetLightShadowStrength(light_, std::clamp(shadow_strength, 0.f, 1.f));
			if (cast_shadows && shadow_bias >= 0.f && (shadows_changed || changed(15, 1)))
				HRL_SetLightShadowBias(light_, shadow_bias);
			if (cast_shadows && shadow_resolution > 0 && (shadows_changed || changed(16, 1)))
				HRL_SetLightShadowResolution(light_, shadow_resolution);
		}

		std::copy(std::begin(state), std::end(state), last_);
		has_last_ = true;
	}
}
