#include <Lynx.h>
#include <iostream>
#include <cmath>
#include <algorithm>

#include "hrl/hrl.h"

#include "AnimationSystem.h"
#include "gameplay/Input.h"
#include "Particles/BlockParticles.h"


// ============================================================
// Sprite
// ============================================================

class Sprite : public lynx::Actor {
public:
    Sprite()
    {
        sprite = HRL_CreateMeshSprite(lynx::GetScene());
        HRL_SetMeshUserHandle(sprite, this);
    }

    void Tick(double dt) override
    {
        (void)dt;
    }

    void OnTransformChanged() override
    {
        Actor::OnTransformChanged();

        HRL_SetMeshLocation(
            sprite,
            transform.location.x + .5f,
            transform.location.y + 1.f,
            transform.location.z
        );
    }

protected:
    HRL_id sprite = HRL_INVALID_ID;
};


// ============================================================
// Pawn
// ============================================================

class Pawn : public Sprite {
public:
    void Init() override
    {
        Sprite::Init();
    }

    void Tick(double dt) override
    {
        Sprite::Tick(dt);

        UpdateMovement(static_cast<float>(dt));

        DrawCollisionDebug();
    }

    void ProcessInput() override
    {
        // ----------------------------------------------------
        // Horizontal movement
        // ----------------------------------------------------

        const float move = move_action_.GetValue();

        velocity_x_ = move * move_speed_;

        // ----------------------------------------------------
        // Jump
        // ----------------------------------------------------

        if (jump_action_.IsPressed() &&
            IsGroundWithinDistance(2.0f))
        {
            velocity_y_ = jump_speed_;
            jump_hold_timer_ = jump_hold_time_;
            grounded_ = false;
        }
    }

protected:

    // ========================================================
    // Collision
    // ========================================================

    bool CheckCollision(
        float x,
        float y,
        uint32_t blocking_flags,
        HRL_VoxelCollision* out_collision = nullptr
    ) const
    {
        HRL_VoxelCollision collision{};

        const bool collided = HRL_VoxelCheckCollision(
            lynx::GetScene(),
            x,
            y,
            collider_width_,
            collider_height_,
            collision_mask_,
            &collision
        ) == HRL_TRUE;

        if (out_collision)
            *out_collision = collision;

        if (!collided)
            return false;

        return (collision.flags & blocking_flags) != 0u;
    }

    bool CanMoveX(float x, float y, float dx) const
    {
        if (dx > 0.f)
        {
            return !CheckCollision(
                x,
                y,
                HRL_VOXEL_COLLISION_RIGHT |
                HRL_VOXEL_COLLISION_INSIDE
            );
        }

        if (dx < 0.f)
        {
            return !CheckCollision(
                x,
                y,
                HRL_VOXEL_COLLISION_LEFT |
                HRL_VOXEL_COLLISION_INSIDE
            );
        }

        return true;
    }

    bool CanMoveY(float x, float y, float dy) const
    {
        if (dy > 0.f)
        {
            return !CheckCollision(
                x,
                y,
                HRL_VOXEL_COLLISION_TOP |
                HRL_VOXEL_COLLISION_INSIDE
            );
        }

        if (dy < 0.f)
        {
            return !CheckCollision(
                x,
                y,
                HRL_VOXEL_COLLISION_BOTTOM |
                HRL_VOXEL_COLLISION_INSIDE
            );
        }

        return true;
    }


    // ========================================================
    // Ground detection
    // ========================================================

    bool IsGroundWithinDistance(float max_distance) const
    {
        float voxel_x;
        float voxel_y;

        if (HRL_WorldToVoxelCoordinates(
                lynx::GetScene(),
                transform.location.x,
                transform.location.y,
                &voxel_x,
                &voxel_y) != HRL_TRUE)
        {
            return false;
        }

        constexpr float probe_step = 0.05f;

        for (float distance = 0.0f;
             distance <= max_distance + 0.0001f;
             distance += probe_step)
        {
            if (!CanMoveY(
                    voxel_x,
                    voxel_y - distance,
                    -probe_step))
            {
                return true;
            }
        }

        return false;
    }


    // ========================================================
    // Movement
    // ========================================================

