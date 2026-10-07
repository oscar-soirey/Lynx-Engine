#include "Game.h"

#include <algorithm>
#include <cmath>

Hero::Hero()
{
	HPROPERTY(max_hp_, lynx::Exposed);
	HPROPERTY(dash_speed_, lynx::Exposed);
	HPROPERTY(pound_speed_, lynx::Exposed);
	HPROPERTY(kill_height_, lynx::Exposed);

	// Humanoid settings (voxels, seconds).
	collider_size = { 1.8f, 2.9f };
	move_speed = 16.f;
	jump_speed = 30.f;
	gravity = 75.f;
	max_fall_speed = 60.f;
	max_jump_count = 2;
	auto_possess_player = 0;   // player 0 controls the hero at Play

	// Interfaces : C++ handlers (JS classes and scripts can send them too).
	BindInterfaceFunction("Damageable", "TakeDamage", [this](const lynx::InterfaceArgs& args) -> lynx::InterfaceArg
	{
		TakeDamage(lynx::InterfaceArgFloat(args, 0, 1.f), lynx::InterfaceArgActor(args, 1));
		return {};
	});
	BindInterfaceFunction("Collector", "OnCollected", [this](const lynx::InterfaceArgs& args) -> lynx::InterfaceArg
	{
		if (const auto* kind = args.empty() ? nullptr : std::get_if<std::string>(&args[0]); kind && *kind == "coin")
			coins_ += lynx::InterfaceArgInt(args, 1, 1);
		RefreshHud();
		return {};
	});
}

void Hero::Init()
{
	lynx::Humanoid::Init();
	AddComponent<lynx::TagsComponent>().Add("player");

	anim_ = &AddComponent<lynx::AnimationSpriteComponent>();
	anim_->size = { 3.4f, 3.4f };
	anim_->offset = { 0.f, 0.15f, 0.f };
	anim_->SetAnimation("sprites/hero_idle.png", 2, 0.45f, true);
}

void Hero::StartGame()
{
	lynx::Humanoid::StartGame();

	hp_ = max_hp_;
	coins_ = 0;
	start_ = transform.location;
	invulnerable_ = 0.f;
	anim_state_.clear();

	auto& camera = AddComponent<lynx::CameraComponent>();
	camera.offset = { 0.f, 3.f, 60.f };
	camera.fov = 32.f;
	camera.follow_speed = 6.f;
	camera.follow_delay = 0.04f;

	auto& light = AddComponent<lynx::PointLightComponent>();
	light.color = { 1.f, 0.85f, 0.6f };
	light.intensity = 0.5f;
	light.offset = { 0.f, 1.f, 6.f };

	total_coins_ = 0;
	if (lynx::Level* level = lynx::Engine::Get()->GetCurrentLevel())
		for (lynx::Actor* a : level->GetActors())
			if (dynamic_cast<Coin*>(a))
				++total_coins_;

	hud_ = lynx::CreateWidget(nullptr, "ui/Hud.widget");
	if (hud_)
		hud_->AddToViewport();
	RefreshHud();
}

void Hero::ProcessInput()
{
	if (dash_left_ <= 0.f && !pounding_)
		Move(move_.GetValue());
	if (jump_.IsPressed())
		Jump();
	if (jump_.IsReleased())
		StopJumping();
	if (dash_.IsPressed() && dash_cooldown_ <= 0.f)
	{
		dash_dir_ = IsFacingRight() ? 1.f : -1.f;
		dash_left_ = 0.16f;
		dash_cooldown_ = 0.6f;
	}
	if (pound_.IsPressed() && !IsGrounded() && !pounding_)
		pounding_ = true;
	if (restart_.IsPressed())
		Respawn();
}

void Hero::Tick(double dt_double)
{
	lynx::Humanoid::Tick(dt_double);
	const float dt = static_cast<float>(dt_double);

	dash_cooldown_ -= dt;
	if (dash_left_ > 0.f)
	{
		dash_left_ -= dt;
		SetVelocity(dash_dir_ * dash_speed_, 0.f);
	}
	if (pounding_)
		SetVelocity(0.f, -pound_speed_);

	if (invulnerable_ > 0.f)
	{
		invulnerable_ -= dt;
		anim_->visible = invulnerable_ <= 0.f || static_cast<int>(invulnerable_ * 14.f) % 2 == 0;
	}

	if (transform.location.y < start_.y - kill_height_)
	{
		TakeDamage(1.f, nullptr);
		Respawn();
	}

	UpdateAnimation();
}

void Hero::OnLanded()
{
	if (!pounding_)
		return;
	pounding_ = false;

	const lynx::vec2 feet(transform.location.x, transform.location.y - collider_size.y * 0.5f);

	// Only the voxels whose type has the flag BREAKABLE (voxels.json : Brick).
	lynx::voxels::VoxelFilter filter;
	filter.flags = lynx::voxels::GetFlagMask("BREAKABLE");
	filter.keep_cells = false;
	const lynx::voxels::VoxelEditResult broken =
		lynx::voxels::DestroyRect({ feet.x, feet.y - 1.5f }, { 5.f, 3.f }, filter);
	if (broken.Any())
		SetVelocity(0.f, -pound_speed_ * 0.5f);   // keeps falling through

	// Actors around the impact.
	lynx::physics::QueryParams params;
	params.ignore = { this };
	params.voxels = false;
	for (lynx::Actor* actor : lynx::physics::OverlapCircle(feet, 3.5f, params).actors)
		lynx::ApplyDamage(actor, 5.f, this);
}

void Hero::TakeDamage(float amount, lynx::Actor* instigator)
{
	if (invulnerable_ > 0.f)
		return;
	hp_ -= std::max(1, static_cast<int>(std::round(amount)));
	invulnerable_ = 1.2f;
	pounding_ = false;

	if (instigator)
		Launch(transform.location.x >= instigator->transform.location.x ? 20.f : -20.f, 18.f, true, true);
	else
		Launch(0.f, 24.f, false, true);

	if (hp_ <= 0)
		Respawn();
	RefreshHud();
}

void Hero::Respawn()
{
	transform.location = start_;
	OnTransformChanged();
	StopMovement();
	hp_ = max_hp_;
	invulnerable_ = 1.5f;
	RefreshHud();
}

void Hero::RefreshHud()
{
	if (!hud_)
		return;
	if (auto* text = hud_->GetWidget<lynx::TextBlock>("Coins"))
		text->text = std::to_string(coins_) + " / " + std::to_string(total_coins_);
	for (int i = 1; i <= 5; ++i)
		if (auto* heart = hud_->GetWidget<lynx::Image>("Heart" + std::to_string(i)))
			heart->SetVisibility(i <= hp_ ? lynx::EVisibility::Visible : lynx::EVisibility::Hidden);
}

void Hero::UpdateAnimation()
{
	std::string state;
	if (!IsGrounded())
		state = GetVelocity().y > 0.f ? "jump" : "fall";
	else
		state = std::abs(GetVelocity().x) > 1.f ? "run" : "idle";

	if (state == anim_state_)
		return;
	anim_state_ = state;

	if (state == "run")
		anim_->SetAnimation("sprites/hero_run.png", 4, 0.08f, true);
	else if (state == "jump")
		anim_->SetAnimation("sprites/hero_jump.png", 1, 1.f, true);
	else if (state == "fall")
		anim_->SetAnimation("sprites/hero_fall.png", 1, 1.f, true);
	else
		anim_->SetAnimation("sprites/hero_idle.png", 2, 0.45f, true);
}
