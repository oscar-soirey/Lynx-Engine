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


namespace
{
    // Sert uniquement a retrouver un Pawn dont reutiliser les particules / sons
    // (voir DestroyVoxelsInRadius). Les collisions sont dans Collision.cpp.
    std::vector<Pawn*> g_pawns;
}

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
    HPROPERTY(relative_sprite_transform_, lynx::Exposed, OnTransformChanged());

    sprite = HRL_CreateMeshSprite(lynx::GetScene());
    HRL_SetMeshUserHandle(sprite, this);
}

Sprite::~Sprite()
{
    HRL_DeleteMesh(sprite);
}

void Sprite::SetXOffset(float in)
{
    if (x_offset_ == in)
        return;

    x_offset_ = in;
    OnTransformChanged();
}

void Sprite::GetMeshWorldRect(float& center_x, float& center_y, float& width, float& height) const
{
    // Pawn::Move miroite le mesh en passant transform.scale.x en negatif.
    // L'offset X du sprite est exprime pour le mesh NON miroite : on le miroite
    // aussi, sinon le sprite saute de l'autre cote du collider quand on se retourne.
    const float flip_x = transform.scale.x < 0.f ? -1.f : 1.f;

    // L'offset est dans l'espace local de l'actor : il suit sa scale, comme le mesh.
    const float scale_x = std::abs(transform.scale.x);
    const float scale_y = std::abs(transform.scale.y);

    center_x = transform.location.x
             + relative_sprite_transform_.location.x * flip_x * scale_x
             + x_offset_;

    center_y = transform.location.y
             + relative_sprite_transform_.location.y * scale_y;

    width  = std::abs(transform.scale.x * relative_sprite_transform_.scale.x);
    height = std::abs(transform.scale.y * relative_sprite_transform_.scale.y);
}

void Sprite::OnTransformChanged()
{
    Actor::OnTransformChanged();

    float center_x, center_y, width, height;
    GetMeshWorldRect(center_x, center_y, width, height);

    HRL_SetMeshLocation(
        sprite,
        center_x,
        center_y,
        transform.location.z + relative_sprite_transform_.location.z
    );

    HRL_SetMeshScale(sprite, transform.scale.x * relative_sprite_transform_.scale.x, transform.scale.y * relative_sprite_transform_.scale.y, 1.f);
}

// ============================================================
// Static Sptite
// ============================================================
StaticSprite::StaticSprite()
{
    HPROPERTY(texture_path, lynx::Exposed, OnTextureChanged());
}

void StaticSprite::Init()
{
    Sprite::Init();
}

void StaticSprite::OnTextureChanged()
{
    HRL_id hrl_texture = lynx::RessourceTex(texture_path.c_str());

    // Unreadable path (empty, typo while typing in the editor...): keep the current material.
    if (hrl_texture == HRL_INVALID_ID)
        return;

    HRL_id material = HRL_CreateMaterial(HRL_SPRITE_SHADER);
    HRL_MaterialSetTexture(material, HRL_T_ALBEDO, hrl_texture);
    HRL_SetMeshMaterial(sprite, material);
}


// ============================================================
// Pawn
// ============================================================

Pawn::Pawn()
{
    HPROPERTY(collider_width_, lynx::Exposed);
    HPROPERTY(collider_height_, lynx::Exposed);
}

Pawn::~Pawn()
{
    auto it = std::find(g_pawns.begin(), g_pawns.end(), this);
    if (it != g_pawns.end())
        g_pawns.erase(it);

    collision::RemovePawnBody(this);
}

int Pawn::DestroyVoxelsInRadius(
    float center_x,
    float center_y,
    float radius,
    float world_z)
{
    // Les particules et le son ne dependent pas de la position du Pawn
    // (Attack() les joue deja a des positions arbitraires) : on reutilise
    // ceux d'un Pawn existant plutot que d'en creer par projectile.
    Pawn* fx = g_pawns.empty() ? nullptr : g_pawns.front();

    const int min_x = static_cast<int>(std::floor(center_x - radius - 1.f));
    const int max_x = static_cast<int>(std::ceil(center_x + radius + 1.f));
    const int min_y = static_cast<int>(std::floor(center_y - radius - 1.f));
    const int max_y = static_cast<int>(std::ceil(center_y + radius + 1.f));

    std::vector<VoxelEvent> voxel_events;

    HRL_BeginVoxelEdit(lynx::GetScene());

    int destroyed_voxels = 0;

    for (int y = min_y; y <= max_y; ++y)
    {
        for (int x = min_x; x <= max_x; ++x)
        {
            const float dx = static_cast<float>(x) - center_x;
            const float dy = static_cast<float>(y) - center_y;
            const float dist_sq = dx * dx + dy * dy;

            if (dist_sq > radius * radius)
                continue;

            const uint8_t type = HRL_GetVoxelType(lynx::GetScene(), x, y);

            if (type == 0 || IsVoxelIndestructible(type))
                continue;

            HRL_SetVoxelType(lynx::GetScene(), x, y, 0);

            ++destroyed_voxels;

            QueueVoxelEvent(voxel_events, type, x, y);

            if (fx)
            {
                // Les debris partent du centre vers l'exterieur.
                const float dist = std::sqrt(dist_sq);
                const float nx = dist > 0.001f ? dx / dist : 0.f;
                const float ny = dist > 0.001f ? dy / dist : 0.f;

                fx->block_particles.PlayVoxel(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    world_z,
                    type,
                    nx * 1.5f,
                    ny * 1.5f + 1.f
                );
            }
        }
    }

    HRL_EndVoxelEdit(lynx::GetScene());

    FireVoxelEvents(voxel_events);

    if (destroyed_voxels > 0 && fx)
    {
        float world_x;
        float world_y;

        if (HRL_VoxelToWorldCoordinates(
                lynx::GetScene(),
                center_x,
                center_y,
                &world_x,
                &world_y) == HRL_TRUE)
        {
            fx->impact_src_.PlayAtLocation({world_x, world_y, world_z});
        }
    }

    return destroyed_voxels;
}