    void UpdateMovement(float dt)
    {
        const bool holding_jump =
            jump_action_.IsHeld() &&
            jump_hold_timer_ > 0.f &&
            velocity_y_ > 0.f;

        if (holding_jump)
        {
            jump_hold_timer_ -= dt;

            velocity_y_ -=
                gravity_ *
                jump_hold_gravity_scale_ *
                dt;
        }
        else
        {
            jump_hold_timer_ = 0.f;

            velocity_y_ -= gravity_ * dt;
        }

        Move(dt);
    }


    void Move(float dt)
    {
        float voxel_x;
        float voxel_y;

        if (HRL_WorldToVoxelCoordinates(
                lynx::GetScene(),
                transform.location.x,
                transform.location.y,
                &voxel_x,
                &voxel_y) != HRL_TRUE)
        {
            return;
        }

        float dx;
        float dy;

        if (HRL_WorldToVoxelCoordinates(
                lynx::GetScene(),
                velocity_x_ * dt,
                velocity_y_ * dt,
                &dx,
                &dy) != HRL_TRUE)
        {
            return;
        }

        const float max_step = 0.25f;

        const float distance =
            std::max(
                std::abs(dx),
                std::abs(dy)
            );

        const int steps =
            std::max(
                1,
                static_cast<int>(
                    std::ceil(distance / max_step)
                )
            );

        const float step_x =
            dx / static_cast<float>(steps);

        const float step_y =
            dy / static_cast<float>(steps);

        const bool was_grounded = grounded_;

        grounded_ = false;

        for (int i = 0; i < steps; ++i)
        {
            // =================================================
            // X
            // =================================================

            if (CanMoveX(
                    voxel_x + step_x,
                    voxel_y,
                    step_x))
            {
                voxel_x += step_x;
            }
            else
            {
                bool stepped = false;

                if (was_grounded &&
                    std::abs(step_x) > 0.f)
                {
                    const float old_x = voxel_x;
                    const float old_y = voxel_y;

                    if (CanMoveY(
                            voxel_x,
                            voxel_y + max_step_height_,
                            max_step_height_) &&
                        CanMoveX(
                            voxel_x + step_x,
                            voxel_y + max_step_height_,
                            step_x))
                    {
                        float test_y =
                            voxel_y + max_step_height_;

                        bool found_floor = false;

                        for (float down = step_down_increment_;
                             down <= max_step_height_ + 0.0001f;
                             down += step_down_increment_)
                        {
                            const float next_y =
                                test_y -
                                step_down_increment_;

                            if (CanMoveY(
                                    voxel_x + step_x,
                                    next_y,
                                    -step_down_increment_))
                            {
                                test_y = next_y;
                            }
                            else
                            {
                                found_floor = true;
                                test_y = next_y;
                                break;
                            }
                        }

                        if (found_floor)
                        {
                            voxel_x += step_x;
                            voxel_y = test_y;

                            grounded_ = true;
                            stepped = true;
                        }
                    }

                    if (!stepped)
                    {
                        voxel_x = old_x;
                        voxel_y = old_y;
                    }
                }

                if (!stepped)
                    velocity_x_ = 0.f;
            }


            // =================================================
            // Y
            // =================================================

            if (CanMoveY(
                    voxel_x,
                    voxel_y + step_y,
                    step_y))
            {
                voxel_y += step_y;
            }
            else
            {
                if (step_y < 0.f)
                    grounded_ = true;

                velocity_y_ = 0.f;
            }
        }

        float world_x;
        float world_y;

        if (HRL_VoxelToWorldCoordinates(
                lynx::GetScene(),
                voxel_x,
                voxel_y,
                &world_x,
                &world_y) == HRL_TRUE)
        {
            transform.location.x = world_x;
            transform.location.y = world_y;

            OnTransformChanged();
        }
    }


    // ========================================================
    // Debug collision
    // ========================================================

    void DrawCollisionDebug() const
    {
        if (!debug_collision_)
            return;

        float half_width_world;
        float half_height_world;

        if (HRL_VoxelToWorldCoordinates(
                lynx::GetScene(),
                collider_width_ * 0.5f,
                collider_height_ * 0.5f,
                &half_width_world,
                &half_height_world) != HRL_TRUE)
        {
            return;
        }

        const float left =
            transform.location.x -
            half_width_world;

        const float right =
            transform.location.x +
            half_width_world;

        const float bottom =
            transform.location.y -
            half_height_world;

        const float top =
            transform.location.y +
            half_height_world;

        const float z =
            transform.location.z + 0.01f;

        const float xs[] = {
            left,
            right,
            right,
            left
        };

        const float ys[] = {
            bottom,
            bottom,
            top,
            top
        };

        const float zs[] = {
            z,
            z,
            z,
            z
        };

        HRL_DrawDebugPolygon(
            lynx::GetScene(),
            HRL_DEBUG_SOLID,
            xs,
            ys,
            zs,
            4,
            1.f,
            0.1f,
            0.1f
        );

        HRL_DrawDebugPoint(
            lynx::GetScene(),
            transform.location.x,
            transform.location.y,
            z,
            6.f,
            1.f,
            1.f,
            0.f
        );
    }


protected:

