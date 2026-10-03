#pragma once

#include <Lynx.h>
#include <hrl/hrl.h>

#include "GameClasses.h"


class Player : public Pawn {
private:
	HRL_id point_light_ = 0;

public:
	Player();
	~Player() override;
	void Init() override;
	void ProcessInput() override;
	void OnTransformChanged() override;

protected:
	void OnLanded() override;

private:
	void PlayFootstep();
	void CastAttack();

	void CreateSelectCharacterWidgets();
	void RemoveSelectCharacterWidgets();

	lynx::blend_space bs_default{};
	lynx::animation _Idle{ sprite, "hero/Idle.png", 4 };
	lynx::animation _Run{sprite, "hero/Run.png", 6, true, 0.1f};

	lynx::blend_space bs_falling{};
	lynx::animation _Jump{sprite, "hero/Jump.png", 8, false, 0.1f};

	lynx::blend_space bs_attacking1{};
	lynx::animation _Attack1{sprite, "hero/Attack1.png", 6, false, 0.1f};
	lynx::animation _WalkAttack1{sprite, "hero/RunAttack1.png", 6, false, 0.1f};

	lynx::blend_space bs_attacking2{};
	lynx::animation _Attack2{sprite, "hero/Attack2.png", 6, false, 0.1f};
	lynx::animation _WalkAttack2{sprite, "hero/RunAttack2.png", 6, false, 0.1f};

	lynx::animation _Hurt{sprite, "hero/Hurt.png", 4, false, 0.15f};


	//actions
	lynx::InputAction jump_action_ = "jump";
	lynx::InputAxis1D move_action_ = "move_x";

	lynx::InputAction attack_action_ = "attack";
	lynx::InputAction cast_action_ = "cast";

	lynx::InputAction select_character_action_ = "select-character";
	lynx::InputAxis1D select_axis_ = "select-axis";

	//audio sources
	lynx::AudioSource rock_fs_src_ = "footstep1.wav";
	lynx::AudioSource landed_src_ = "landed.wav";

	//game widgets
	int selected_character_widget_=2;
	lynx::WidgetsScene select_character_scene_;
	//cosmetic-only, ne change pas la valeur ni la forme
	void SetSelectedWidget(int in);

	bool select_menu_open_ = false;
	int  last_select_dir_  = 0;

	//Forme du personnage, 1=normale, 2=lourde, 3=projectile, 4=
	int current_player_form_=1;
};