#include "Ennemy.h"

#include <Lynx.h>
#include <cmath>
#include <cstdio>

// Mettre a 0 pour couper les logs de debug du saut
#define ENNEMY_DEBUG_JUMP 1

#include "hrl/hrl.h"

#include "../Collision.h"

Ennemy::Ennemy()
{
	HPROPERTY(life, lynx::Exposed);
}

void Ennemy::Init()
{
	Pawn::Init();
	blood_particles_.Initialize();
	death_particles_.Initialize();
	player_hurt_particles_.Initialize();
	BuildBehaviorTree();

	hurt_target_cs_.Trigger(); //Je sais pas pourquoi mais ca marche
	hurt_target_cs_.duration = 0.25f;
	hurt_target_cs_.positionAmplitude = 1.6f;
	hurt_target_cs_.rotationAmplitude = 1.3f;
	hurt_target_cs_.frequency = 30.f;
	hurt_target_cs_.falloff = 2.4f;

}

void Ennemy::Tick(double _dt)
{
	Pawn::Tick(_dt);
	if (process_behavior_tick_)
		ProcessBehavior(static_cast<float>(_dt));
}

void Ennemy::Hurt(Actor *instigator, float amount)
{
	if (is_dead_)
		return;

	Pawn::Hurt(instigator, amount);
	CancelAttack(); // un ennemi blesse est interrompu
	life -= amount;
	if (life <= 0)
	{
		is_dead_ = true;
		SetCollisionMask(collision::kTerrainMask);
		Death();
	}

	//repousser l'ennemi
	if (instigator)
	{
		float direction =
				transform.location.x - instigator->transform.location.x;

		if (direction != 0.f)
		{
			direction = direction > 0.f ? 1.f : -1.f;
			LaunchPawn(direction * knockback_intensity_, 12.f, true, true);
		}

		//particules de sang
		blood_particles_.Play(transform.location.x, transform.location.y, 0.f, direction, 1.f);
	}
}

void Ennemy::ProcessBehavior(float delta_time)
{
	if (attack_cooldown_timer_ > 0.f)
		attack_cooldown_timer_ -= delta_time;

	if (jump_cooldown_timer_ > 0.f)
		jump_cooldown_timer_ -= delta_time;

	UpdateJumpState(delta_time);

#if ENNEMY_DEBUG_JUMP
	debug_timer_ += delta_time;
	if (debug_timer_ >= 0.5f && target_ && !is_dead_)
	{
		debug_timer_ = 0.f;
		const float ddx = target_->transform.location.x - transform.location.x;
		std::printf("[Ennemy] see=%d dx=%.2f obstacleAhead=%d jumping=%d attacking=%d jumpCd=%.2f stuckT=%.2f y=%.2f\n",
			CanSeeTarget() ? 1 : 0, ddx, HasObstacleAhead(ddx > 0.f ? 1.f : -1.f) ? 1 : 0,
			is_jumping_ ? 1 : 0, is_attacking_ ? 1 : 0, jump_cooldown_timer_, stuck_timer_, transform.location.y);
	}
#endif

	stuck_tracked_this_tick_ = false;

	if (behavior_root_)
		behavior_root_->Tick(delta_time);

	// Si on n'a pas essaye d'avancer ce tick, on repart de zero pour la detection
	if (!stuck_tracked_this_tick_)
	{
		stuck_timer_ = 0.f;
		stuck_dir_ = 0.f;
	}
}

bool Ennemy::HasTarget() const
{
	return target_ != nullptr;
}

float Ennemy::DistanceToTarget() const
{
	if (!target_)
		return INFINITY;

	float dx = target_->transform.location.x - transform.location.x;
	float dy = target_->transform.location.y - transform.location.y;
	return std::sqrt(dx * dx + dy * dy);
}

bool Ennemy::CanSeeTarget() const
{
	return HasTarget()
		&& DistanceToTarget() <= see_radius_
		&& HasLineOfSight();
}

