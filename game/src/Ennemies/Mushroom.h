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

	void OnAttackStart() override;
	void OnAttackEnd() override;
	void OnJump() override;
	void OnLand() override;


private:

	lynx::blend_space bs_default{};
	lynx::animation idle_{sprite, "ennemy/Mushroom/Idle.png", 7};
	lynx::animation walk_{sprite, "ennemy/Mushroom/Run.png", 8};

	// Idle joue pendant tout le saut / la chute (objet separe de idle_ qui est dans le blend space)
	lynx::animation air_idle_{sprite, "ennemy/Mushroom/Idle.png", 7};

	// ATTENTION : adapter le chemin et le nombre de frames a ton asset
	lynx::animation attack_{sprite, "ennemy/Mushroom/Attack.png", 10, false, 0.07f};

	lynx::animation hurt_{sprite, "ennemy/Mushroom/Hit.png", 5, false, 0.1f};

	lynx::animation death_{sprite, "ennemy/Mushroom/Die.png", 15, false, 0.2f};

	lynx::AudioSource hurt_src_{"sounds/hit/BodyHit.wav"};
};