#include "Humanoid.h"

#include "BoxColliderComponent.h"
#include "EngineActors.h"
#include "../core/Engine.h"
#include "../core/VoxelPhysics.h"
#include "../scripting/Private/ScriptSystem.h"

#include <algorithm>
#include <cmath>

namespace lynx
{
	namespace
	{
		float MoveTowards(float current, float target, float max_delta)
		{
			if (std::fabs(target - current) <= max_delta)
				return target;
			return current + (target > current ? max_delta : -max_delta);
		}

		constexpr float kNoInput = 0.001f;
	}


	Humanoid::Humanoid()
	{
		HPROPERTY(collider_size, Exposed);
		HPROPERTY(move_speed, Exposed);
		HPROPERTY(ground_acceleration, Exposed);
		HPROPERTY(ground_deceleration, Exposed);
		HPROPERTY(direction_change_acceleration, Exposed);
		HPROPERTY(air_acceleration, Exposed);
		HPROPERTY(air_deceleration, Exposed);
		HPROPERTY(air_control, Exposed);
		HPROPERTY(jump_speed, Exposed);
		HPROPERTY(gravity, Exposed);
		HPROPERTY(max_fall_speed, Exposed);
		HPROPERTY(jump_hold_time, Exposed);
		HPROPERTY(jump_hold_gravity_scale, Exposed);
		HPROPERTY(max_jump_count, Exposed);
		HPROPERTY(max_step_height, Exposed);
		HPROPERTY(face_movement_direction, Exposed);
		HPROPERTY(movement_enabled, Exposed);
		HPROPERTY(collision_layer, Exposed);
		HPROPERTY(collision_mask, Exposed);
		HPROPERTY(show_collider, Exposed);

		// Appelables depuis JavaScript (this.Move(1), this.Jump()...).
		HFUNCTION(Move);
		HFUNCTION(Jump);
		HFUNCTION(StopJumping);
		HFUNCTION(Launch);
		HFUNCTION(StopMovement);
		HFUNCTION(SetVelocity);
		HFUNCTION(GetVelocity);
		HFUNCTION(GetMoveInput);
		HFUNCTION(IsGrounded);
		HFUNCTION(IsFalling);
		HFUNCTION(IsFacingRight);
		HFUNCTION(GetJumpCount);
		HFUNCTION(IsGroundWithinDistance);
	}


	// ========================================================================
	// Cycle de vie
	// ========================================================================

	void Humanoid::Init()
	{
		Actor::Init();

		collider_ = &AddComponent<ColliderComponent>();
		collider_->trigger = false;
		collider_->movable = true;
		collider_->collide_with_voxels = true;

		if (!Engine::IsReleaseMode())
			AddComponent<EditorIconComponent>(EngineActorIcon::Actor);

		SyncCollider();
	}

	void Humanoid::SyncCollider()
	{
		if (!collider_)
			return;

		collider_->size = { std::max(collider_size.x, 0.01f), std::max(collider_size.y, 0.01f) };
		collider_->layer = static_cast<uint32_t>(collision_layer);
		collider_->mask = static_cast<uint32_t>(collision_mask);
		// La boite : toujours dans l'editeur, en jeu si demande.
		collider_->debug_draw = playing_ ? show_collider : !Engine::IsReleaseMode();
	}

	void Humanoid::Update(double dt)
	{
		Actor::Update(dt);

		// Reglages modifiables a tout moment (Details, JS).
		SyncCollider();
	}

	void Humanoid::StartGame()
	{
		Actor::StartGame();

		playing_ = true;
		velocity_ = { 0.f, 0.f };
		move_input_ = 0.f;
		jump_held_ = false;
		jump_hold_timer_ = 0.f;
		jump_count_ = 0;
		facing_right_ = transform.scale.x >= 0.f;

		SyncCollider();

		// Pose au sol au lancement : pas d'OnLanded parasite a la premiere frame.
		grounded_ = collider_ && collider_->IsBlockedAt({ 0.f, -ContactDistance(), 0.f });
	}

	void Humanoid::EndGame()
	{
		Actor::EndGame();

		playing_ = false;
		SyncCollider();
	}

	void Humanoid::Tick(double dt)
	{
		Actor::Tick(dt);

		SyncCollider();

		if (movement_enabled && collider_)
			UpdateMovement(static_cast<float>(dt));
	}


	// ========================================================================
	// Commandes de mouvement
	// ========================================================================

	void Humanoid::Move(float direction)
	{
		move_input_ = std::clamp(direction, -1.f, 1.f);

		if (move_input_ > kNoInput)
			facing_right_ = true;
		else if (move_input_ < -kNoInput)
			facing_right_ = false;
		else
			return;

		if (face_movement_direction)
		{
			const float sx = std::fabs(transform.scale.x);
			transform.scale.x = facing_right_ ? sx : -sx;
		}
	}

