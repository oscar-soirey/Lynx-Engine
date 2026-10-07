#include "Light2DComponent.h"

#include "Actor.h"

#include <algorithm>

namespace lynx
{
	namespace
	{
		std::vector<Light2DComponent*>& Registry()
		{
			static std::vector<Light2DComponent*> lights;
			return lights;
		}
	}

	Light2DComponent::Light2DComponent()
	{
		Registry().push_back(this);
	}

	Light2DComponent::~Light2DComponent()
	{
		auto& lights = Registry();
		lights.erase(std::remove(lights.begin(), lights.end(), this), lights.end());
	}

	const std::vector<Light2DComponent*>& Light2DComponent::GetAll()
	{
		return Registry();
	}

	vec3 Light2DComponent::GetWorldLocation() const
	{
		const Actor* owner = GetOwner();
		return owner ? owner->transform.location + offset : offset;
	}

	float Light2DComponent::GetWorldDirection() const
	{
		float d = direction;
		if (use_actor_rotation)
			if (const Actor* owner = GetOwner())
				d += owner->transform.rotation.z;
		return d;
	}

	void Light2DComponent::Cache()
	{
		if (!GetOwner())
			return;
		cached_location = GetWorldLocation();
		cached_direction = GetWorldDirection();
		has_cache = true;
	}

	void Light2DComponent::OnAttach()
	{
		Cache();
	}

	void Light2DComponent::Update(float)
	{
		Cache();
	}

	void Light2DComponent::LateUpdate(float)
	{
		// Final position of the frame (after the gameplay moved the actor).
		Cache();
	}
}
