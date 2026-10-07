#pragma once

#include <Lynx.h>

// =============================================================================
// {{PROJECT_TITLE}} : the classes of the game (C++ version of the "Plateforme
// 2D" template). Registered in Module.cpp, they appear in Place Actors.
//
// Interfaces (gameplay/Interface.h) declared in LynxGame_SetupScene :
//   "Collector"  OnCollected(kind, amount)    the hero picks up coins
//   engine "Damageable" TakeDamage(amount, instigator)
// =============================================================================

// -----------------------------------------------------------------------------
// Hero : a lynx::Humanoid (walk, jump, gravity, collisions, voxel surfaces)
// driven by input.json : move_x, jump, dash, pound, restart.
// -----------------------------------------------------------------------------
class Hero : public lynx::Humanoid
{
public:
	Hero();

	void Init() override;
	void StartGame() override;
	void Tick(double dt) override;

protected:
	void ProcessInput() override;
	void OnLanded() override;

	// Tweaks (Details window, saved with the level)
	int max_hp_ = 5;
	float dash_speed_ = 36.f;
	float pound_speed_ = 70.f;
	float kill_height_ = 40.f;

private:
	void TakeDamage(float amount, lynx::Actor* instigator);
	void Respawn();
	void RefreshHud();
	void UpdateAnimation();

	lynx::InputAxis1D move_ = "move_x";
	lynx::InputAction jump_ = "jump";
	lynx::InputAction dash_ = "dash";
	lynx::InputAction pound_ = "pound";
	lynx::InputAction restart_ = "restart";

	lynx::AnimationSpriteComponent* anim_ = nullptr;
	lynx::UserWidget* hud_ = nullptr;
	std::string anim_state_;

	int hp_ = 5;
	int coins_ = 0;
	int total_coins_ = 0;
	float invulnerable_ = 0.f;
	float dash_left_ = 0.f;
	float dash_cooldown_ = 0.f;
	float dash_dir_ = 1.f;
	bool pounding_ = false;
	lynx::vec3 start_{};
};

// -----------------------------------------------------------------------------
// Coin : trigger box, gives itself to any "Collector".
// -----------------------------------------------------------------------------
class Coin : public lynx::Actor
{
public:
	Coin();

	void Init() override;
	void StartGame() override;
	void Tick(double dt) override;

protected:
	void OnBeginOverlap(lynx::Actor* other) override;

	int value_ = 1;

private:
	float base_y_ = 0.f;
	float time_ = 0.f;
	bool taken_ = false;
};

// -----------------------------------------------------------------------------
// Slime : Humanoid patrolling, turns at walls and ledges (physics raycasts).
// -----------------------------------------------------------------------------
class Slime : public lynx::Humanoid
{
public:
	Slime();

	void Init() override;
	void StartGame() override;
	void Tick(double dt) override;

	void Squash();

protected:
	int damage_ = 1;

private:
	float dir_ = -1.f;
	bool dead_ = false;
	float dead_time_ = 0.f;
	lynx::AnimationSpriteComponent* anim_ = nullptr;
};