    // ========================================================
    // Input
    // ========================================================

    lynx::InputAction jump_action_ = "jump";
    lynx::InputAxis1D move_action_ = "move_x";


    // ========================================================
    // Physics
    // ========================================================

    float velocity_x_ = 0.f;
    float velocity_y_ = 0.f;

    float move_speed_ = 10.f;

    float jump_speed_ = 15.f;

    float gravity_ = 40.f;

    float jump_hold_time_ = 0.32f;
    float jump_hold_timer_ = 0.f;

    float jump_hold_gravity_scale_ = 0.25f;


    // ========================================================
    // Step
    // ========================================================

    float max_step_height_ = 4.f;
    float step_down_increment_ = 0.03f;


    // ========================================================
    // Collider
    // ========================================================

    float collider_width_ = 5.8f;
    float collider_height_ = 15.f;

    uint32_t collision_mask_ = 1u;

    bool grounded_ = false;

    bool debug_collision_ = true;
};


// ============================================================
// Player
// ============================================================

class Player : public Pawn {
private:
    HRL_id point_light_=0;


public:

    void Init() override
    {
        Pawn::Init();

        block_particles.Initialize();
        //block_particles.SetLifetime(0.5f, 1.0f);

        // ====================================================
        // Idle / Run
        // ====================================================

        bs_default.add(
            0.f,
            { sprite, "hero/Idle.png", 4 }
        );

        bs_default.add(
            1.f,
            {sprite, "hero/Run.png", 6, true, 0.1f}
        );

        bs_default.add(
            -1.f,
            {sprite, "hero/Run.png", 6, true, 0.1f}
        );


        // ====================================================
        // Attack 1
        // ====================================================

        bs_attacking1.add(
            0.f,
            {sprite, "hero/Attack1.png", 6, false, 0.1f}
        );

        bs_attacking1.add(
            1.f,
            {sprite, "hero/WalkAttack1.png", 6, false, 0.1f}
        );

        bs_attacking1.add(
            -1.f,
            {sprite, "hero/WalkAttack1.png", 6, false, 0.1f}
        );


        // ====================================================
        // Attack 2
        // ====================================================

        Attack2.add_event(4, [this]()
        {
            DestroyVoxelsInAttackRange();
        });

        WalkAttack2.add_event(4, [this]()
        {
            DestroyVoxelsInAttackRange();
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
            attacking_finished();
        });


        current_blendspace_ = &bs_default;


        point_light_ = HRL_CreateLight(lynx::GetScene(), HRL_POINT_LIGHT);
        HRL_SetLightIntensity(point_light_, 20.f);
        HRL_SetLightColor(point_light_, 0.9f, 0.9f, 1.0f);
    }


    void Tick(double dt) override
    {
        // ----------------------------------------------------
        // Animation
        // ----------------------------------------------------

        if (current_blendspace_)
            current_blendspace_->update(dt);


        // ----------------------------------------------------
        // Movement
        // ----------------------------------------------------

        Pawn::Tick(dt);
    }


    void ProcessInput() override
    {
        // Pawn handles:
        // - movement
        // - jump
        Pawn::ProcessInput();


        // ----------------------------------------------------
        // Direction
        // ----------------------------------------------------

        const float move =
            move_action_.GetValue();

        if (move < 0.f)
        {
            facing_right_ = false;

            HRL_SetMeshScale(
                sprite,
                -transform.scale.x,
                transform.scale.y,
                transform.scale.z
            );
        }
        else if (move > 0.f)
        {
            facing_right_ = true;

            HRL_SetMeshScale(
                sprite,
                transform.scale.x,
                transform.scale.y,
                transform.scale.z
            );
        }


        // ----------------------------------------------------
        // Animation blend
        // ----------------------------------------------------

        if (current_blendspace_)
            current_blendspace_->set_value(move);


        // ----------------------------------------------------
        // Attack
        // ----------------------------------------------------

        if (attack_action_.IsPressed() &&
            !isAttacking)
        {
            current_blendspace_ = &bs_attacking2;

            isAttacking = true;

            bs_attacking2.restart();
        }
    }


