#include "Ennemy.h"

#include <Lynx.h>
#include <cmath>

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
	BuildBehaviorTree();
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
	if (behavior_root_)
		behavior_root_->Tick(delta_time);
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

	(void)delta_time; // le mouvement est gere par Pawn::UpdateMovement

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

	Move(dx > 0.f ? 1.f : -1.f);
	return bt::Status::Running;
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
