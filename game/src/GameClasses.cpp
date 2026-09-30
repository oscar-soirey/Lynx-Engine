#include "GameClasses.h"

#include <Lynx.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

#include "hrl/hrl.h"

#include "AnimationSystem.h"


namespace
{
    std::vector<Pawn*> g_pawns;

    // Spatial index used for combat queries. Pawns are registered by the
    // voxel cell containing their center, so attacks only inspect nearby
    // cells instead of every Pawn in the game.
    constexpr int kPawnQueryCellSize = 16;

    struct PawnQueryCell
    {
        int x;
        int y;

        bool operator==(const PawnQueryCell& other) const
        {
            return x == other.x && y == other.y;
        }
    };

    struct PawnQueryCellHash
    {
        std::size_t operator()(const PawnQueryCell& cell) const
        {
            const std::uint64_t x = static_cast<std::uint32_t>(cell.x);
            const std::uint64_t y = static_cast<std::uint32_t>(cell.y);
            return static_cast<std::size_t>((x << 32) ^ y);
        }
    };

    std::unordered_map<PawnQueryCell, std::unordered_set<Pawn*>, PawnQueryCellHash>
        g_pawn_query_cells;

    PawnQueryCell GetPawnQueryCell(float voxel_x, float voxel_y)
    {
        return {
            static_cast<int>(std::floor(voxel_x / static_cast<float>(kPawnQueryCellSize))),
            static_cast<int>(std::floor(voxel_y / static_cast<float>(kPawnQueryCellSize)))
        };
    }
}

bool Pawn::debug_collision_enabled_ = false;

namespace
{

    float MoveTowards(float current, float target, float max_delta)
    {
        if (current < target)
            return std::min(current + max_delta, target);

        if (current > target)
            return std::max(current - max_delta, target);

        return target;
    }
}


// ============================================================
// Sprite
// ============================================================

Sprite::Sprite()
{
    sprite = HRL_CreateMeshSprite(lynx::GetScene());
    HRL_SetMeshUserHandle(sprite, this);
}

void Sprite::Init()
{
    HRL_SetMeshScale(sprite, transform.scale.x * relative_sprite_transform_.scale.x, transform.scale.y * relative_sprite_transform_.scale.y, 1.f);
}

void Sprite::Tick(double dt)
{
    (void)dt;
}

void Sprite::OnTransformChanged()
{
    Actor::OnTransformChanged();

    HRL_SetMeshLocation(
        sprite,
        transform.location.x + relative_sprite_transform_.location.x,
        transform.location.y + relative_sprite_transform_.location.y,
        transform.location.z + relative_sprite_transform_.location.z
    );
}


// ============================================================
// Pawn
// ============================================================

Pawn::~Pawn()
{
    auto it = std::find(g_pawns.begin(), g_pawns.end(), this);
    if (it != g_pawns.end())
        g_pawns.erase(it);

    if (spatial_cell_valid_)
    {
        const PawnQueryCell spatial_cell{spatial_cell_x_, spatial_cell_y_};
        auto cell_it = g_pawn_query_cells.find(spatial_cell);
        if (cell_it != g_pawn_query_cells.end())
        {
            cell_it->second.erase(this);
            if (cell_it->second.empty())
                g_pawn_query_cells.erase(cell_it);
        }
    }
}

void Pawn::SetCollisionDebugEnabled(bool enabled)
{
    debug_collision_enabled_ = enabled;
}

void Pawn::Init()
{
    if (std::find(g_pawns.begin(), g_pawns.end(), this) == g_pawns.end())
        g_pawns.push_back(this);

    Sprite::Init();
    UpdatePawnSpatialCell();

    block_particles.Initialize();

    impact_src_.AttachToActor(this);
    impact_src_.max_distance = 10.f;
    impact_src_.pitch_min = 0.8f;
    impact_src_.pitch_max = 1.2f;
    impact_src_.volume_min = 0.5f;
    impact_src_.volume_max = 0.8f;
}

