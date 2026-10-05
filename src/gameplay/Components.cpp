#include "Components.h"

#include "Actor.h"
#include "../core/Engine.h"
#include "../core/Level.h"

#include <algorithm>

namespace lynx
{
	// ========================================================================
	// TagsComponent
	// ========================================================================

	bool TagsComponent::Has(const std::string& tag) const
	{
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}

	void TagsComponent::Add(const std::string& tag)
	{
		if (!Has(tag))
			tags.push_back(tag);
	}

	void TagsComponent::Remove(const std::string& tag)
	{
		std::erase(tags, tag);
	}


	// ========================================================================
	// VelocityComponent
	// ========================================================================

	void VelocityComponent::Tick(float dt)
	{
		Actor* owner = GetOwner();
		if (!owner)
			return;

		owner->transform.location += linear * dt;
		owner->transform.rotation += angular * dt;

		if (damping > 0.f)
		{
			const float k = std::max(0.f, 1.f - damping * dt);
			linear = linear * k;
			angular = angular * k;
		}
	}


	// ========================================================================
	// LifetimeComponent
	// ========================================================================

	void LifetimeComponent::Tick(float dt)
	{
		remaining -= dt;
		if (remaining > 0.f || destroy_requested_)
			return;

		Actor* owner = GetOwner();
		Level* level = Engine::Get() ? Engine::Get()->GetCurrentLevel() : nullptr;
		if (owner && level)
		{
			destroy_requested_ = true;
			// differe (Level::Update) : le parcours en cours continue sans risque
			level->DestroyActor(owner);
		}
	}
}
