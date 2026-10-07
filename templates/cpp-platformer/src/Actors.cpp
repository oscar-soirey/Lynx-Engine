#include "Game.h"

#include <cmath>
#include <cstdlib>

namespace
{
	// Destroyed at the end of the frame (Level::DestroyActor is deferred).
	void DestroyLater(lynx::Actor* actor)
	{
		if (lynx::Level* level = lynx::Engine::Get()->GetCurrentLevel())
			level->DestroyActor(actor);
	}
}

// =============================================================================
// Coin
// =============================================================================

Coin::Coin()
{
	HPROPERTY(value_, lynx::Exposed);
}

void Coin::Init()
{
	lynx::Actor::Init();

	auto& anim = AddComponent<lynx::AnimationSpriteComponent>();
	anim.size = { 2.2f, 2.2f };
	anim.SetAnimation("sprites/coin_spin.png", 4, 0.12f, true);

	auto& box = AddComponent<lynx::ColliderComponent>();
	box.size = { 1.8f, 1.8f };
	box.trigger = true;
	box.movable = false;
}

void Coin::StartGame()
{
	lynx::Actor::StartGame();
	base_y_ = transform.location.y;
	time_ = static_cast<float>(std::rand() % 600) / 100.f;
	taken_ = false;
}

void Coin::Tick(double dt)
{
	lynx::Actor::Tick(dt);
	time_ += static_cast<float>(dt);
	transform.location.y = base_y_ + std::sin(time_ * 3.f) * 0.3f;
	OnTransformChanged();
}

void Coin::OnBeginOverlap(lynx::Actor* other)
{
	if (taken_ || !other || !other->Implements("Collector"))
		return;
	taken_ = true;
	lynx::interfaces::Call(other, "Collector", "OnCollected", { std::string("coin"), value_ });
	DestroyLater(this);
}


// =============================================================================
// Slime
// =============================================================================

Slime::Slime()
{
	HPROPERTY(damage_, lynx::Exposed);

	collider_size = { 2.2f, 1.4f };
	move_speed = 5.f;
	jump_speed = 16.f;
	gravity = 70.f;
	// Layer 2 meeting only layer 2 : the hero goes through, the contact is tested in Tick.
	collision_layer = 2;
	collision_mask = 2;

	BindInterfaceFunction("Damageable", "TakeDamage", [this](const lynx::InterfaceArgs&) -> lynx::InterfaceArg
	{
		Squash();
		return {};
	});
}

void Slime::Init()
{
	lynx::Humanoid::Init();
	AddComponent<lynx::TagsComponent>().Add("enemy");
	anim_ = &AddComponent<lynx::AnimationSpriteComponent>();
	anim_->size = { 2.8f, 2.8f };
	anim_->offset = { 0.f, 0.6f, 0.f };
	anim_->SetAnimation("sprites/slime.png", 2, 0.25f, true);
}

void Slime::StartGame()
{
	lynx::Humanoid::StartGame();
	dir_ = -1.f;
	dead_ = false;
}

void Slime::Tick(double dt)
{
	lynx::Humanoid::Tick(dt);

	if (dead_)
	{
		dead_time_ += static_cast<float>(dt);
		if (dead_time_ > 0.5f)
			DestroyLater(this);
		return;
	}

	// Wall in front, or no ground in front : turn around (voxels only).
	const lynx::vec2 p(transform.location.x, transform.location.y);
	const lynx::vec2 ahead(p.x + dir_ * 1.7f, p.y);
	lynx::physics::QueryParams params;
	params.actors = false;
	const bool wall = lynx::physics::Raycast(p, ahead, params).hit;
	const bool ground = lynx::physics::Raycast(ahead, { ahead.x, ahead.y - 2.5f }, params).hit;
	if (IsGrounded() && (wall || !ground))
		dir_ = -dir_;
	Move(dir_);

	// Contact with the hero : squashed from above, damage otherwise.
	lynx::Level* level = lynx::Engine::Get()->GetCurrentLevel();
	lynx::Actor* hero = level ? level->GetActorFromID("hero") : nullptr;
	auto* humanoid = dynamic_cast<lynx::Humanoid*>(hero);
	if (!humanoid)
		return;
	const float dx = hero->transform.location.x - p.x;
	const float dy = hero->transform.location.y - p.y;
	if (std::abs(dx) > 2.f || std::abs(dy) > 2.4f)
		return;
	if (dy > 1.2f && humanoid->GetVelocity().y < 0.f)
	{
		humanoid->Launch(0.f, 26.f, false, true);
		Squash();
	}
	else
	{
		lynx::ApplyDamage(hero, static_cast<float>(damage_), this);
	}
}

void Slime::Squash()
{
	if (dead_)
		return;
	dead_ = true;
	dead_time_ = 0.f;
	movement_enabled = false;
	anim_->size = { 3.2f, 1.2f };
	anim_->offset = { 0.f, -0.4f, 0.f };
}