void Pawn::Tick(double dt)
{
    Sprite::Tick(dt);

    UpdateMovement(static_cast<float>(dt));
    UpdatePawnSpatialCell();

    DrawCollisionDebug();


    // ----------------------------------------------------
    // Animation
    // ----------------------------------------------------

    if (current_blendspace_)
        current_blendspace_->update(dt);
}

void Pawn::OnTransformChanged()
{
    Sprite::OnTransformChanged();
    UpdatePawnSpatialCell();
}

void Pawn::UpdatePawnSpatialCell()
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

    const PawnQueryCell new_cell = GetPawnQueryCell(voxel_x, voxel_y);

    if (spatial_cell_valid_ &&
        spatial_cell_x_ == new_cell.x &&
        spatial_cell_y_ == new_cell.y)
    {
        return;
    }

    if (spatial_cell_valid_)
    {
        const PawnQueryCell old_cell{spatial_cell_x_, spatial_cell_y_};
        auto old_it = g_pawn_query_cells.find(old_cell);
        if (old_it != g_pawn_query_cells.end())
        {
            old_it->second.erase(this);
            if (old_it->second.empty())
                g_pawn_query_cells.erase(old_it);
        }
    }

    g_pawn_query_cells[new_cell].insert(this);
    spatial_cell_x_ = new_cell.x;
    spatial_cell_y_ = new_cell.y;
    spatial_cell_valid_ = true;
}

bool Pawn::BeginAttack()
{
    if (is_attacking_)
        return false;

    is_attacking_ = true;
    return true;
}

void Pawn::Attack(float direction)
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

    const float center_x =
        voxel_x + direction * attack_center_distance;

    const float center_y = voxel_y;

    const int min_x = static_cast<int>(
        std::floor(center_x - attack_radius - 1.f));

    const int max_x = static_cast<int>(
        std::ceil(center_x + attack_radius + 1.f));

    const int min_y = static_cast<int>(
        std::floor(center_y - attack_radius - 1.f));

    const int max_y = static_cast<int>(
        std::ceil(center_y + attack_radius + 1.f));

    // Query only the spatial cells touched by the attack area. Pawn centers
    // are indexed by cell, so this avoids scanning every Pawn in the game.
    const float query_radius_x = attack_radius + collider_width_ * 0.5f;
    const float query_radius_y = attack_radius + collider_height_ * 0.5f;

    const int query_min_cell_x = static_cast<int>(
        std::floor((center_x - query_radius_x) /
                   static_cast<float>(kPawnQueryCellSize)));
    const int query_max_cell_x = static_cast<int>(
        std::floor((center_x + query_radius_x) /
                   static_cast<float>(kPawnQueryCellSize)));
    const int query_min_cell_y = static_cast<int>(
        std::floor((center_y - query_radius_y) /
                   static_cast<float>(kPawnQueryCellSize)));
    const int query_max_cell_y = static_cast<int>(
        std::floor((center_y + query_radius_y) /
                   static_cast<float>(kPawnQueryCellSize)));

    for (int cell_y = query_min_cell_y; cell_y <= query_max_cell_y; ++cell_y)
    {
        for (int cell_x = query_min_cell_x; cell_x <= query_max_cell_x; ++cell_x)
        {
            const PawnQueryCell cell{cell_x, cell_y};
            const auto cell_it = g_pawn_query_cells.find(cell);

            if (cell_it == g_pawn_query_cells.end())
                continue;

            for (Pawn* other : cell_it->second)
            {
                if (other == this)
                    continue;

                float other_x;
                float other_y;

                if (HRL_WorldToVoxelCoordinates(
                        lynx::GetScene(),
                        other->transform.location.x,
                        other->transform.location.y,
                        &other_x,
                        &other_y) != HRL_TRUE)
                {
                    continue;
                }

                const float half_width = other->collider_width_ * 0.5f;
                const float half_height = other->collider_height_ * 0.5f;

                const float closest_x = std::clamp(
                    other_x,
                    center_x - half_width,
                    center_x + half_width
                );

                const float closest_y = std::clamp(
                    other_y,
                    center_y - half_height,
                    center_y + half_height
                );

                const float dx = closest_x - center_x;
                const float dy = closest_y - center_y;

                if ((dx * dx + dy * dy) > attack_radius * attack_radius)
                    continue;

                const float forward =
                    (other_x - voxel_x) * direction;

                if (forward < -0.5f)
                    continue;

                other->Hurt(this, hurt_amount_);
            }
        }
    }

    HRL_BeginVoxelEdit(lynx::GetScene());

    int destroyed_voxels = 0;

    for (int y = min_y; y <= max_y; ++y)
    {
        for (int x = min_x; x <= max_x; ++x)
        {
            const float dx =
                static_cast<float>(x) - center_x;

            const float dy =
                static_cast<float>(y) - center_y;

            if ((dx * dx + dy * dy) >
                attack_radius * attack_radius)
            {
                continue;
            }

            const float forward =
                (static_cast<float>(x) - voxel_x) * direction;

            if (forward < -0.5f)
                continue;

            const uint8_t type = HRL_GetVoxelType(
                lynx::GetScene(),
                x,
                y
            );

            if (type == 0)
                continue;

            HRL_SetVoxelType(
                lynx::GetScene(),
                x,
                y,
                0
            );

            ++destroyed_voxels;

            block_particles.PlayVoxel(
                static_cast<float>(x),
                static_cast<float>(y),
                transform.location.z,
                type,
                direction * 1.5f,
                2.4f
            );

            impact_src_.Play();
        }
    }

    HRL_EndVoxelEdit(lynx::GetScene());

    if (destroyed_voxels > 0)
    {
        lynx::GetCameraShake().Trigger();

        lynx::GetEngine()->SetGlobalTimeDilatation(
            0.1f,
            0.15f
        );
    }
}