	bool Humanoid::Jump()
	{
		if (!movement_enabled || jump_count_ >= max_jump_count)
			return false;

		// Tombe d'une plateforme sans sauter : le premier saut est "consomme"
		// (comme un double saut, pas un saut depuis le sol).
		if (!grounded_ && jump_count_ == 0)
		{
			if (max_jump_count < 2)
				return false;
			jump_count_ = 1;
		}

		++jump_count_;

		velocity_.y = jump_speed;
		jump_hold_timer_ = jump_hold_time;
		jump_held_ = true;
		grounded_ = false;

		OnJumped(jump_count_);
		scripting::CallActorEvent(this, "OnJumped", nullptr);
		return true;
	}

	void Humanoid::StopJumping()
	{
		jump_held_ = false;
	}

	void Humanoid::Launch(float x, float y, bool override_x, bool override_y)
	{
		velocity_.x = override_x ? x : velocity_.x + x;
		velocity_.y = override_y ? y : velocity_.y + y;

		if (velocity_.y > 0.f)
		{
			grounded_ = false;
			// Un lancer n'est pas un saut tenu.
			jump_hold_timer_ = 0.f;
		}
	}

	void Humanoid::StopMovement()
	{
		velocity_ = { 0.f, 0.f };
		move_input_ = 0.f;
		jump_hold_timer_ = 0.f;
	}

	bool Humanoid::IsGroundWithinDistance(float distance) const
	{
		if (!collider_)
			return false;
		return collider_->IsBlockedAt({ 0.f, -std::max(distance, ContactDistance()), 0.f });
	}


	// ========================================================================
	// Physique
	// ========================================================================

	float Humanoid::ContactDistance() const
	{
		// MoveAndCollide s'arrete a une fraction infime du mur : un contact se
		// detecte un peu plus loin, proportionnellement a la boite.
		const float h = std::fabs(collider_size.y * transform.scale.y);
		return std::max(1e-3f, h * 0.005f);
	}

	void Humanoid::UpdateMovement(float dt)
	{
		if (dt <= 0.f)
			return;

		// Voxels around : surface (friction, bounce) and fluids (drag, buoyancy).
		const lynx::voxel_physics::Medium medium = lynx::voxel_physics::GetMedium(this);
		ground_bounciness_ = medium.ground_bounciness;
		const float grip = std::clamp(medium.ground_friction, 0.02f, 5.f);
		const bool swimming = medium.fluid_fraction > 0.3f && medium.buoyancy > 0.f;

		// ---------------------------------------------------- horizontal
		// Sticky ground (friction > 1) : slower walk.
		const float target = move_input_ * move_speed * (grounded_ && grip > 1.f ? 1.f / std::sqrt(grip) : 1.f);
		const bool no_input = std::fabs(move_input_) < kNoInput;

		if (grounded_)
		{
			// Ice (friction < 1) : slow to start and to stop.
			if (no_input)
				velocity_.x = MoveTowards(velocity_.x, 0.f, ground_deceleration * grip * dt);
			else if (std::fabs(velocity_.x) > kNoInput && std::signbit(velocity_.x) != std::signbit(target))
				velocity_.x = MoveTowards(velocity_.x, target, direction_change_acceleration * grip * dt);
			else
				velocity_.x = MoveTowards(velocity_.x, target, ground_acceleration * std::min(grip, 1.5f) * dt);
		}
		else
		{
			const float control = std::clamp(air_control, 0.f, 1.f);
			if (no_input)
				velocity_.x = MoveTowards(velocity_.x, 0.f, air_deceleration * control * dt);
			else
				velocity_.x = MoveTowards(velocity_.x, target, air_acceleration * control * dt);
		}

		// ---------------------------------------------------- vertical
		const bool holding_jump = jump_held_ && jump_hold_timer_ > 0.f && velocity_.y > 0.f;

		if (holding_jump)
		{
			jump_hold_timer_ -= dt;
			velocity_.y -= gravity * jump_hold_gravity_scale * dt;
		}
		else
		{
			jump_hold_timer_ = 0.f;
			velocity_.y -= gravity * dt;
		}

		// Inside a liquid / gas : buoyancy (against the gravity) and drag.
		if (medium.fluid_fraction > 0.f)
		{
			velocity_.y += gravity * medium.buoyancy * medium.fluid_fraction * dt;
			const float keep = std::max(0.f, 1.f - medium.drag * medium.fluid_fraction * 5.f * dt);
			velocity_.x *= keep;
			velocity_.y *= keep;
		}
		// Swimming : Jump again and again.
		if (swimming)
			jump_count_ = 0;

		if (max_fall_speed > 0.f)
			velocity_.y = std::max(velocity_.y, -max_fall_speed);

		IntegrateMovement(dt);
	}

