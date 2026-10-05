#include "ExampleActor.h"

#include <cstdio>

ExampleActor::ExampleActor()
{
	HPROPERTY(message_, lynx::Exposed);
	HPROPERTY(spin_speed_, lynx::Exposed);

	HFUNCTION(SayHello);
}

void ExampleActor::StartGame()
{
	lynx::Actor::StartGame();
	SayHello();
}

void ExampleActor::Tick(double dt)
{
	lynx::Actor::Tick(dt);

	if (spin_speed_ != 0.f)
	{
		transform.rotation.z += spin_speed_ * static_cast<float>(dt);
		OnTransformChanged();
	}
}

void ExampleActor::SayHello()
{
	std::printf("[%s] %s\n", object_id_.c_str(), message_.c_str());
}
