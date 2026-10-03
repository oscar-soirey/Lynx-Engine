#include "Player.h"

#include "GameClasses.h"

#include <Lynx.h>
#include <hrl/hrl.h>

#include "Projectile.h"


Player::Player()
{
    // Offset du mesh NON miroite (joueur vers la droite). Le miroir est automatique.
    // Corps du hero (sans l'epee) ~ 6 px a gauche du centre d'une frame de 42 px,
    // soit 6/42 * 5 = ~0.7 unite monde : on decale le quad vers la droite.
    relative_sprite_transform_.location.x = 0.7f;
    relative_sprite_transform_.location.y = 0.85f;

    //halo light autour du player
    point_light_ = HRL_CreateLight(lynx::GetScene(), HRL_POINT_LIGHT);
    HRL_SetLightIntensity(point_light_, 20.f);
    HRL_SetLightColor(point_light_, 0.9f, 0.9f, 1.0f);
}

Player::~Player() 
{
	lynx::UnattachAudioListener();
	HRL_DeleteLight(point_light_);
}


void Player::PlayFootstep()
{
    uint32_t flags = GetGroundVoxelFlags();
    if (flags & (GAME_VOXEL_ROCK | GAME_VOXEL_SAND))
    {
        rock_fs_src_.Play();
    }
}


//Revoir la logique de landed
void Player::OnLanded()
{
    //landed_src_.Play();
}


void Player::Init()
{
    relative_sprite_transform_.scale.x = 10.f;
    relative_sprite_transform_.scale.y = 10.f;

    attack_radius = 13.5f;
    attack_center_distance = 7.f;

    Pawn::Init();


    //Init
    lynx::AttachAudioListener(this);
    lynx::SetUseDopplerEffect(false);

    rock_fs_src_.AttachToActor(this);
    rock_fs_src_.max_distance = 5.f;
    rock_fs_src_.pitch_min = 0.8f;
    rock_fs_src_.pitch_max = 1.2f;
    rock_fs_src_.volume_min = 0.2f;
    rock_fs_src_.volume_max = 0.4f;

    landed_src_.AttachToActor(this);
    landed_src_.max_distance = 10.f;
    landed_src_.pitch_min = 0.8f;
    landed_src_.pitch_max = 1.2f;
    landed_src_.volume_min = 0.5f;
    landed_src_.volume_max = 0.8f;

    whoosh_src_.AttachToActor(this);
    whoosh_src_.pitch_min = 1.9f;
    whoosh_src_.pitch_max = 2.2f;
    whoosh_src_.volume_min = 0.6f;
    whoosh_src_.volume_max = 0.8f;


    max_jump_count=2;



    // ====================================================
    // Idle / Run
    // ====================================================

    _Run.add_event(1, [this]
    {
        PlayFootstep();
    });
    _Run.add_event(6, [this]
    {
        PlayFootstep();
    });

    bs_default.add(
        0.f,
        _Idle
    );

    bs_default.add(
        1.f,
        _Run
    );

    bs_default.add(
        -1.f,
        _Run
    );


    // Jump / Falling : pas de blend_space, ce sont des etats a part entiere
    // (voir les regles de transition plus bas).



    // ====================================================
    // Attack
    // ====================================================

    _Attack.add_event(1, [this]()
    {
        Attack(facing_right_ ? 1.0f : -1.0f);
        whoosh_src_.Play();
    });

    _WalkAttack.add_event(1, [this]()
    {
        if (attack_event_done_) return;
        attack_event_done_ = true;
        Attack(facing_right_ ? 1.0f : -1.0f);
        whoosh_src_.Play();
    });

    bs_attacking.add(
        0.f,
        _Attack
    );

    bs_attacking.add(
        1.f,
        _WalkAttack
    );

    bs_attacking.add(
        -1.f,
        _WalkAttack
    );


    //Regles de transition
    anim_manager_.add_state("loco", bs_default, { .variable = "speed" });

    // Jump joue une fois, puis enchaine tout seul sur Falling (next_state).
    anim_manager_.add_state("jump", _Jump, { .next_state = "falling" });
    anim_manager_.add_state("double_jump", _Jump, { .next_state = "falling" });
    anim_manager_.add_state("falling", _Falling);

    anim_manager_.add_state("attacking", bs_attacking, {
        .variable = "speed",
        .on_exit    = [this]() { FinishAttack(); }
    });
    anim_manager_.add_state("hurt", _Hurt);

    // Sol -> air ("airborne" est calcule dans Pawn::Tick)
    anim_manager_.add_transition("loco", "jump", anim_manager_.is_true("airborne"), 1);

    // Air -> sol
    anim_manager_.add_transition("jump", "loco", anim_manager_.is_false("airborne"), 1);
    anim_manager_.add_transition("falling", "loco", anim_manager_.is_false("airborne"), 1);

    // Fin d'une attaque / d'un hurt en l'air : retour direct sur la boucle de chute
    // (sinon on passerait par loco, ce qui relancerait Jump).
    anim_manager_.add_transition("attacking", "falling", anim_manager_.is_true("airborne"), 1, true);
    anim_manager_.add_transition("hurt", "falling", anim_manager_.is_true("airborne"), 1, true);

    // Deuxieme saut : depuis jump ou falling, on relance l'anim via double_jump.
    // Priorite 2 pour passer avant les transitions d'air.
    anim_manager_.add_transition("jump",    "double_jump", anim_manager_.triggered("double-jump"), 2);
    anim_manager_.add_transition("falling", "double_jump", anim_manager_.triggered("double-jump"), 2);

    // Retour au sol
    anim_manager_.add_transition("double_jump", "loco", anim_manager_.is_false("airborne"), 1);

    // Priorite 2 (> transitions d'air) : un trigger n'est valable qu'un seul update.
    // S'il perdait contre loco->jump, is_attacking_ resterait a true (BeginAttack
    // l'a deja mis) et on ne pourrait plus jamais attaquer.
    anim_manager_.add_any_transition("attacking", anim_manager_.triggered("start-attack"), 2);
    anim_manager_.add_any_transition("hurt", anim_manager_.triggered("hurt"), 2);
}