void Pawn::FinishAttack()
{
    is_attacking_ = false;
}

void Pawn::Move(float direction)
{
    direction = std::clamp(direction, -1.f, 1.f);

    target_velocity_x_ = direction * move_speed_;

    // Pawn owns its facing direction.
    if (direction < 0.f)
    {
        facing_right_ = false;

        HRL_SetMeshScale(
            sprite,
            -std::abs(transform.scale.x) * relative_sprite_transform_.scale.x,
            transform.scale.y * relative_sprite_transform_.scale.y,
            1
        );
    }
    else if (direction > 0.f)
    {
        facing_right_ = true;

        HRL_SetMeshScale(
            sprite,
            std::abs(transform.scale.x) * relative_sprite_transform_.scale.x,
            transform.scale.y * relative_sprite_transform_.scale.y,
            1
        );
    }
}

bool Pawn::Jump()
{
    if (!IsGroundWithinDistance(2.0f))
        return false;

    velocity_y_ = jump_speed_;
    jump_hold_timer_ = jump_hold_time_;
    grounded_ = false;

    jump_action_held_ = true;

    return true;
}

void Pawn::StopJumping()
{
    jump_action_held_ = false;
}

bool Pawn::CheckCollision(
    float x,
    float y,
    uint32_t blocking_flags,
    HRL_VoxelCollision* out_collision
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

bool Pawn::CheckPawnCollision(float x, float y) const
{
    const float half_width = collider_width_ * 0.5f;
    const float half_height = collider_height_ * 0.5f;

    for (const Pawn* other : g_pawns)
    {
        if (other == this)
            continue;

        float other_x;
        float other_y;

        if (HRL_WorldToVoxelCoordinates(
                lynx::GetScene(),
                other->transform.location.x,
                other->transform.location.y,
                &other_x,
                &other_y) != HRL_TRUE)
        {
            continue;
        }

        const float other_half_width =
            other->collider_width_ * 0.5f;

        const float other_half_height =
            other->collider_height_ * 0.5f;

        if (std::abs(x - other_x) <
                half_width + other_half_width &&
            std::abs(y - other_y) <
                half_height + other_half_height)
        {
            return true;
        }
    }

    return false;
}

bool Pawn::CanMoveX(float x, float y, float dx) const
{
    if (dx == 0.f)
        return true;

    if (CheckPawnCollision(x, y))
        return false;

    if (dx > 0.f)
    {
        return !CheckCollision(
            x,
            y,
            HRL_VOXEL_COLLISION_RIGHT |
            HRL_VOXEL_COLLISION_INSIDE
        );
    }

    return !CheckCollision(
        x,
        y,
        HRL_VOXEL_COLLISION_LEFT |
        HRL_VOXEL_COLLISION_INSIDE
    );
}

bool Pawn::CanMoveY(float x, float y, float dy) const
{
    if (dy == 0.f)
        return true;

    if (CheckPawnCollision(x, y))
        return false;

    if (dy > 0.f)
    {
        return !CheckCollision(
            x,
            y,
            HRL_VOXEL_COLLISION_TOP |
            HRL_VOXEL_COLLISION_INSIDE
        );
    }

    return !CheckCollision(
        x,
        y,
        HRL_VOXEL_COLLISION_BOTTOM |
        HRL_VOXEL_COLLISION_INSIDE
    );
}

uint32_t Pawn::GetGroundVoxelFlags() const
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
        return 0;
    }

    float half_width_voxel;
    float half_height_voxel;

    if (HRL_WorldToVoxelCoordinates(
            lynx::GetScene(),
            collider_width_ * 0.5f,
            collider_height_ * 0.5f,
            &half_width_voxel,
            &half_height_voxel) != HRL_TRUE)
    {
        return 0;
    }

    const float feet_y =
        voxel_y - half_height_voxel;

    const int x =
        static_cast<int>(std::floor(voxel_x));

    const int y =
        static_cast<int>(std::floor(feet_y - 0.001f)) - 1;

    const uint8_t type = HRL_GetVoxelType(
        lynx::GetScene(),
        x,
        y
    );

    if (type == 0)
        return 0;

    return voxelData[type - 1].flags;
}

