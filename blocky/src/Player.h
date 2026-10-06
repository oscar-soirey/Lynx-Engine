#pragma once

#include <Lynx.h>
#include <hrl/hrl.h>

#include "GameClasses.h"
#include "widgets/WidgetScene.h"


class Player : public Pawn {
private:
	HRL_id point_light_ = 0;

public:
	Player();
	~Player() override;
	void Init() override;

	void Hurt(Actor *instigator, float amount) override;
	void StartGame() override;
	void EndGame() override;

protected:
	void OnTransformChanged() override;
	void OnLanded() override;
	void OnDoubleJump(int count) override;

private:
	void PlayFootstep();
	void CastAttack();

	void ProcessInput() override;

	void CreateSelectCharacterWidgets();

	lynx::blend_space bs_default{};
	lynx::animation _Idle{ sprite, "hero/Idle.png", 10 };
	lynx::animation _Run{sprite, "hero/Run.png", 10, true, 0.1f};

	// Air : _Jump = debut (une fois), puis _Falling = boucle tant qu'on est en l'air
	lynx::animation _Falling{sprite, "hero/Falling.png", 3, true, 0.1f};
	lynx::animation _Jump{sprite, "hero/Jump.png", 6, false, 0.1f};

	lynx::animation _Hurt{sprite, "hero/Hurt.png", 4, false, 0.15f};

	lynx::blend_space bs_attacking{};
	lynx::animation _Attack{sprite, "hero/Attack.png", 3, false, 0.1f};
	lynx::animation _WalkAttack{sprite, "hero/Attack.png", 3, false, 0.1f};

	bool attack_event_done_=false;

	// Direction verticale de l'attaque en cours : +1 haut, -1 bas, 0 devant (lue au debut de l'attaque)
	float attack_dir_y_ = 0.f;
	static constexpr float kAttackAimThreshold = 0.5f;

	int life=3;

	//actions
	lynx::InputAction jump_action_ = "jump";
	lynx::InputAxis1D move_action_ = "move_x";
	lynx::InputAxis1D move_y_action_ = "move_y";

	lynx::InputAction attack_action_ = "attack";
	lynx::InputAction cast_action_ = "cast";

	lynx::InputAction select_character_action_ = "select-character";
	lynx::InputAxis1D select_axis_ = "select-axis";

	//audio sources
	lynx::AudioSource rock_fs_src_ = "footstep1.wav";
	lynx::AudioSource landed_src_ = "landed.wav";
	lynx::AudioSource whoosh_src_ = "sounds/whoosh/whoosh.wav";

	//game widgets
	int selected_character_widget_=2;
	lynx::WidgetsScene select_character_scene_;
	//cosmetic-only, ne change pas la valeur ni la forme
	void SetSelectedWidget(int in);

	bool select_menu_open_ = false;
	int  last_select_dir_  = 0;

	//Forme du personnage, 1=normale, 2=lourde, 3=projectile, 4=
	int current_player_form_=1;

	void SetPlayerForm(int in);

	void Death();

	void RefreshLifeWidget();
	lynx::WidgetsScene life_widget_scene_;
};