bt::Status Ennemy::MoveToTarget(float delta_time, float acceptance_radius)
{
	if (!process_behavior_tick_)
		return bt::Status::Failure;

	if (!target_)
	{
		Move(0.f);
		return bt::Status::Failure;
	}

	float dx = target_->transform.location.x - transform.location.x;

	if (std::fabs(dx) <= acceptance_radius)
	{
		Move(0.f);
		return bt::Status::Success;
	}

	const float direction = dx > 0.f ? 1.f : -1.f;
	Move(direction);
	UpdateStuckDetection(delta_time, direction);
	return bt::Status::Running;
}

void Ennemy::UpdateStuckDetection(float delta_time, float direction)
{
	stuck_tracked_this_tick_ = true;

	const float x = transform.location.x;
	const float y = transform.location.y;

	// Nouvelle direction (ou premier tick) : on prend une position de reference
	if (stuck_dir_ != direction)
	{
		stuck_dir_ = direction;
		stuck_timer_ = 0.f;
		stuck_ref_x_ = x;
		stuck_ref_y_ = y;
		return;
	}

	stuck_timer_ += delta_time;
	if (stuck_timer_ < stuck_check_interval_)
		return;

	// Fin de la fenetre : a-t-on bouge ? (le test sur y evite de sauter en l'air)
	const bool blocked = std::fabs(x - stuck_ref_x_) < stuck_min_distance_
		&& std::fabs(y - stuck_ref_y_) < stuck_min_distance_;

	stuck_timer_ = 0.f;
	stuck_ref_x_ = x;
	stuck_ref_y_ = y;

	if (blocked && !is_jumping_ && jump_cooldown_timer_ <= 0.f)
		Jump("stuck");
}

void Ennemy::Jump(const char *reason)
{
	// Impulsion verticale uniquement : on garde la vitesse horizontale
	LaunchPawn(0.f, jump_velocity_, false, true);
	jump_cooldown_timer_ = jump_cooldown_;

#if ENNEMY_DEBUG_JUMP
	std::printf("[Ennemy] JUMP (%s)\n", reason);
#else
	(void)reason;
#endif

	is_jumping_ = true;
	jump_left_ground_ = false;
	jump_still_timer_ = 0.f;
	jump_air_time_ = 0.f;
	prev_y_ = transform.location.y;

	OnJump();
}

void Ennemy::UpdateJumpState(float delta_time)
{
	const float y = transform.location.y;

	if (is_jumping_ && delta_time > 0.f)
	{
		jump_air_time_ += delta_time;

		// Valeur absolue : ne depend pas du sens de l'axe y
		const float vy = std::fabs(y - prev_y_) / delta_time;

		if (vy > air_speed_threshold_)
		{
			jump_left_ground_ = true;
			jump_still_timer_ = 0.f;
		}
		else
		{
			jump_still_timer_ += delta_time;
		}

		const char *why = nullptr;
		if (jump_left_ground_ && jump_still_timer_ >= landed_confirm_time_)
			why = "atterri";
		else if (!jump_left_ground_ && jump_air_time_ >= takeoff_timeout_)
			why = "le saut n'a pas decolle";
		else if (jump_air_time_ >= max_jump_time_)
			why = "timeout";

		if (why)
		{
#if ENNEMY_DEBUG_JUMP
			std::printf("[Ennemy] fin du saut : %s (%.2fs)\n", why, jump_air_time_);
#endif
			is_jumping_ = false;
			jump_left_ground_ = false;
			OnLand();
		}
	}

	prev_y_ = y;
}

