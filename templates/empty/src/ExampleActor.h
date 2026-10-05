#pragma once

#include <Lynx.h>

// An actor of the game : appears in "Place Actors" (registered in Module.cpp).
// HPROPERTY members are editable in Details and saved with the level ;
// HFUNCTION functions can be called from JavaScript and Python.
class ExampleActor : public lynx::Actor
{
public:
	ExampleActor();

	void StartGame() override;
	void Tick(double dt) override;

	void SayHello();

protected:
	std::string message_ = "Hello from {{PROJECT_TITLE}} !";
	float spin_speed_ = 0.f;           // degrees per second around Z
};
