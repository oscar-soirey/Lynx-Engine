#pragma once

#include <Lynx.h>
#include <hrl/hrl.h>

#include "GameClasses.h"


class Player : public Pawn {
private:
	HRL_id point_light_ = 0;

public:
	void Init() override;
	void ProcessInput() override;
	void OnTransformChanged() override;

protected:
	void OnLanded() override;

private:
	void AttackingFinished();
	void PlayFootstep();

	lynx::animation Idle_{ sprite, "hero/Idle.png", 4 };
	lynx::animation Run_{sprite, "hero/Run.png", 6, true, 0.1f};

	lynx::animation Jump_{sprite, "hero/Jump.png", 8, false, 0.1f};

	lynx::animation Hurt_{sprite, "hero/Hurt.png", 4, false, 0.15f};

	lynx::animation Attack1{sprite, "hero/Attack1.png", 6, false, 0.1f};
	lynx::animation WalkAttack1{sprite, "hero/RunAttack1.png", 6, false, 0.1f};
	lynx::animation Attack2{sprite, "hero/Attack2.png", 6, false, 0.1f};
	lynx::animation WalkAttack2{sprite, "hero/RunAttack2.png", 6, false, 0.1f};

	lynx::blend_space bs_default{};
	lynx::blend_space bs_falling{};
	lynx::blend_space bs_hurt{};

	lynx::blend_space bs_attacking1{};
	lynx::blend_space bs_attacking2{};

	lynx::InputAction jump_action_ = "jump";
	lynx::InputAxis1D move_action_ = "move_x";

	lynx::InputAction attack_action_ = "attack";

	lynx::AudioSource rock_fs_src_ = "footstep1.wav";
	lynx::AudioSource landed_src_ = "landed.wav";
};