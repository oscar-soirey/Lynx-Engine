#pragma once

#include <Lynx.h>

// =============================================================================
// Player : side-view character
// -----------------------------------------------------------------------------
// Gravity, jump, collisions against the voxel world (types with a collision in
// assets/voxels.json). Keys : input.json ("move_x", "jump").
// Every HPROPERTY is editable in the Details window and saved with the level.
// =============================================================================

class Player : public lynx::Actor
{
public:
	Player();

	void Init() override;
	void StartGame() override;
	void Tick(double dt) override;

	void Jump();

protected:
	// Tweaks (voxels, seconds : 1 unit = 1 voxel)
	float move_speed_ = 23.f;
	float acceleration_ = 200.f;
	float jump_speed_ = 40.f;
	float gravity_ = 107.f;
	float max_fall_speed_ = 83.f;
	lynx::vec2 collider_size_{ 2.3f, 3.2f };
	float kill_depth_ = 130.f;         // fell that far below the start : respawn

private:
	void ProcessInput() override;

	// true if the collider centered on (x, y) touches a solid voxel.
	bool Blocked(float x, float y) const;

	// Moves along one axis in small steps, stops against the voxels.
	// Returns false if it was blocked.
	bool MoveAxis(float& position, float delta, bool horizontal);

	lynx::InputAxis1D move_ = "move_x";
	lynx::InputAction jump_ = "jump";

	float input_x_ = 0.f;
	bool jump_pressed_ = false;
	bool jump_held_ = false;

	float velocity_x_ = 0.f;
	float velocity_y_ = 0.f;
	bool on_ground_ = false;
	float coyote_time_ = 0.f;          // a jump is still allowed just after leaving a ledge

	lynx::vec3 start_location_{};
	lynx::StaticSpriteComponent* sprite_ = nullptr;
	lynx::PointLightComponent* lantern_ = nullptr;   // small light that follows the player
};