	void Humanoid::IntegrateMovement(float dt)
	{
		if (!collider_)
			return;

		const bool was_grounded = grounded_;
		const float contact = ContactDistance();
		const float fall_speed = velocity_.y;   // before the landing (bounce)

		// ---------------------------------------------------- X
		const float dx = velocity_.x * dt;

		if (dx != 0.f)
		{
			const float dir = dx > 0.f ? 1.f : -1.f;
			float remaining = dx;

			// Deja contre un mur : pas de MoveAndCollide (un OnHit par
			// contact, pas un par frame).
			if (!collider_->IsBlockedAt({ dir * contact, 0.f, 0.f }))
			{
				const vec3 applied = collider_->MoveAndCollide({ dx, 0.f, 0.f });
				remaining = collider_->WasBlockedX() ? dx - applied.x : 0.f;
			}

			if (remaining != 0.f && !(was_grounded && TryStepUp(remaining)))
				velocity_.x = 0.f;
		}

		// ---------------------------------------------------- Y
		const float dy = velocity_.y * dt;
		grounded_ = false;

		if (dy < 0.f && collider_->IsBlockedAt({ 0.f, -contact, 0.f }))
		{
			// Pose au sol : rien a faire (pas d'OnHit a chaque frame).
			grounded_ = true;
			velocity_.y = 0.f;
		}
		else if (dy > 0.f && collider_->IsBlockedAt({ 0.f, contact, 0.f }))
		{
			// Tete contre le plafond.
			velocity_.y = 0.f;
			jump_hold_timer_ = 0.f;
		}
		else if (dy != 0.f)
		{
			collider_->MoveAndCollide({ 0.f, dy, 0.f });

			if (collider_->WasBlockedY())
			{
				if (dy < 0.f)
					grounded_ = true;
				else
					jump_hold_timer_ = 0.f;
				velocity_.y = 0.f;
			}
			// Descente d'une marche en marchant : reste au sol au lieu de
			// "tomber" une frame.
			else if (was_grounded && dy < 0.f && SnapToFloor())
			{
				grounded_ = true;
				velocity_.y = 0.f;
			}
		}

		OnTransformChanged();

		// Bouncy ground (rubber...) : back up.
		if (grounded_ && !was_grounded && ground_bounciness_ > 0.f && fall_speed < -3.f)
		{
			velocity_.y = -fall_speed * std::min(ground_bounciness_, 1.f);
			grounded_ = false;
			FireLanded();
			return;
		}

		if (grounded_)
		{
			jump_count_ = 0;
			if (!was_grounded)
				FireLanded();
		}
	}

	bool Humanoid::TryStepUp(float dx)
	{
		const float step = max_step_height * std::fabs(transform.scale.y);

		if (step <= 0.f || !collider_)
			return false;

		// Place au-dessus, et la marche est franchissable a cette hauteur.
		if (collider_->IsBlockedAt({ 0.f, step, 0.f }) || collider_->IsBlockedAt({ dx, step, 0.f }))
			return false;

		// Plus petite hauteur qui libere la boite (dichotomie).
		float lo = 0.f;
		float hi = step;
		for (int i = 0; i < 14; ++i)
		{
			const float mid = (lo + hi) * 0.5f;
			if (collider_->IsBlockedAt({ dx, mid, 0.f }))
				lo = mid;
			else
				hi = mid;
		}

		transform.location.x += dx;
		transform.location.y += hi + ContactDistance() * 0.25f;
		return true;
	}

	bool Humanoid::SnapToFloor()
	{
		const float step = max_step_height * std::fabs(transform.scale.y);

		// Pas en train de sauter, et un sol a moins d'une marche.
		if (step <= 0.f || !collider_ || velocity_.y > 0.f ||
		    !collider_->IsBlockedAt({ 0.f, -step, 0.f }))
			return false;

		float lo = 0.f;   // libre
		float hi = step;  // bloque
		for (int i = 0; i < 14; ++i)
		{
			const float mid = (lo + hi) * 0.5f;
			if (collider_->IsBlockedAt({ 0.f, -mid, 0.f }))
				hi = mid;
			else
				lo = mid;
		}

		transform.location.y -= lo;
		return true;
	}

	void Humanoid::FireLanded()
	{
		OnLanded();
		scripting::CallActorEvent(this, "OnLanded", nullptr);
	}
}
