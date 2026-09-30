#pragma once

#include "../GameClasses.h"

class Ennemy : public Pawn {
public:
	int life=100;

	Ennemy();
	void Init() override;

	void Hurt(Actor *instigator, float amount) override;
};