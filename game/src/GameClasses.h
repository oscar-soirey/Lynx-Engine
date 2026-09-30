#pragma once

#include <Lynx.h>
#include <cstdint>
#include "hrl/hrl.h"
#include "AnimationSystem.h"
#include "Particles/BlockParticles.h"

// ============================================================
// Sprite
// ============================================================

class Sprite : public lynx::Actor {
public:
    Sprite();
    void Init() override;
    void Tick(double dt) override;
    void OnTransformChanged() override;

protected:
    HRL_id sprite = HRL_INVALID_ID;

    lynx::transform relative_sprite_transform_;
};


// ============================================================
// Pawn
// ============================================================

class Pawn : public Sprite {
public:
    ~Pawn();

    void Init() override;
    void Tick(double dt) override;
    void OnTransformChanged() override;

    virtual void Hurt(lynx::Actor* instigator, float amount){}

    static void SetCollisionDebugEnabled(bool enabled);

protected:

    float attack_radius = 10.f;
    float attack_center_distance = 4.5f;


    virtual void OnLanded(){};

    // ========================================================
    // Combat
    // ========================================================

    bool BeginAttack();
    void Attack(float direction);
    void FinishAttack();
    bool IsAttacking() const { return is_attacking_; }

    // ========================================================
    // Collision
    // ========================================================

    bool CheckCollision(
        float x,
        float y,
        uint32_t blocking_flags,
        HRL_VoxelCollision* out_collision = nullptr
    ) const;

    bool CanMoveX(float x, float y, float dx) const;
    bool CanMoveY(float x, float y, float dy) const;

    bool CheckPawnCollision(float x, float y) const;

    // ========================================================
    // Ground detection
    // ========================================================

    bool IsGroundWithinDistance(float max_distance) const;

    uint32_t GetGroundVoxelFlags() const;

    // ========================================================
    // Movement
    // ========================================================

    void Move(float direction);
    bool Jump();
    void StopJumping();

    bool IsFacingRight() const { return facing_right_; }

protected:
    void UpdatePawnSpatialCell();

    void UpdateMovement(float dt);
    void IntegrateMovement(float dt);

    // ========================================================
    // Debug collision
    // ========================================================

    void DrawCollisionDebug() const;


protected:

    // ========================================================
    // Physics
    // ========================================================

    float velocity_x_ = 0.f;
    float velocity_y_ = 0.f;

    float target_velocity_x_ = 0.f;

    float move_speed_ = 10.f;

    // Ground acceleration.
    float ground_acceleration_ = 170.f;

    // Deceleration when releasing movement.
    float ground_deceleration_ = 200.f;

    // Acceleration when changing direction.
    float direction_change_acceleration_ = 220.f;

    // Air movement.
    float air_acceleration_ = 135.f;
    float air_deceleration_ = 120.f;

    // 0 = no air control, 1 = full air control.
    float air_control_ = 1.f;

    float jump_speed_ = 18.f;

    float gravity_ = 55.f;

    float jump_hold_time_ = 0.32f;
    float jump_hold_timer_ = 0.f;

    float jump_hold_gravity_scale_ = 0.25f;


    // ========================================================
    // Step
    // ========================================================

    float max_step_height_ = 3.f;
    float step_down_increment_ = 0.03f;


    // ========================================================
    // Collider
    // ========================================================

    float collider_width_ = 5.8f;
    float collider_height_ = 15.f;

    uint32_t collision_mask_ = 1u;

    bool grounded_ = false;

    bool jump_action_held_ = false;

    bool is_attacking_ = false;

    float hurt_amount_ = 0.f;

    int spatial_cell_x_ = 0;
    int spatial_cell_y_ = 0;
    bool spatial_cell_valid_ = false;

    BlockParticles block_particles;
    lynx::AudioSource impact_src_ = "sound.wav";

    static bool debug_collision_enabled_;




    lynx::blend_space* current_blendspace_ = nullptr;

    bool facing_right_ = true;
};