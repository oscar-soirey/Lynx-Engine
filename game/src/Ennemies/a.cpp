#include "a.h"

void A::Init()
{
	relative_sprite_transform_.scale.x = 7.f;
	relative_sprite_transform_.scale.y = 7.f;
	relative_sprite_transform_.location.x = 1.f;
	relative_sprite_transform_.location.y = 2.f;

	Ennemy::Init();

	bs_default.add(
				0.f,
				idle_
		);

	bs_default.add(
			1.f,
			walk_
	);

	bs_default.add(
			-1.f,
			walk_
	);

	bs_hurt.add(0.f, hurt_);
	bs_hurt.on_finished([&]{ current_blendspace_ = &bs_default; });
}

void A::Hurt(Actor *instigator, float amount)
{
	Ennemy::Hurt(instigator, amount);
	current_blendspace_ = &bs_hurt;
	bs_hurt.restart();
}
