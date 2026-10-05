#include "Player.h"

#include <hrl/hrl.h>

#include <algorithm>
#include <cmath>

namespace
{
	// Voxel collision sides that block the player (types "SOLID" in voxels.json).
	constexpr uint32_t kSolidMask =
		HRL_VOXEL_COLLISION_LEFT | HRL_VOXEL_COLLISION_RIGHT |
		HRL_VOXEL_COLLISION_TOP | HRL_VOXEL_COLLISION_BOTTOM;

	// Collider shrunk a little : a box exactly touching the ground is not "inside" it.
	constexpr float kSkin = 0.07f;   // voxels

	// Largest step of a move (voxels) : no tunnelling through thin platforms.
	constexpr float kMaxStep = 0.2f;

	constexpr float kCoyoteTime = 0.1f;

	float MoveTowards(float current, float target, float max_delta)
	{
		if (current < target)
			return std::min(current + max_delta, target);
		return std::max(current - max_delta, target);
	}
}


Player::Player()
{
	HPROPERTY(move_speed_, lynx::Exposed);
	HPROPERTY(acceleration_, lynx::Exposed);
	HPROPERTY(jump_speed_, lynx::Exposed);
	HPROPERTY(gravity_, lynx::Exposed);
	HPROPERTY(max_fall_speed_, lynx::Exposed);
	HPROPERTY(collider_size_, lynx::Exposed);
	HPROPERTY(kill_depth_, lynx::Exposed);

	HFUNCTION(Jump);   // also callable from JavaScript : Level.find("player").Jump()
}


void Player::Init()
{
	lynx::Actor::Init();

	// Visible in the editor too (Init runs when the level is loaded).
	sprite_ = &AddComponent<lynx::StaticSpriteComponent>();
	sprite_->texture = "sprites/player.png";
	sprite_->size = { 3.3f, 3.3f };   // voxels

	// A light carried by the player : the component creates and moves the
	// renderer light itself (no HRL call).
	lantern_ = &AddComponent<lynx::PointLightComponent>();
	lantern_->color = { 1.f, 0.85f, 0.6f };
	lantern_->intensity = 0.8f;
	lantern_->offset = { 0.f, 1.f, 5.f };
}


void Player::StartGame()
{
	lynx::Actor::StartGame();

	start_location_ = transform.location;
	velocity_x_ = 0.f;
	velocity_y_ = 0.f;
}


void Player::ProcessInput()
{
	input_x_ = move_.GetValue();

	if (jump_.IsPressed())
		jump_pressed_ = true;

	jump_held_ = jump_.IsHeld();
}


void Player::Jump()
{
	velocity_y_ = jump_speed_;
	on_ground_ = false;
	coyote_time_ = 0.f;
}


bool Player::Blocked(float x, float y) const
{
	// Positions and sizes are already in voxels (1 unit = 1 voxel).
	const uint32_t scene = lynx::Engine::GetScene();

	HRL_VoxelCollision hit{};
	return HRL_VoxelCheckCollision(scene, x, y,
	                               collider_size_.x - kSkin * 2.f, collider_size_.y - kSkin * 2.f,
	                               kSolidMask, &hit) == HRL_TRUE;
}


bool Player::MoveAxis(float& position, float delta, bool horizontal)
{
	const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / kMaxStep)));
	const float step = delta / static_cast<float>(steps);

	for (int i = 0; i < steps; ++i)
	{
		const float next = position + step;
		const bool blocked = horizontal
			? Blocked(next, transform.location.y)
			: Blocked(transform.location.x, next);

		if (blocked)
			return false;

		position = next;
	}

	return true;
}


void Player::Tick(double dt_double)
{
	lynx::Actor::Tick(dt_double);

	const float dt = std::min(static_cast<float>(dt_double), 1.f / 20.f);

	// Horizontal : accelerate towards the wanted speed.
	velocity_x_ = MoveTowards(velocity_x_, input_x_ * move_speed_, acceleration_ * dt);

	// Ground and jump (with a short "coyote time").
	on_ground_ = Blocked(transform.location.x, transform.location.y - kSkin * 2.f);
	coyote_time_ = on_ground_ ? kCoyoteTime : std::max(0.f, coyote_time_ - dt);

	if (jump_pressed_ && coyote_time_ > 0.f)
		Jump();
	jump_pressed_ = false;

	// Releasing the key early : shorter jump.
	if (!jump_held_ && velocity_y_ > 0.f)
		velocity_y_ *= 0.5f;

	velocity_y_ = std::max(velocity_y_ - gravity_ * dt, -max_fall_speed_);

	// Move, one axis at a time.
	lynx::vec3 location = transform.location;

	if (!MoveAxis(location.x, velocity_x_ * dt, true))
		velocity_x_ = 0.f;
	transform.location.x = location.x;

	if (!MoveAxis(location.y, velocity_y_ * dt, false))
		velocity_y_ = 0.f;
	transform.location.y = location.y;

	// Fell out of the world : back to the start.
	if (transform.location.y < start_location_.y - kill_depth_)
	{
		transform.location = start_location_;
		velocity_x_ = 0.f;
		velocity_y_ = 0.f;
	}

	if (sprite_ && std::abs(input_x_) > 0.1f)
		sprite_->flip_x = input_x_ < 0.f;

	OnTransformChanged();
}
