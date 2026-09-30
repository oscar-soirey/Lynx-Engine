#include "Player.h"

#include "GameClasses.h"

#include <Lynx.h>
#include <hrl/hrl.h>


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
    relative_sprite_transform_.location.x = .5f;
    relative_sprite_transform_.location.y = 1.f;
    relative_sprite_transform_.scale.x = 5.f;
    relative_sprite_transform_.scale.y = 5.f;

    Pawn::Init();

    //Init
    lynx::AttachAudioListener(this);

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



    // ====================================================
    // Idle / Run
    // ====================================================

    Run_.add_event(1, [this]()
    {
        PlayFootstep();
    });
    Run_.add_event(4, [this]()
    {
        PlayFootstep();
    });

    bs_default.add(
        0.f,
        Idle_
    );

    bs_default.add(
        1.f,
        Run_
    );

    bs_default.add(
        -1.f,
        Run_
    );


    //Jump
    bs_falling.add(0.f, Jump_);

    bs_falling.on_finished([&] { current_blendspace_ = &bs_default; });


    //Hurt
    bs_hurt.add(0.f, Hurt_);
    bs_hurt.on_finished([&] { current_blendspace_ = &bs_default; });


    // ====================================================
    // Attack 1
    // ====================================================

    Attack1.add_event(4, [this]()
    {
        Attack(facing_right_ ? 1.0f : -1.0f);
    });

    WalkAttack1.add_event(4, [this]()
    {
        Attack(facing_right_ ? 1.0f : -1.0f);
    });


    bs_attacking1.on_finished([this]
    {
        AttackingFinished();
    });

    bs_attacking1.add(
        0.f,
        Attack1
    );

    bs_attacking1.add(
        1.f,
        WalkAttack1
    );

    bs_attacking1.add(
        -1.f,
        WalkAttack1
    );



    // ====================================================
    // Attack 2
    // ====================================================

    Attack2.add_event(4, [this]()
    {
        Attack(facing_right_ ? 1.0f : -1.0f);
    });

    WalkAttack2.add_event(4, [this]()
    {
        Attack(facing_right_ ? 1.0f : -1.0f);
    });

    bs_attacking2.add(
        0.f,
        Attack2
    );

    bs_attacking2.add(
        1.f,
        WalkAttack2
    );

    bs_attacking2.add(
        -1.f,
        WalkAttack2
    );

    bs_attacking2.on_finished([this]
    {
        AttackingFinished();
    });


    current_blendspace_ = &bs_default;


    point_light_ = HRL_CreateLight(lynx::GetScene(), HRL_POINT_LIGHT);
    HRL_SetLightIntensity(point_light_, 20.f);
    HRL_SetLightColor(point_light_, 0.9f, 0.9f, 1.0f);
}

void Player::ProcessInput()
{
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
    // Animation blend
    // ----------------------------------------------------

    if (current_blendspace_)
        current_blendspace_->set_value(move);


    // ----------------------------------------------------
    // Attack
    // ----------------------------------------------------

    if (attack_action_.IsPressed() && BeginAttack())
    {
        current_blendspace_ = &bs_attacking2;
        bs_attacking2.restart();
    }
}

void Player::OnTransformChanged()
{
        Pawn::OnTransformChanged();
        HRL_SetLightLocation(point_light_, transform.location.x, transform.location.y, transform.location.z);
}

void Player::AttackingFinished()
{
        current_blendspace_ = &bs_default;
        FinishAttack();
}