    void OnTransformChanged() override
    {
        Pawn::OnTransformChanged();
        HRL_SetLightLocation(point_light_, transform.location.x, transform.location.y, transform.location.z);
    }


private:

    lynx::animation Attack2{sprite, "hero/Attack2.png", 6, false, 0.1f};
    lynx::animation WalkAttack2{sprite, "hero/WalkAttack2.png", 6, false, 0.1f};

    // ========================================================
    // Attack
    // ========================================================

    void DestroyVoxelsInAttackRange()
{
    float voxel_x;
    float voxel_y;

    if (HRL_WorldToVoxelCoordinates(
            lynx::GetScene(),
            transform.location.x,
            transform.location.y,
            &voxel_x,
            &voxel_y) != HRL_TRUE)
    {
        return;
    }

    const float attack_radius = 10.f;
    const float attack_center_distance = 4.5f;

    const float direction =
        facing_right_
            ? 1.0f
            : -1.0f;

    const float center_x =
        voxel_x +
        direction *
        attack_center_distance;

    const float center_y =
        voxel_y;

    const int min_x =
        static_cast<int>(
            std::floor(
                center_x -
                attack_radius -
                1.f
            )
        );

    const int max_x =
        static_cast<int>(
            std::ceil(
                center_x +
                attack_radius +
                1.f
            )
        );

    const int min_y =
        static_cast<int>(
            std::floor(
                center_y -
                attack_radius -
                1.f
            )
        );

    const int max_y =
        static_cast<int>(
            std::ceil(
                center_y +
                attack_radius +
                1.f
            )
        );


    // ========================================================
    // Voxel edit
    // ========================================================

    HRL_BeginVoxelEdit(
        lynx::GetScene()
    );

    int destroyed_voxels = 0;

    for (int y = min_y;
         y <= max_y;
         ++y)
    {
        for (int x = min_x;
             x <= max_x;
             ++x)
        {
            const float dx =
                static_cast<float>(x) -
                center_x;

            const float dy =
                static_cast<float>(y) -
                center_y;

            if ((dx * dx + dy * dy) >
                attack_radius *
                attack_radius)
            {
                continue;
            }


            const float forward =
                (static_cast<float>(x) -
                 voxel_x) *
                direction;

            if (forward < -0.5f)
                continue;


            // =================================================
            // Destroy voxel
            // =================================================

            uint8_t type = HRL_GetVoxelType(
                    lynx::GetScene(),
                    x,
                    y);
            if (type != 0)
            {
                HRL_SetVoxelType(
                    lynx::GetScene(),
                    x,
                    y,
                    0
                );

                ++destroyed_voxels;

                //ajoute un systeme de particules pour chaque blocs detruits
                float dir_x = 1.5f;
                if (!facing_right_) dir_x = -1.5f;
                float dir_y = 2.4f;

                block_particles.PlayVoxel(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    transform.location.z,
                    type,
                    dir_x,
                    dir_y
                );

            }
        }
    }


    HRL_EndVoxelEdit(
        lynx::GetScene()
    );


    // ========================================================
    // Camera shake
    // ========================================================

    if (destroyed_voxels > 0)
    {
        lynx::GetCameraShake().Trigger();

        lynx::GetEngine()->SetGlobalTimeDilatation(0.1f, 0.15f);
    }
}


    // ========================================================
    // Attack finished
    // ========================================================

    void attacking_finished()
    {
        current_blendspace_ =
            &bs_default;

        isAttacking = false;
    }


private:

    // ========================================================
    // Animations
    // ========================================================

    lynx::blend_space bs_default{};

    lynx::blend_space bs_attacking1{};

    lynx::blend_space bs_attacking2{};

    lynx::blend_space* current_blendspace_ =
        nullptr;


    // ========================================================
    // Input
    // ========================================================

    lynx::InputAction attack_action_ =
        "attack";


    // ========================================================
    // Player state
    // ========================================================

    bool facing_right_ = true;

    bool isAttacking = false;

    BlockParticles block_particles;
};


// ============================================================
// Module
// ============================================================

LYNX_LINK_MODULE(
    LYNX_MODULE_REGISTER(Sprite);
    LYNX_MODULE_REGISTER(Pawn);
    LYNX_MODULE_REGISTER(Player);
)