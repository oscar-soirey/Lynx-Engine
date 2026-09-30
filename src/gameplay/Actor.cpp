#include "Actor.h"

#include "../core/Engine.h"

namespace lynx
{
	void Actor::OnTransformChanged()
	{
		ED_transform_modified.Call();
	}


	Actor::Actor()
	{
		HPROPERTY(transform, Exposed, OnTransformChanged());
	}

	void Actor::OnPossessed(int pc)
	{

	}

	void Actor::OnUnpossessed(int pc)
	{

	}
}
