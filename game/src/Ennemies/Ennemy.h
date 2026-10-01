#pragma once

#include "../GameClasses.h"
#include "../Particles/BloodParticles.h"

class Ennemy : public Pawn {
public:
	int life=100;

	Ennemy();
	void Init() override;

	void Hurt(Actor *instigator, float amount) override;

protected:
	float knockback_intensity_ = 20.0f;

	BloodParticles blood_particles_;
};