void Pawn::Init()
{
    if (std::find(g_pawns.begin(), g_pawns.end(), this) == g_pawns.end())
        g_pawns.push_back(this);

    Sprite::Init();

    // Les classes filles (Player::Init, A::Init...) modifient relative_sprite_transform_
    // avant d'appeler Pawn::Init : sans ce refresh le mesh garde l'ancienne taille /
    // position jusqu'au premier deplacement.
    OnTransformChanged();

    block_particles.Initialize();

    impact_src_.AttachToActor(this);
    impact_src_.max_distance = 10.f;
    impact_src_.pitch_min = 0.8f;
    impact_src_.pitch_max = 1.2f;
    impact_src_.volume_min = 0.5f;
    impact_src_.volume_max = 0.8f;

    body_hit_cs_.duration = 0.6f;
    body_hit_cs_.positionAmplitude = 0.22f;
    body_hit_cs_.rotationAmplitude = 0.f;
    body_hit_cs_.falloff = 2.4f;

    anim_manager_.set_default_state("loco");
}

void Pawn::Tick(double dt)
{
    Sprite::Tick(dt);

    if (movement_enabled_)
    {
        UpdateMovement(static_cast<float>(dt));
    }

    SyncCollider();


    // ----------------------------------------------------
    // Animation
    // ----------------------------------------------------

    anim_manager_.set_float("speed", target_velocity_x_);
    anim_manager_.update(dt);

}

void Pawn::OnTransformChanged()
{
    Sprite::OnTransformChanged();
    SyncCollider();
}

void Pawn::SyncCollider()
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

    collision::SetPawnBody(
        this,
        collision::Box{
            voxel_x,
            voxel_y,
            collider_width_ * 0.5f,
            collider_height_ * 0.5f
        }
    );
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

    // Pawns touches par le disque d'attaque (grille spatiale du module collision).
    std::vector<collision::PawnHit> hits;
    collision::QueryPawnsInDisc(center_x, center_y, attack_radius, hits, this);

    for (const collision::PawnHit& hit : hits)
    {
        const float forward =
            (hit.box.x - voxel_x) * direction;

        if (forward < -0.5f)
            continue;

        hit.pawn->Hurt(this, hurt_amount_);

        lynx::SetCameraShake(body_hit_cs_);
        lynx::GetCameraShake().Trigger();
    }

    std::vector<VoxelEvent> voxel_events;

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

            if (type == 0 || IsVoxelIndestructible(type))
                continue;

            HRL_SetVoxelType(
                lynx::GetScene(),
                x,
                y,
                0
            );

            ++destroyed_voxels;

            QueueVoxelEvent(voxel_events, type, x, y);

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

    FireVoxelEvents(voxel_events);

    if (destroyed_voxels > 0)
    {
        lynx::SetCameraShake(destroy_voxels_cs_);
        lynx::GetCameraShake().Trigger();

        lynx::GetEngine()->SetGlobalTimeDilatation(
            0.1f,
            0.15f
        );
    }
}


void Pawn::LaunchPawn(float launch_x, float launch_y, bool override_x, bool override_y)
{
    if (override_x)
        velocity_x_ = launch_x;
    else
        velocity_x_ += launch_x;

    if (override_y)
        velocity_y_ = launch_y;
    else
        velocity_y_ += launch_y;
}

void Pawn::Hurt(Actor *instigator, float amount)
{
    anim_manager_.set_trigger("hurt");
    if (hurt_source_reference_)
    {
        hurt_source_reference_->Play();
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
    int invert_factor = 1;
    if ( invert_right_left_ ) invert_factor = -1;
    if (direction < 0.f)
    {
        facing_right_ = false;
        transform.scale.x = -std::abs(transform.scale.x)*invert_factor;
        SetXOffset(facing_left_relative_);
    }
    else if (direction > 0.f)
    {
        facing_right_ = true;
        transform.scale.x = std::abs(transform.scale.x)*invert_factor;
        SetXOffset(0.f);
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

bool Pawn::CanMoveX(float x, float y, float dx) const
{
    return collision::CanMoveX(
        this, x, y, collider_width_, collider_height_, collision_mask_, dx);
}

bool Pawn::CanMoveY(float x, float y, float dy) const
{
    return collision::CanMoveY(
        this, x, y, collider_width_, collider_height_, collision_mask_, dy);
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

    return collision::GetVoxelFlags(x, y);
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

void Pawn::Update(double dt)
{
    Sprite::Update(dt);

    // Le collider peut avoir ete modifie depuis l'editeur (taille) : on resynchronise.
    SyncCollider();

    if (collision::IsDebugEnabled())
    {
        collision::DrawDebugCollider(
            transform.location.x,
            transform.location.y,
            transform.location.z,
            collider_width_,
            collider_height_
        );

        // Bounds du quad du mesh (cyan) : doit entourer le personnage, centre sur son pivot.
        float mesh_x, mesh_y, mesh_w, mesh_h;
        GetMeshWorldRect(mesh_x, mesh_y, mesh_w, mesh_h);

        collision::DrawDebugRectWorld(
            mesh_x,
            mesh_y,
            transform.location.z,
            mesh_w,
            mesh_h,
            0.1f,
            0.9f,
            1.f
        );
    }
}
