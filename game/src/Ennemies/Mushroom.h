#pragma once

#include "Ennemy.h"

class Mushroom : public Ennemy {
public:

	Mushroom();

	void Init() override;

	void Hurt(Actor *instigator, float amount) override;

	void StartGame() override;

protected:

	void BuildBehaviorTree() override;

	void Death() override;


private:

	lynx::blend_space bs_default{};
	lynx::animation idle_{sprite, "ennemy/Mushroom/Idle.png", 7};
	lynx::animation walk_{sprite, "ennemy/Mushroom/Run.png", 8};

	lynx::animation hurt_{sprite, "ennemy/Mushroom/Hit.png", 5, false, 0.1f};

	lynx::animation death_{sprite, "ennemy/Mushroom/Die.png", 15, false, 0.25f};

	lynx::AudioSource hurt_src_{"sounds/hit/BodyHit.wav"};
};