void Player::OnDoubleJump(int count)
{
    anim_manager_.set_trigger("double-jump");
}

void Player::ProcessInput()
{
    // ----------------------------------------------------
    // Select character
    // ----------------------------------------------------
    if (select_character_action_.IsPressed())
    {
        select_menu_open_ = true;
        last_select_dir_  = 0;
        selected_character_widget_ = current_player_form_ + 1;
        lynx::GetEngine()->SetGlobalTimeDilatation(0.07f);
        CreateSelectCharacterWidgets();
        SetSelectedWidget(selected_character_widget_);
    }
    else if (select_character_action_.IsReleased())
    {
        select_menu_open_ = false;
        current_player_form_ = selected_character_widget_ - 1;
        lynx::GetEngine()->SetGlobalTimeDilatation(1.f);
        RemoveSelectCharacterWidgets();
    }

    if (select_menu_open_)
    {
        const float axis = select_axis_.GetValue();
        const int dir = (axis > 0.5f) - (axis < -0.5f);   // -1 / 0 / +1

        if (dir != 0 && dir != last_select_dir_)           // seulement au moment où on pousse
        {
            selected_character_widget_ = std::clamp(selected_character_widget_ - dir, 2, 4);
            SetSelectedWidget(selected_character_widget_);
        }
        last_select_dir_ = dir;
    }

    // ----------------------------------------------------
    // Movement
    // ----------------------------------------------------

    const float move =
        move_action_.GetValue();

    Move(move);

    if (jump_action_.IsPressed())
        Jump();
    if (jump_action_.IsReleased())
        StopJumping();


    // ----------------------------------------------------
    // Attack
    // ----------------------------------------------------

    if (attack_action_.IsPressed() && BeginAttack())
    {
        attack_event_done_ = false;
        anim_manager_.set_trigger("start-attack");
    }
    if (cast_action_.IsPressed())
    {
        //anim_manager_.set_trigger("start-cast");
        CastAttack();
        //Hurt(this, 10);
    }
}

void Player::CastAttack()
{
    float pvelocity = 40.f;
    int direction_factor = 1;
    if (!facing_right_) direction_factor = -1;

    lynx::GetEngine()->GetCurrentLevel()
    ->SpawnActorFromClass<Projectile>(
    {transform.location,
    {},
    {3.f*direction_factor, 3.f, 1.f}
    })
    ->SetVelocity({pvelocity * direction_factor, 0.f});
}

void Player::OnTransformChanged()
{
    Pawn::OnTransformChanged();
    HRL_SetLightLocation(point_light_, transform.location.x, transform.location.y, transform.location.z);
}


