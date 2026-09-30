#pragma once

#include "Ennemy.h"

class A : public Ennemy {
public:
	void Init() override;

	void Hurt(Actor *instigator, float amount) override;

private:
	lynx::blend_space bs_default{};
	lynx::animation idle_{sprite, "ennemy/Idle.png", 4};
	lynx::animation walk_{sprite, "ennemy/Walk.png", 6};

	lynx::blend_space bs_hurt{};
	lynx::animation hurt_{sprite, "ennemy/Hurt.png", 4, false, 0.1f};
};