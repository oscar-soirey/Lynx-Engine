#pragma once

#include <Lynx.h>
#include <cstdint>
#include "hrl/hrl.h"
#include "Particles/BlockParticles.h"
#include "Collision.h"
#include <memory>
#include <vector>



// ============================================================
// Debug collision shape
// ============================================================
class DebugCollisionShape : public lynx::Actor {
public:

	float collider_width_ = 5.8f;
	float collider_height_ = 15.f;

	DebugCollisionShape()
	{
		HPROPERTY(collider_width_, lynx::Exposed);
		HPROPERTY(collider_height_, lynx::Exposed);
	}

	void Update(double _dt) override
	{
		collision::DrawDebugCollider(
				transform.location.x,
				transform.location.y,
				transform.location.z,
				collider_width_,
				collider_height_
		);
	}
};




// ============================================================
// Light
// ============================================================
class LightActor : public lynx::Actor {
public:

	std::string type="point";
	
	LightActor()
	{
		HPROPERTY(type, lynx::Exposed);
	}
	
	void Init() override 
	{
		light_ = HRL_CreateLight(lynx::GetScene(), HRL_SKY_LIGHT);
		HRL_SetLightIntensity(light_, 0.8f);
	}
	
	~LightActor() override
	{
		HRL_DeleteLight(light_);
	}
	
private:
	HRL_id light_;
};




// ============================================================
// Sound source
// ============================================================
class AudioSource2D : public lynx::Actor {
public:
    bool loop=true;
    std::string path;
    AudioSource2D()
    {
        HPROPERTY(path, lynx::Exposed);
        HPROPERTY(loop, lynx::Exposed);
    }

    void Init() override
    {
        audio2D_ = std::make_unique<lynx::Audio2D>(path.c_str());
        audio2D_->looping = loop;
    }

    void StartGame() override
    {
        audio2D_->Play();
    }

    void EndGame() override
    {
        audio2D_->Stop();
    }

private:
    std::unique_ptr<lynx::Audio2D> audio2D_;
};



// ============================================================
// Sprite
// ============================================================

class Sprite : public lynx::Actor {
public:
    Sprite();
    virtual ~Sprite();

		void SetXOffset(float in);

protected:
    HRL_id sprite = HRL_INVALID_ID;

    lynx::transform relative_sprite_transform_{};

    void OnTransformChanged() override;

    // Centre / taille (monde) du quad du mesh, tels que envoyes a HRL.
    // Le quad est centre sur sa position : c'est aussi ce que dessine le debug.
    void GetMeshWorldRect(float& center_x, float& center_y, float& width, float& height) const;

private:
	// Correction manuelle supplementaire (monde), appliquee telle quelle.
	float x_offset_=0.f;
};


class StaticSprite : public Sprite {
public:
    std::string texture_path;
    StaticSprite();
    void Init() override;
    void OnTextureChanged();
};


// ============================================================
// Pawn
// ============================================================

class Pawn : public Sprite {
public:
		Pawn();
    ~Pawn();

    void Init() override;
    void Tick(double dt) override;
		
		void Update(double dt) override;

    virtual void Hurt(Actor* instigator, float amount);

    // Masque de collision (voir collision::kTerrainMask / kPawnMask).
    //   SetCollisionMask(collision::kTerrainMask) : collisionne avec le terrain
    //   seulement, traverse tous les autres Pawns.
    //   SetCollisionMask(collision::kDefaultMask) : comportement normal.
    uint32_t GetCollisionMask() const { return collision_mask_; }
    void SetCollisionMask(uint32_t mask) { collision_mask_ = mask; }

    // Launches the Pawn by applying an instantaneous velocity.
    // Override flags replace the corresponding current velocity component.
    void LaunchPawn(float launch_x, float launch_y, bool override_x = false, bool override_y = false);

    // Detruit tous les voxels du disque donne (coordonnees voxel) et joue les
    // memes particules / son que Attack(). Retourne le nombre de voxels detruits.
    static int DestroyVoxelsInRadius(
        float center_x,
        float center_y,
        float radius,
        float world_z
    );

protected:
    void OnTransformChanged() override;

    float attack_radius = 10.f;
    float attack_center_distance = 4.5f;

	lynx::anim_manager anim_manager_;


    virtual void OnLanded(){}

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

    bool CanMoveX(float x, float y, float dx) const;
    bool CanMoveY(float x, float y, float dy) const;

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

	virtual void OnDoubleJump(int count) {}

    bool IsFacingRight() const { return facing_right_; }

protected:
    // Publie la boite du Pawn au module collision (a appeler a chaque changement).
    void SyncCollider();

    void UpdateMovement(float dt);
    void IntegrateMovement(float dt);



protected:

    // ========================================================
    // Physics
    // ========================================================

    float velocity_x_ = 0.f;
    float velocity_y_ = 0.f;

    float target_velocity_x_ = 0.f;

    float move_speed_ = 10.f;

		bool movement_enabled_=true;

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

		int max_jump_count=1;
		int current_jump_count=0;


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

    uint32_t collision_mask_ = collision::kDefaultMask;

    bool grounded_ = false;

    // Temps passe sans toucher le sol. Sert a piloter le parametre d'animation
    // "airborne" avec un petit delai (evite un flash de Jump sur une marche).
    float air_time_ = 0.f;
    float air_anim_delay_ = 0.05f;

    bool jump_action_held_ = false;

    bool is_attacking_ = false;

    float hurt_amount_ = 0.f;

    BlockParticles block_particles;
    lynx::AudioSource impact_src_ = "sound.wav";

    lynx::AudioSource* hurt_source_reference_=nullptr;


    bool facing_right_ = true;
		//Correction manuelle (monde) de la position X du sprite quand le pawn regarde a gauche. Le miroir de l offset est deja automatique (Sprite::GetMeshWorldRect) : laisser a 0 sauf cas particulier.
		float facing_left_relative_=0.f;

		bool invert_right_left_=false;

    lynx::CameraShake destroy_voxels_cs_;
		lynx::CameraShake body_hit_cs_;
};