void Player::CreateSelectCharacterWidgets()
{
    HRL_id font = lynx::RessourceFont("widgets/normal-font.ttf");

    {
        //Fond noir blurry
        HRL_id w = HRL_CreateWidget(lynx::GetViewport(), HRL_WIDGET_IMAGE);
        HRL_SetWidgetAnchor(w, 0.f, 0.f);
        HRL_SetWidgetPosition(w, 0.f, 0.f);
        HRL_SetImageTexture(w, lynx::RessourceTex("widgets/black-background.png"));
        HRL_SetWidgetZIndex(w, -100);
        //toute la taille de l'ecran
        HRL_SetWidgetSize(w, 1.f, 1.f);
        HRL_SetWidgetAlpha(w, 0.7f);
        select_character_scene_.Add(w);
    }



    {
        //Select Character text
        HRL_id w = HRL_CreateWidget(lynx::GetViewport(), HRL_WIDGET_LABEL);
        HRL_SetWidgetAnchor(w, 1.f, 1.f);
        HRL_SetWidgetPosition(w, 0.9f, 0.7f);
        HRL_SetLabelFont(w, font);
        HRL_SetLabelText(w, "Select Character");
        HRL_SetLabelTextSize(w, 30.f);
        select_character_scene_.Add(w);
    }

    {
        //Forme normale
        HRL_id w = HRL_CreateWidget(lynx::GetViewport(), HRL_WIDGET_LABEL);
        HRL_SetWidgetAnchor(w, 1.f, 1.f);
        HRL_SetWidgetPosition(w, 0.9f, 0.8f);
        HRL_SetLabelFont(w, font);
        HRL_SetLabelText(w, "Forme Normale");
        HRL_SetLabelTextSize(w, 22.f);
        select_character_scene_.Add(w);
    }
    {
        //Forme lourde
        HRL_id w = HRL_CreateWidget(lynx::GetViewport(), HRL_WIDGET_LABEL);
        HRL_SetWidgetAnchor(w, 1.f, 1.f);
        HRL_SetWidgetPosition(w, 0.9f, 0.85f);
        HRL_SetLabelFont(w, font);
        HRL_SetLabelText(w, "Forme Lourde");
        HRL_SetLabelTextSize(w, 22.f);
        select_character_scene_.Add(w);
    }
    {
        //Forme projectile
        HRL_id w = HRL_CreateWidget(lynx::GetViewport(), HRL_WIDGET_LABEL);
        HRL_SetWidgetAnchor(w, 1.f, 1.f);
        HRL_SetWidgetPosition(w, 0.9f, 0.9f);
        HRL_SetLabelFont(w, font);
        HRL_SetLabelText(w, "Forme Projectile");
        HRL_SetLabelTextSize(w, 22.f);
        select_character_scene_.Add(w);
    }
}

void Player::SetSelectedWidget(int in)
{
    HRL_SetLabelTintColor(select_character_scene_.Get(2), 1.f, 1.f, 1.f, 1.f);
    HRL_SetLabelTintColor(select_character_scene_.Get(3), 1.f, 1.f, 1.f, 1.f);
    HRL_SetLabelTintColor(select_character_scene_.Get(4), 1.f, 1.f, 1.f, 1.f);
    HRL_SetLabelTintColor(select_character_scene_.Get(in), 0.f, 1.f, 0.f, 1.f);
}

void Player::RemoveSelectCharacterWidgets()
{
    select_character_scene_.Delete();
}

void Player::SetPlayerForm(int in)
{

}

void Player::Death()
{
    movement_enabled_=false;
    input_enabled_=false;
}

void Player::Hurt(Actor *instigator, float amount)
{
    life -= 1;
    if (life <= 0)
    {
        Death();
    }
    RefreshLifeWidget();
}

void Player::RefreshLifeWidget()
{
    HRL_id w = life_widget_scene_.Get(0);
    std::string life_str(std::to_string(life));
    HRL_SetLabelText(w, life_str.c_str());
}

void Player::StartGame()
{
    Pawn::StartGame();
    //Create life widget
    HRL_id w = HRL_CreateWidget(lynx::GetViewport(), HRL_WIDGET_LABEL);
    HRL_SetWidgetAnchor(w, 1.f, 0.f);
    HRL_SetWidgetPosition(w, 0.9f, 0.1f);
    HRL_SetLabelFont(w, lynx::RessourceFont("widgets/normal-font.ttf"));
    HRL_SetLabelText(w, "3");
    HRL_SetLabelTextSize(w, 42.f);
    life_widget_scene_.Add(w);
}

void Player::EndGame()
{
    life_widget_scene_.Delete();
}