bool Ennemy::HasObstacleAhead(float direction) const
{
	if (direction == 0.f)
		return false;

	const float wx = transform.location.x;
	const float wy = transform.location.y + obstacle_probe_height_;

	float sx, sy, ex, ey;
	if (HRL_WorldToVoxelCoordinates(lynx::GetScene(), wx, wy, &sx, &sy) != HRL_TRUE)
		return false;
	if (HRL_WorldToVoxelCoordinates(lynx::GetScene(), wx + direction * obstacle_probe_distance_, wy, &ex, &ey) != HRL_TRUE)
		return false;

	// Comme pour la vue : un voxel qui collisionne avec collision_mask_ bloque le rayon
	return !collision::HasLineOfSight(sx, sy, ex, ey, collision_mask_);
}

bool Ennemy::ShouldJumpOverObstacle() const
{
	if (is_dead_ || is_jumping_ || is_attacking_ || jump_cooldown_timer_ > 0.f)
		return false;
	if (!CanSeeTarget())
		return false;

	const float dx = target_->transform.location.x - transform.location.x;
	if (std::fabs(dx) <= chase_acceptance_radius_)
		return false;

	return HasObstacleAhead(dx > 0.f ? 1.f : -1.f);
}

bool Ennemy::CanAttackTarget() const
{
	return !is_dead_
		&& !is_jumping_
		&& attack_cooldown_timer_ <= 0.f
		&& CanSeeTarget()
		&& DistanceToTarget() <= attack_range_;
}

bt::Status Ennemy::Attack(float delta_time)
{
	if (!process_behavior_tick_ || !target_)
	{
		CancelAttack();
		return bt::Status::Failure;
	}

	if (!is_attacking_)
	{
		is_attacking_ = true;
		attack_hit_done_ = false;
		attack_timer_ = 0.f;
		OnAttackStart();
	}

	Move(0.f); // l'ennemi s'arrete pendant l'attaque
	attack_timer_ += delta_time;

	// Instant du coup : on re-verifie la portee, le joueur peut avoir esquive
	if (!attack_hit_done_ && attack_timer_ >= attack_windup_)
	{
		attack_hit_done_ = true;

		if (DistanceToTarget() <= attack_range_)
		{
			if (auto *victim = dynamic_cast<Pawn *>(target_))
			{
				float dx = target_->transform.location.x - transform.location.x;
				int direction = 1;
				if (dx < 0) direction = -1;
				victim->LaunchPawn(direction*30.f, 10.f, true, true);
				player_hurt_particles_.Play(victim->transform.location.x, victim->transform.location.y, victim->transform.location.z);
				lynx::SetCameraShake(hurt_target_cs_);
				hurt_target_cs_.Trigger();
				//lynx::GetEngine()->SetGlobalTimeDilatation(0.04f, 0.1f);
				victim->Hurt(this, attack_damage_);
			}
		}
	}

	if (attack_timer_ >= attack_duration_)
	{
		is_attacking_ = false;
		attack_cooldown_timer_ = attack_cooldown_;
		OnAttackEnd();
		return bt::Status::Success;
	}

	return bt::Status::Running;
}

void Ennemy::CancelAttack()
{
	if (!is_attacking_)
		return;

	is_attacking_ = false;
	attack_hit_done_ = false;
	attack_timer_ = 0.f;
	attack_cooldown_timer_ = attack_cooldown_;
}

bool Ennemy::HasLineOfSight() const
{
	if (!target_)
		return false;

	// Positions en coordonnees voxel
	float sx, sy, ex, ey;

	if (HRL_WorldToVoxelCoordinates(
			lynx::GetScene(),
			transform.location.x,
			transform.location.y + eye_offset_y_,
			&sx, &sy) != HRL_TRUE)
	{
		return false;
	}

	if (HRL_WorldToVoxelCoordinates(
			lynx::GetScene(),
			target_->transform.location.x,
			target_->transform.location.y + eye_offset_y_,
			&ex, &ey) != HRL_TRUE)
	{
		return false;
	}

	// Un voxel bloque la vue s'il collisionne avec collision_mask_ (comme pour le mouvement)
	return collision::HasLineOfSight(sx, sy, ex, ey, collision_mask_);
}
