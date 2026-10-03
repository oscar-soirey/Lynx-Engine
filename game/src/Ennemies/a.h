#pragma once

#include "Ennemy.h"

class A : public Ennemy {
public:

	A();

	void Init() override;

	void Hurt(Actor *instigator, float amount) override;

	void StartGame() override;

protected:

	void BuildBehaviorTree() override;

	void Death() override;


private:

	lynx::blend_space bs_default{};
	lynx::animation idle_{sprite, "ennemy/Idle.png", 4};
	lynx::animation walk_{sprite, "ennemy/Walk.png", 6};

	lynx::animation hurt_{sprite, "ennemy/Hurt.png", 4, false, 0.1f};

	lynx::animation death_{sprite, "ennemy/Death.png", 6, false, 0.25f};

	lynx::AudioSource hurt_src_{"sounds/hit/BodyHit.wav"};
};