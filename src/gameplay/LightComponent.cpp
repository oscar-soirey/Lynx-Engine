#include "LightComponent.h"

#include "Actor.h"
#include "../core/Engine.h"

#include <hrl/hrl.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "../core/CameraState.h"

namespace lynx
{
	namespace
	{
		constexpr uint32_t kInvalid = 0xFFFFFFFFu;

		// Every LightComponent alive (for the light budget).
		std::vector<LightComponent*>& Registry()
		{
			static std::vector<LightComponent*> lights;
			return lights;
		}
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
		if (registered_)
		{
			auto& all = Registry();
			const auto it = std::find(all.begin(), all.end(), this);
			if (it != all.end())
			{
				*it = all.back();
				all.pop_back();
			}
		}
		if (light_ != kInvalid && HRL_IsValidLight(light_))
			HRL_DeleteLight(light_);
	}

	void LightComponent::OnAttach()
	{
		if (!registered_)
		{
			Registry().push_back(this);
			registered_ = true;
		}
		Sync(true);
	}

	void LightComponent::UpdateBudget()
	{
		auto& all = Registry();
		std::vector<std::pair<float, LightComponent*>> on;
		on.reserve(all.size());

		// The reference point : the light with the highest priority (the
		// player's), else the camera moved last.
		const LightComponent* ref = nullptr;
		for (LightComponent* l : all)
		{
			l->budget_ok_ = false;
			if (!l->enabled || l->intensity <= 0.f)
				continue;
			if (l->priority > 0 && (!ref || l->priority > ref->priority))
				ref = l;
			on.emplace_back(0.f, l);
		}
		if (static_cast<int>(on.size()) <= kLightBudget)
		{
			for (auto& [score, l] : on)
				l->budget_ok_ = true;
			return;
		}

		vec3 center(0.f);
		bool has_center = false;
		if (ref)
		{
			center = ref->GetWorldLocation();
			has_center = true;
		}
		else
		{
			has_center = camera_state::GetLastLocation(center);
		}

		for (auto& [score, l] : on)
		{
			if (l->type == Type::Sky || l->type == Type::Directional)
			{
				score = -1e30f;
				continue;
			}
			float d2 = 0.f;
			if (has_center)
			{
				const vec3 p = l->GetWorldLocation();
				const float dx = p.x - center.x, dy = p.y - center.y;
				d2 = dx * dx + dy * dy;
			}
			score = d2 - static_cast<float>(l->priority) * 1e12f;
		}
		std::nth_element(on.begin(), on.begin() + (kLightBudget - 1), on.end(),
		                 [](const auto& a, const auto& b) { return a.first < b.first; });
		for (int i = 0; i < kLightBudget; ++i)
			on[i].second->budget_ok_ = true;
	}

	void LightComponent::Update(float)
	{
		// LateUpdate sends the final state of the frame to HRL ; here only
		// the creation (a light exists from its first frame on).
		if (light_ == kInvalid)
			Sync(false);
	}

	void LightComponent::LateUpdate(float)
	{
		// Position finale de la frame (apres le mouvement du gameplay).
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

		// Off (enabled false, or no intensity) : no HRL light at all. HRL sends
		// only 32 of its scene lights to the shaders, taken in the order of a
		// hash map, the switched-off ones included : with many lights turned
		// off (Glow, gameplay), the ones that are on were dropped at random
		// (the light of the player...). Created again when switched on.
		if (!enabled || intensity <= 0.f || !budget_ok_)
		{
			if (light_ != kInvalid)
			{
				if (HRL_IsValidLight(light_))
					HRL_DeleteLight(light_);
				light_ = kInvalid;
			}
			has_last_ = false;
			return;
		}

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