bool Pawn::IsGroundWithinDistance(float max_distance) const
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

void Pawn::UpdateMovement(float dt)
{
    // ====================================================
    // Horizontal movement
    // ====================================================

    const float target = target_velocity_x_;

    if (grounded_)
    {
        // ------------------------------------------------
        // No input: decelerate
        // ------------------------------------------------

        if (std::abs(target) < 0.001f)
        {
            velocity_x_ = MoveTowards(
                velocity_x_,
                0.f,
                ground_deceleration_ * dt
            );
        }

        // ------------------------------------------------
        // Change direction
        // ------------------------------------------------

        else if (
            std::abs(velocity_x_) > 0.001f &&
            std::signbit(velocity_x_) !=
            std::signbit(target)
        )
        {
            velocity_x_ = MoveTowards(
                velocity_x_,
                target,
                direction_change_acceleration_ * dt
            );
        }

        // ------------------------------------------------
        // Normal acceleration
        // ------------------------------------------------

        else
        {
            velocity_x_ = MoveTowards(
                velocity_x_,
                target,
                ground_acceleration_ * dt
            );
        }
    }
    else
    {
        // =================================================
        // Air control
        // =================================================

        if (std::abs(target) < 0.001f)
        {
            velocity_x_ = MoveTowards(
                velocity_x_,
                0.f,
                air_deceleration_ *
                air_control_ *
                dt
            );
        }
        else
        {
            velocity_x_ = MoveTowards(
                velocity_x_,
                target,
                air_acceleration_ *
                air_control_ *
                dt
            );
        }
    }


    // ====================================================
    // Vertical movement
    // ====================================================

    const bool holding_jump =
        jump_action_held_ &&
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

    IntegrateMovement(dt);
}

void Pawn::IntegrateMovement(float dt)
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
            {
                grounded_ = true;
            }

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


    if (!was_grounded && grounded_)
    {
        OnLanded();
    }
}

void Pawn::DrawCollisionDebug() const
{
    if (!debug_collision_enabled_)
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

    // The voxel world front face is at Z = 0 and the gameplay camera
    // looks toward negative Z from positive Z. Keep the debug outline just
    // in front of that face so it is not depth-occluded by the voxel world.
    const float z =
        transform.location.z - 0.1f;

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
        HRL_DEBUG_HOLLOW,
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