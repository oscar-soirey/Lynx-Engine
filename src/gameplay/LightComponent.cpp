#include "LightComponent.h"

#include "Actor.h"
#include "../core/Engine.h"

#include <hrl/hrl.h>

namespace lynx
{
	namespace
	{
		constexpr uint32_t kInvalid = 0xFFFFFFFFu;
	}

	LightComponent::~LightComponent()
	{
		if (light_ != kInvalid)
			HRL_DeleteLight(light_);
	}

	void LightComponent::OnAttach()
	{
		Sync();
	}

	void LightComponent::Update(float)
	{
		Sync();
	}

	void LightComponent::Sync()
	{
		// Changement de type : nouvelle lumiere.
		if (light_ != kInvalid && type != created_type_)
		{
			HRL_DeleteLight(light_);
			light_ = kInvalid;
		}

		if (light_ == kInvalid)
		{
			const uint32_t scene = Engine::GetScene();

			if (!HRL_IsValidScene(scene))
				return;

			light_ = HRL_CreateLight(scene, type == Type::Sky ? HRL_SKY_LIGHT : HRL_POINT_LIGHT);
			created_type_ = type;

			if (light_ == kInvalid)
				return;
		}

		HRL_SetLightColor(light_, color.x, color.y, color.z);
		HRL_SetLightIntensity(light_, enabled ? intensity : 0.f);

		if (type == Type::Point)
		{
			if (Actor* owner = GetOwner())
			{
				const vec3 p = owner->transform.location + offset;
				HRL_SetLightLocation(light_, p.x, p.y, p.z);
			}
		}
	}
}
